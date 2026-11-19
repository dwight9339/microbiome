/* 3V3 rail load test (separate firmware: microbiome_loadtest.uf2).
 *
 * Steps the board through fixed load stages so the rail can be measured
 * with a meter / scope: backlight, LCD streaming, second core, amp quiescent,
 * tones up to a full-scale square wave, and load-step bursts for transient
 * droop. The stage number is drawn large in the top-left of the LCD, so it
 * can be run on battery with no USB attached.
 *
 * Buttons: right = next stage, center = previous stage (both wrap),
 * up/down = audio level +/-10%. The auto sequence is serial-only ("auto").
 *
 * Scope trigger on LOADTEST_PIN_TRIG (GP10 by default, GP26/A0 on the
 * Feather prototype): high during each
 * audio burst, or toggling once per wave period for continuous tones.
 * Edges line up with the start of the load step to within a few us.
 *
 * A 3 s watchdog runs; if the board locks up (e.g. flash reads corrupted by
 * a sagging rail) it reboots and reports which stage it was in.
 *
 * Soak test: "log 30" prints the RP2350 die temperature every 30 s, with
 * its rise since the stage was entered (a board-temperature proxy; the
 * regulator itself runs hotter).
 *
 * USB serial console: type "help".
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/structs/powman.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "st7789.h"

#ifndef LOADTEST_PIN_TRIG
#define LOADTEST_PIN_TRIG 10
#endif
_Static_assert(LOADTEST_PIN_TRIG != AMP_PIN_SD && LOADTEST_PIN_TRIG != AUDIO_PIN_PWM &&
                   LOADTEST_PIN_TRIG != LCD_PIN_BL,
               "LOADTEST_PIN_TRIG collides with an audio or backlight pin");

#define AUDIO_RATE   150000u /* PWM carrier = sample rate, Hz */
#define AUDIO_MAXLEN 1024u   /* lowest tone = AUDIO_RATE / AUDIO_MAXLEN = 147 Hz */
#define DWELL_S_DEF  10u

enum { LCD_STATIC, LCD_STREAM };
enum { WAVE_NONE, WAVE_SINE, WAVE_SQUARE };
static const char *const k_wave_name[] = {"none", "sine", "square"};

typedef struct {
    const char *name;
    uint8_t bl;       /* backlight duty, % */
    uint8_t lcd;      /* LCD_STATIC: one frame then idle; LCD_STREAM: full frames back to back */
    uint8_t core1;    /* 1 = second core spinning */
    uint8_t amp;      /* 1 = amp out of shutdown */
    uint8_t wave;
    uint16_t freq;    /* Hz */
    uint8_t level;    /* % of full-scale PWM swing */
    uint16_t on_ms, off_ms; /* bursts; 0/0 = continuous */
} load_t;

static const load_t k_stages[] = {
    {"baseline: backlight off, LCD idle, amp shutdown", 0, LCD_STATIC, 0, 0, WAVE_NONE, 1000, 0, 0, 0},
    {"backlight 100%", 100, LCD_STATIC, 0, 0, WAVE_NONE, 1000, 0, 0, 0},
    {"+ LCD streaming", 100, LCD_STREAM, 0, 0, WAVE_NONE, 1000, 0, 0, 0},
    {"+ core1 busy", 100, LCD_STREAM, 1, 0, WAVE_NONE, 1000, 0, 0, 0},
    {"+ amp enabled, silent", 100, LCD_STREAM, 1, 1, WAVE_NONE, 1000, 0, 0, 0},
    {"+ 1 kHz sine 25%", 100, LCD_STREAM, 1, 1, WAVE_SINE, 1000, 25, 0, 0},
    {"+ 1 kHz sine 100%", 100, LCD_STREAM, 1, 1, WAVE_SINE, 1000, 100, 0, 0},
    {"+ 200 Hz square 100% (steady worst case)", 100, LCD_STREAM, 1, 1, WAVE_SQUARE, 200, 100, 0, 0},
    {"+ 200 Hz square 100%, 100/400 ms bursts (transient)", 100, LCD_STREAM, 1, 1, WAVE_SQUARE, 200, 100, 100, 400},
    {"amp only: 200 Hz square 100%, backlight off, LCD idle", 0, LCD_STATIC, 0, 1, WAVE_SQUARE, 200, 100, 0, 0},
};
#define NSTAGES ((int)(sizeof k_stages / sizeof k_stages[0]))

static load_t cur;
static int cur_stage;
static bool lcd_dirty = true;
static bool auto_on;
static uint32_t dwell_s = DWELL_S_DEF, next_auto_ms;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

/* ---- backlight PWM ------------------------------------------------------ */

#define BL_TOP 999u

static void backlight_init(void) {
    if (LCD_PIN_BL < 0) return;
    uint slice = pwm_gpio_to_slice_num(LCD_PIN_BL);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, BL_TOP);
    pwm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / ((BL_TOP + 1) * 10000.0f)); /* 10 kHz */
    pwm_init(slice, &c, true);
    gpio_set_function(LCD_PIN_BL, GPIO_FUNC_PWM);
}

static void backlight_set(unsigned pct) {
    if (LCD_PIN_BL >= 0) pwm_set_gpio_level(LCD_PIN_BL, (uint16_t)(pct * (BL_TOP + 1) / 100));
}

/* ---- audio: wavetable -> PWM compare via two chained DMA channels -------- */
/* "data" copies the table into the slice's CC register, one sample per PWM
 * wrap; when it finishes it chains to "ctrl", which reloads data's read
 * address from tab_ptr and retriggers it. Switching tab_ptr between the
 * tone and a silence table makes bursts without stopping the DMA. */

#if AUDIO_PIN_PWM >= 0
static uint32_t wave_tab[AUDIO_MAXLEN], silence_tab[AUDIO_MAXLEN];
static const uint32_t *volatile tab_ptr;
static int dma_data = -1, dma_ctrl = -1;
static uint audio_slice, audio_top;
static uint32_t audio_len;
static volatile uint32_t burst_on_loops, burst_off_loops, burst_left;
static volatile bool burst_state, burst_queued, wave_active;

static uint32_t cc_word(uint32_t level) {
    return pwm_gpio_to_channel(AUDIO_PIN_PWM) == PWM_CHAN_B ? level << 16 : level;
}

static void __isr audio_irq(void) {
    dma_channel_acknowledge_irq0((uint)dma_data);
    /* By now ctrl has already reloaded data from tab_ptr for the loop that
     * just started; what we write here plays one loop later. */
    if (!wave_active) return;
    if (!burst_on_loops) {
        gpio_xor_mask(1u << LOADTEST_PIN_TRIG);
        return;
    }
    gpio_put(LOADTEST_PIN_TRIG, burst_queued);
    if (burst_left == 0) {
        burst_state = !burst_state;
        burst_left = burst_state ? burst_on_loops : burst_off_loops;
    }
    burst_left--;
    burst_queued = burst_state;
    tab_ptr = burst_state ? wave_tab : silence_tab;
}

static void audio_stop(void) {
    if (dma_data < 0) return;
    hw_clear_bits(&dma_hw->ch[dma_ctrl].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[dma_data].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort((uint)dma_ctrl);
    dma_channel_abort((uint)dma_data);
    dma_channel_acknowledge_irq0((uint)dma_data);
}

static void audio_start(void) {
    dma_channel_config c = dma_channel_get_default_config((uint)dma_data);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pwm_get_dreq(audio_slice));
    channel_config_set_chain_to(&c, (uint)dma_ctrl);
    dma_channel_configure((uint)dma_data, &c, &pwm_hw->slice[audio_slice].cc, tab_ptr, audio_len, false);

    dma_channel_config k = dma_channel_get_default_config((uint)dma_ctrl);
    channel_config_set_transfer_data_size(&k, DMA_SIZE_32);
    channel_config_set_read_increment(&k, false);
    channel_config_set_write_increment(&k, false);
    dma_channel_configure((uint)dma_ctrl, &k, &dma_hw->ch[dma_data].al3_read_addr_trig, &tab_ptr, 1, false);

    dma_channel_start((uint)dma_data);
}

/* Returns the frequency actually produced (AUDIO_RATE / table length). */
static uint32_t audio_set(unsigned wave, unsigned freq, unsigned level, unsigned on_ms, unsigned off_ms) {
    audio_stop();
    if (freq < AUDIO_RATE / AUDIO_MAXLEN + 1) freq = AUDIO_RATE / AUDIO_MAXLEN + 1;
    if (freq > AUDIO_RATE / 8) freq = AUDIO_RATE / 8;
    if (level > 100) level = 100;
    audio_len = AUDIO_RATE / freq;
    uint32_t mid = (audio_top + 1) / 2;
    float amp = (float)mid * (float)level / 100.0f;
    for (uint32_t i = 0; i < audio_len; i++) {
        float s = 0.0f;
        if (wave == WAVE_SINE) s = sinf(6.2831853f * (float)i / (float)audio_len);
        else if (wave == WAVE_SQUARE) s = i < audio_len / 2 ? 1.0f : -1.0f;
        wave_tab[i] = cc_word((uint32_t)lroundf((float)mid + amp * s));
        silence_tab[i] = cc_word(mid);
    }
    uint32_t period_us = 1000000u / (AUDIO_RATE / audio_len);
    burst_on_loops = on_ms ? (on_ms * 1000u + period_us / 2) / period_us : 0;
    burst_off_loops = on_ms ? (off_ms * 1000u + period_us / 2) / period_us : 0;
    if (on_ms && !burst_on_loops) burst_on_loops = 1;
    if (on_ms && !burst_off_loops) burst_off_loops = 1;
    burst_state = false;
    burst_queued = false;
    burst_left = burst_off_loops;
    wave_active = wave != WAVE_NONE;
    tab_ptr = (wave_active && !burst_on_loops) ? wave_tab : silence_tab;
    gpio_put(LOADTEST_PIN_TRIG, 0);
    audio_start();
    return AUDIO_RATE / audio_len;
}

static void audio_init(void) {
    audio_slice = pwm_gpio_to_slice_num(AUDIO_PIN_PWM);
    audio_top = clock_get_hz(clk_sys) / AUDIO_RATE - 1;
    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, audio_top);
    pwm_init(audio_slice, &c, true);
    pwm_set_gpio_level(AUDIO_PIN_PWM, (audio_top + 1) / 2);
    gpio_set_function(AUDIO_PIN_PWM, GPIO_FUNC_PWM);

    dma_data = dma_claim_unused_channel(true);
    dma_ctrl = dma_claim_unused_channel(true);
    irq_set_exclusive_handler(DMA_IRQ_0, audio_irq);
    dma_channel_set_irq0_enabled((uint)dma_data, true);
    irq_set_enabled(DMA_IRQ_0, true);
    audio_set(WAVE_NONE, 1000, 0, 0, 0);
}
#else
static void audio_init(void) {}
static uint32_t audio_set(unsigned wave, unsigned freq, unsigned level, unsigned on_ms, unsigned off_ms) {
    (void)wave; (void)level; (void)on_ms; (void)off_ms;
    return freq;
}
#endif

static void amp_enable(bool on) {
    if (AMP_PIN_SD >= 0) gpio_put(AMP_PIN_SD, on);
}

/* ---- core1 load --------------------------------------------------------- */

static volatile bool core1_busy;
static volatile float core1_sink;

static void core1_main(void) {
    float x = 1.0f;
    for (;;) {
        if (!core1_busy) {
            __wfe();
            continue;
        }
        for (int i = 0; i < 20000; i++) x = x * 1.0000001f + 1e-7f;
        core1_sink = x;
    }
}

/* ---- LCD ---------------------------------------------------------------- */

static uint8_t strip_buf[2][LCD_WIDTH * LCD_STRIP_ROWS * 2] __attribute__((aligned(4)));

static const uint8_t k_font3x5[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};
#define DIGIT_X 10
#define DIGIT_Y 10
#define DIGIT_SCALE 10

static bool digit_px(int digit, int x, int y) {
    x -= DIGIT_X;
    y -= DIGIT_Y;
    if (digit < 0 || digit > 9 || x < 0 || y < 0 || x >= 3 * DIGIT_SCALE || y >= 5 * DIGIT_SCALE) return false;
    return (k_font3x5[digit][y / DIGIT_SCALE] >> (2 - x / DIGIT_SCALE)) & 1;
}

static void render_strip(uint8_t *out, int y0, int rows, bool anim, uint32_t phase) {
    for (int y = y0; y < y0 + rows; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) {
            uint16_t c = 0;
            if (digit_px(cur_stage, x, y)) {
                c = 0xFFFF;
            } else if (anim) {
                uint32_t r = ((uint32_t)x + phase) & 31, g = ((uint32_t)y * 2 + phase) & 63,
                         b = ((uint32_t)(x ^ y) + phase * 2) & 31;
                c = (uint16_t)(r << 11 | g << 5 | b);
            }
            *out++ = (uint8_t)(c >> 8);
            *out++ = (uint8_t)c;
        }
    }
}

static void draw_frame(bool anim, uint32_t phase) {
    int buf = 0;
    lcd_begin_frame();
    for (int y = 0; y < LCD_HEIGHT; y += LCD_STRIP_ROWS) {
        int rows = LCD_HEIGHT - y < LCD_STRIP_ROWS ? LCD_HEIGHT - y : LCD_STRIP_ROWS;
        render_strip(strip_buf[buf], y, rows, anim, phase);
        lcd_wait();
        lcd_push_async(strip_buf[buf], (size_t)rows * LCD_WIDTH * 2);
        buf ^= 1;
    }
    lcd_end_frame();
}

/* ---- die temperature (board-temperature proxy for the soak test) -------- */
/* The ADC reference is 3V3 itself, so a sagging rail skews the absolute
 * reading by about 4 C per 1% of sag. "vref" corrects it from a meter
 * reading; the rise within one stage is valid either way. */

static float adc_vref = 3.3f;
static float stage_temp0;
static uint32_t stage_ms0, log_s, next_log_ms;

static float die_temp_c(void) {
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);
    uint32_t sum = 0;
    for (int i = 0; i < 64; i++) sum += adc_read();
    float v = (float)sum / 64.0f * adc_vref / 4096.0f;
    return 27.0f - (v - 0.706f) / 0.001721f;
}

static void print_temp(void) {
    float t = die_temp_c();
    printf("[%8.1f s] stage %d  die %.1f C  (%+.1f C over %lu s in this stage, vref %.3f V)\n", now_ms() / 1000.0,
           cur_stage, t, t - stage_temp0, (unsigned long)((now_ms() - stage_ms0) / 1000), adc_vref);
}

/* ---- applying a load ---------------------------------------------------- */

static void print_load(void) {
    printf("  bl %u%%, lcd %s, core1 %s, amp %s, wave %s %u Hz %u%%", cur.bl,
           cur.lcd == LCD_STREAM ? "stream" : "static", cur.core1 ? "busy" : "idle", cur.amp ? "on" : "off",
           k_wave_name[cur.wave], cur.freq, cur.level);
    if (cur.on_ms) printf(", bursts %u/%u ms", cur.on_ms, cur.off_ms);
    printf("\n");
}

static void apply(void) {
    backlight_set(cur.bl);
    core1_busy = cur.core1;
    __sev();
    amp_enable(cur.amp);
    cur.freq = (uint16_t)audio_set(cur.wave, cur.freq, cur.level, cur.on_ms, cur.off_ms);
    lcd_dirty = true;
}

static void enter_stage(int n) {
    cur_stage = (n % NSTAGES + NSTAGES) % NSTAGES;
    cur = k_stages[cur_stage];
    stage_temp0 = die_temp_c();
    stage_ms0 = now_ms();
    apply();
    printf("[%8.1f s] stage %d: %s\n", now_ms() / 1000.0, cur_stage, cur.name);
    print_load();
}

/* ---- reset cause -------------------------------------------------------- */

#define SCRATCH_MAGIC 0x4C4F4144u /* "LOAD" */

static void print_reset_cause(void) {
    uint32_t r = powman_hw->chip_reset;
    printf("last reset:");
    if (r & POWMAN_CHIP_RESET_HAD_POR_BITS) printf(" power-on");
    if (r & POWMAN_CHIP_RESET_HAD_BOR_BITS) printf(" BROWN-OUT");
    if (r & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) printf(" RUN-pin");
    if (r & POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS) printf(" GLITCH-DETECT");
    if (r & (POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS | POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS |
             POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS |
             POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS))
        printf(" watchdog");
    if (r & POWMAN_CHIP_RESET_HAD_RESCUE_BITS) printf(" rescue");
    printf(" (chip_reset 0x%08lx)\n", (unsigned long)r);
    if (watchdog_enable_caused_reboot() && watchdog_hw->scratch[0] == SCRATCH_MAGIC)
        printf("*** WATCHDOG TIMEOUT: board hung in stage %lu after %lu s ***\n",
               (unsigned long)watchdog_hw->scratch[1], (unsigned long)watchdog_hw->scratch[2] / 1000);
}

/* ---- console / buttons -------------------------------------------------- */

static void help(void) {
    printf("stages:\n");
    for (int i = 0; i < NSTAGES; i++) printf("  %d  %s\n", i, k_stages[i].name);
    printf("commands:\n"
           "  stage N | next | prev      go to a stage (holds there)\n"
           "  auto [SECONDS]             run all stages in order, default %u s each; \"hold\" stops\n"
           "  bl PCT                     backlight duty\n"
           "  lcd static|stream\n"
           "  cpu 0|1                    core1 busy loop\n"
           "  amp 0|1                    amp shutdown pin\n"
           "  tone sine|square|none FREQ PCT\n"
           "  burst ON_MS OFF_MS         0 0 = continuous\n"
           "  temp | log SECONDS        die temperature now / every N s (0 = off)\n"
           "  vref VOLTS                measured 3V3, to correct the temperature reading\n"
           "  status\n"
           "buttons: right next stage, center previous stage, up/down audio level\n",
           DWELL_S_DEF);
}

static void console_exec(char *line) {
    char *cmd = strtok(line, " \t");
    if (!cmd) return;
    char *a1 = strtok(NULL, " \t"), *a2 = strtok(NULL, " \t"), *a3 = strtok(NULL, " \t");
    if (!strcmp(cmd, "stage") && a1) {
        auto_on = false;
        enter_stage(atoi(a1));
        return;
    } else if (!strcmp(cmd, "next") || !strcmp(cmd, "prev")) {
        auto_on = false;
        enter_stage(cur_stage + (cmd[0] == 'n' ? 1 : -1));
        return;
    } else if (!strcmp(cmd, "auto")) {
        dwell_s = a1 && atoi(a1) > 0 ? (uint32_t)atoi(a1) : DWELL_S_DEF;
        auto_on = true;
        printf("auto: %d stages, %lu s each\n", NSTAGES, (unsigned long)dwell_s);
        enter_stage(0);
        next_auto_ms = now_ms() + dwell_s * 1000u;
        return;
    } else if (!strcmp(cmd, "hold")) {
        auto_on = false;
        printf("holding stage %d\n", cur_stage);
        return;
    } else if (!strcmp(cmd, "status")) {
        printf("[%8.1f s] stage %d%s, clk_sys %lu MHz\n", now_ms() / 1000.0, cur_stage, auto_on ? " (auto)" : "",
               (unsigned long)(clock_get_hz(clk_sys) / 1000000));
        print_load();
        print_temp();
        print_reset_cause();
        return;
    } else if (!strcmp(cmd, "temp")) {
        print_temp();
        return;
    } else if (!strcmp(cmd, "log") && a1) {
        log_s = atoi(a1) > 0 ? (uint32_t)atoi(a1) : 0;
        next_log_ms = now_ms();
        if (log_s) printf("logging every %lu s\n", (unsigned long)log_s);
        else printf("logging off\n");
        return;
    } else if (!strcmp(cmd, "vref") && a1) {
        float v = strtof(a1, NULL);
        if (v > 2.5f && v < 3.6f) adc_vref = v;
        printf("vref %.3f V\n", adc_vref);
        return;
    } else if (!strcmp(cmd, "bl") && a1) {
        int v = atoi(a1);
        cur.bl = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    } else if (!strcmp(cmd, "lcd") && a1) {
        cur.lcd = !strcmp(a1, "stream") ? LCD_STREAM : LCD_STATIC;
    } else if (!strcmp(cmd, "cpu") && a1) {
        cur.core1 = atoi(a1) != 0;
    } else if (!strcmp(cmd, "amp") && a1) {
        cur.amp = atoi(a1) != 0;
    } else if (!strcmp(cmd, "tone") && a1) {
        cur.wave = !strcmp(a1, "sine") ? WAVE_SINE : !strcmp(a1, "square") ? WAVE_SQUARE : WAVE_NONE;
        if (a2) cur.freq = (uint16_t)atoi(a2);
        if (a3) cur.level = (uint8_t)(atoi(a3) > 100 ? 100 : atoi(a3) < 0 ? 0 : atoi(a3));
    } else if (!strcmp(cmd, "burst") && a1 && a2) {
        cur.on_ms = (uint16_t)atoi(a1);
        cur.off_ms = (uint16_t)atoi(a2);
    } else {
        help();
        return;
    }
    auto_on = false;
    apply();
    printf("ok (custom, based on stage %d)\n", cur_stage);
    print_load();
}

static void console_poll(void) {
    static char line[64];
    static unsigned len;
    for (;;) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) return;
        if (c == '\r' || c == '\n') {
            line[len] = '\0';
            if (len) console_exec(line);
            len = 0;
        } else if (len + 1 < sizeof line) {
            line[len++] = (char)c;
        }
    }
}

/* Only four buttons are needed (the breadboard has no left button). */
enum { B_NEXT, B_PREV, B_UP, B_DOWN };
static const int k_btn_pins[] = {
    [B_NEXT] = BTN_PIN_RIGHT, [B_PREV] = BTN_PIN_A, [B_UP] = BTN_PIN_UP, [B_DOWN] = BTN_PIN_DOWN,
};
#define NBTN ((int)(sizeof k_btn_pins / sizeof k_btn_pins[0]))

static void buttons_init(void) {
    for (int i = 0; i < NBTN; i++) {
        if (k_btn_pins[i] < 0) continue;
        gpio_init((uint)k_btn_pins[i]);
        gpio_set_dir((uint)k_btn_pins[i], GPIO_IN);
        gpio_pull_up((uint)k_btn_pins[i]);
    }
}

static void buttons_poll(void) {
    static uint32_t prev, last_ms;
    uint32_t t = now_ms();
    if (t - last_ms < 20) return; /* debounce */
    last_ms = t;
    uint32_t in = 0;
    for (int i = 0; i < NBTN; i++)
        if (k_btn_pins[i] >= 0 && !gpio_get((uint)k_btn_pins[i])) in |= 1u << i;
    uint32_t pressed = in & ~prev;
    prev = in;
    if (pressed & (1u << B_NEXT)) { auto_on = false; enter_stage(cur_stage + 1); }
    if (pressed & (1u << B_PREV)) { auto_on = false; enter_stage(cur_stage - 1); }
    if (pressed & (1u << B_UP | 1u << B_DOWN)) {
        int v = cur.level + ((pressed & (1u << B_UP)) ? 10 : -10);
        cur.level = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
        apply();
        printf("audio level %u%%\n", cur.level);
    }
}

/* ---- main --------------------------------------------------------------- */

int main(void) {
    stdio_init_all();
    if (AMP_PIN_SD >= 0) {
        gpio_init(AMP_PIN_SD);
        gpio_set_dir(AMP_PIN_SD, GPIO_OUT);
        gpio_put(AMP_PIN_SD, 0);
    }
    gpio_init(LOADTEST_PIN_TRIG);
    gpio_set_dir(LOADTEST_PIN_TRIG, GPIO_OUT);
    gpio_put(LOADTEST_PIN_TRIG, 0);
    buttons_init();
    adc_init();
    adc_set_temp_sensor_enabled(true);
    lcd_init();
    backlight_init();
    audio_init();
    multicore_launch_core1(core1_main);

    /* Boot splash: 2 s of moving pattern with the backlight on and a short
     * quiet beep, so a dead display or amp is obvious before stage 0
     * (backlight off, amp off) starts. Also gives a USB terminal time to
     * attach so the banner isn't lost. */
    cur_stage = -1; /* no digit */
    backlight_set(100);
    amp_enable(true);
    audio_set(WAVE_SINE, 1000, 25, 0, 0);
    bool beeping = true;
    for (uint32_t t0 = now_ms(), ph = 0; now_ms() - t0 < 2000;) {
        draw_frame(true, ph++);
        if (beeping && now_ms() - t0 >= 200) {
            audio_set(WAVE_NONE, 1000, 0, 0, 0);
            amp_enable(false);
            beeping = false;
        }
    }
    printf("\nmicrobiome 3V3 load test, clk_sys %lu MHz, trigger on GP%d\n",
           (unsigned long)(clock_get_hz(clk_sys) / 1000000), LOADTEST_PIN_TRIG);
    print_reset_cause();
    help();

    watchdog_hw->scratch[0] = SCRATCH_MAGIC;
    watchdog_enable(3000, true);
    enter_stage(0);

    uint32_t phase = 0;
    for (;;) {
        watchdog_update();
        watchdog_hw->scratch[1] = (uint32_t)cur_stage;
        watchdog_hw->scratch[2] = now_ms();
        console_poll();
        buttons_poll();
        if (log_s && (int32_t)(now_ms() - next_log_ms) >= 0) {
            print_temp();
            next_log_ms += log_s * 1000u;
        }
        if (auto_on && (int32_t)(now_ms() - next_auto_ms) >= 0) {
            if (cur_stage + 1 >= NSTAGES) {
                auto_on = false;
                printf("[%8.1f s] auto done, holding stage %d\n", now_ms() / 1000.0, cur_stage);
            } else {
                enter_stage(cur_stage + 1);
                next_auto_ms += dwell_s * 1000u;
            }
        }
        if (cur.lcd == LCD_STREAM) {
            draw_frame(true, phase++);
        } else {
            if (lcd_dirty) draw_frame(false, 0);
            sleep_ms(5);
        }
        lcd_dirty = false;
    }
}

/* microbiome firmware for RP2350 + ST7789.
 *
 * Main loop: read buttons -> update engine -> stream the frame to the LCD in
 * double-buffered DMA strips (render strip N+1 while strip N is on the wire).
 *
 * USB serial console (115200, any terminal):
 *   time HH:MM          set time of day (and the RTC, if fitted)
 *   date YYYY-MM-DD     set the date
 *   speed N             time multiplier (1 = real time)
 *   set VAR VALUE       pin a world variable, e.g. "set weather precip"
 *   unset VAR           release it
 *   stats               frame timing
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "hardware/gpio.h"
#include "mb/mb.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "st7789.h"
#ifdef MB_RTC_DS3231
#include "ds3231.h"
#endif

extern const uint8_t mb_pack_start[], mb_pack_end[];

static mb_engine_t eng;
static uint8_t strip_buf[2][LCD_WIDTH * LCD_STRIP_ROWS * 2] __attribute__((aligned(4)));

/* ---- host services ------------------------------------------------------ */

#ifdef MB_RTC_DS3231
static bool host_wall_time(void *ctx, mb_datetime_t *out) {
    (void)ctx;
    return ds3231_read(out);
}
#endif

/* ---- buttons ------------------------------------------------------------ */

static const struct { int pin; uint32_t bit; } k_buttons[] = {
    {BTN_PIN_LEFT, MB_IN_LEFT}, {BTN_PIN_RIGHT, MB_IN_RIGHT}, {BTN_PIN_UP, MB_IN_UP},
    {BTN_PIN_DOWN, MB_IN_DOWN}, {BTN_PIN_A, MB_IN_A},         {BTN_PIN_B, MB_IN_B},
};

static void buttons_init(void) {
    for (unsigned i = 0; i < sizeof k_buttons / sizeof k_buttons[0]; i++) {
        if (k_buttons[i].pin < 0) continue;
        gpio_init((uint)k_buttons[i].pin);
        gpio_set_dir((uint)k_buttons[i].pin, GPIO_IN);
        gpio_pull_up((uint)k_buttons[i].pin);
    }
}

static uint32_t buttons_read(void) {
    uint32_t in = 0;
    for (unsigned i = 0; i < sizeof k_buttons / sizeof k_buttons[0]; i++)
        if (k_buttons[i].pin >= 0 && !gpio_get((uint)k_buttons[i].pin)) in |= k_buttons[i].bit;
    return in;
}

/* ---- serial console ----------------------------------------------------- */

typedef struct {
    uint32_t frames, update_us, render_us, frame_us;
} stats_t;
static stats_t st;

static void console_exec(char *line) {
    char *cmd = strtok(line, " \t");
    if (!cmd) return;
    char *a1 = strtok(NULL, " \t"), *a2 = strtok(NULL, " \t");
    unsigned h, m, y, mo, d;
    if (!strcmp(cmd, "time") && a1 && sscanf(a1, "%u:%u", &h, &m) == 2 && h < 24 && m < 60) {
        mb_set_day_minute(&eng, h * 60 + m);
#ifdef MB_RTC_DS3231
        mb_datetime_t t;
        if (ds3231_read(&t)) {
            t.hour = (uint8_t)h; t.minute = (uint8_t)m; t.second = 0;
            if (ds3231_write(&t)) mb_clock_use_wall(&eng);
        }
#endif
        printf("ok %02u:%02u\n", h, m);
    } else if (!strcmp(cmd, "date") && a1 && sscanf(a1, "%u-%u-%u", &y, &mo, &d) == 3) {
        mb_set_date(&eng, (uint16_t)y, (uint8_t)mo, (uint8_t)d);
#ifdef MB_RTC_DS3231
        mb_datetime_t t;
        if (ds3231_read(&t)) {
            t.year = (uint16_t)y; t.month = (uint8_t)mo; t.day = (uint8_t)d;
            if (ds3231_write(&t)) mb_clock_use_wall(&eng);
        }
#endif
        printf("ok %04u-%02u-%02u\n", y, mo, d);
    } else if (!strcmp(cmd, "speed") && a1) {
        mb_set_time_scale(&eng, (uint32_t)atoi(a1));
        printf("ok\n");
    } else if ((!strcmp(cmd, "set") && a1 && a2) || (!strcmp(cmd, "unset") && a1)) {
        int idx = mb_var_find(&eng, a1);
        if (idx < 0) { printf("no variable %s\n", a1); return; }
        if (!strcmp(cmd, "unset")) { mb_var_unpin(&eng, idx); printf("ok\n"); return; }
        char lbl[32];
        for (int i = 0; i < mb_var_enum_count(&eng, idx); i++) {
            mb_var_pin(&eng, idx, i);
            if (!strcmp(mb_var_label(&eng, idx, lbl, sizeof lbl), a2)) { printf("ok\n"); return; }
        }
        mb_var_pin(&eng, idx, atoi(a2));
        printf("ok\n");
    } else if (!strcmp(cmd, "stats")) {
        uint32_t n = st.frames ? st.frames : 1;
        printf("%lu frames: update %lu us, render+send %lu us, frame %lu us (%.1f fps), %d actors, %02lu:%02lu\n",
               (unsigned long)st.frames, (unsigned long)(st.update_us / n), (unsigned long)(st.render_us / n),
               (unsigned long)(st.frame_us / n), 1e6 / (double)(st.frame_us / n + 1), mb_actor_count(&eng),
               (unsigned long)(mb_day_minute(&eng) / 60), (unsigned long)(mb_day_minute(&eng) % 60));
        memset(&st, 0, sizeof st);
    } else {
        printf("commands: time HH:MM | date YYYY-MM-DD | speed N | set VAR VALUE | unset VAR | stats\n");
    }
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

/* ---- main --------------------------------------------------------------- */

static void render_frame(void) {
    int buf = 0;
    lcd_begin_frame();
    for (int y = 0; y < LCD_HEIGHT; y += LCD_STRIP_ROWS) {
        int rows = LCD_HEIGHT - y < LCD_STRIP_ROWS ? LCD_HEIGHT - y : LCD_STRIP_ROWS;
        /* This buffer was last sent two strips ago; the DMA on the other buffer
         * may still be running, so render first and only then wait. */
        mb_engine_render_strip(&eng, y, rows, strip_buf[buf], LCD_WIDTH * 2);
        lcd_wait();
        lcd_push_async(strip_buf[buf], (size_t)rows * LCD_WIDTH * 2);
        buf ^= 1;
    }
    lcd_end_frame();
}

int main(void) {
    stdio_init_all();
    buttons_init();
    lcd_init();

    mb_config_t cfg = {
        .width = LCD_WIDTH, .height = LCD_HEIGHT, .format = MB_PIXFMT_RGB565_BE,
        .seed = get_rand_32(), .warmup_s = 90,
    };
#ifdef MB_RTC_DS3231
    ds3231_init();
    cfg.host.wall_time = host_wall_time;
#endif

    mb_err_t err = mb_engine_init(&eng, mb_pack_start, (size_t)(mb_pack_end - mb_pack_start), &cfg);
    if (err) {
        lcd_fill(0xF800); /* red: bad pack */
        for (;;) {
            printf("microbiome: pack error: %s\n", mb_err_str(err));
            sleep_ms(2000);
        }
    }

    uint32_t last = to_ms_since_boot(get_absolute_time());
    for (;;) {
        uint64_t t0 = time_us_64();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        console_poll();
        mb_engine_update(&eng, now - last, buttons_read());
        last = now;
        uint64_t t1 = time_us_64();
        render_frame();
        uint64_t t2 = time_us_64();

        uint32_t frame_us = (uint32_t)(t2 - t0);
        if (frame_us < TARGET_FRAME_MS * 1000u) sleep_us(TARGET_FRAME_MS * 1000u - frame_us);
        st.frames++;
        st.update_us += (uint32_t)(t1 - t0);
        st.render_us += (uint32_t)(t2 - t1);
        st.frame_us += (uint32_t)(time_us_64() - t0);
    }
}

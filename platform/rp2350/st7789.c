/* ST7789 / ILI9341 driver: SPI command interface + DMA pixel streaming.
 * The two controllers share the commands used here (CASET/RASET/RAMWR,
 * MADCTL, COLMOD); the ILI9341 additionally wants its power and gamma
 * registers set at boot. */
#include "st7789.h"

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* Each GPIO's SPI role is fixed: pin % 4 is RX, CSn, SCK, TX, and the SPI
 * instance alternates every 8 pins. A clock on an RX pin just stays silent. */
_Static_assert(LCD_PIN_SCK % 4 == 2, "LCD_PIN_SCK is not an SPI SCK pin (spi0: GP2/6/18/22, spi1: GP10/14/26)");
_Static_assert(LCD_PIN_MOSI % 4 == 3, "LCD_PIN_MOSI is not an SPI TX pin (spi0: GP3/7/19/23, spi1: GP11/15/27)");
_Static_assert(LCD_PIN_SCK / 8 % 2 == LCD_PIN_MOSI / 8 % 2, "LCD_PIN_SCK and LCD_PIN_MOSI belong to different SPI blocks");

static int dma_ch = -1;

static void cs(bool level) { gpio_put(LCD_PIN_CS, level); }
static void dc(bool data) { gpio_put(LCD_PIN_DC, data); }

static void cmd(uint8_t c, const uint8_t *args, size_t n) {
    cs(0);
    dc(0);
    spi_write_blocking(LCD_SPI, &c, 1);
    if (n) {
        dc(1);
        spi_write_blocking(LCD_SPI, args, n);
    }
    cs(1);
}

static void cmd1(uint8_t c, uint8_t a) { cmd(c, &a, 1); }

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    x0 += LCD_X_OFFSET; x1 += LCD_X_OFFSET;
    y0 += LCD_Y_OFFSET; y1 += LCD_Y_OFFSET;
    uint8_t ca[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t ra[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    cmd(0x2A, ca, 4); /* CASET */
    cmd(0x2B, ra, 4); /* RASET */
}

void lcd_backlight(int on) {
    if (LCD_PIN_BL >= 0) gpio_put(LCD_PIN_BL, on ? 1 : 0);
}

void lcd_init(void) {
    spi_init(LCD_SPI, LCD_SPI_HZ);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);

    const int outs[] = {LCD_PIN_CS, LCD_PIN_DC, LCD_PIN_RST, LCD_PIN_BL};
    for (unsigned i = 0; i < sizeof outs / sizeof outs[0]; i++) {
        if (outs[i] < 0) continue;
        gpio_init((uint)outs[i]);
        gpio_set_dir((uint)outs[i], GPIO_OUT);
        gpio_put((uint)outs[i], 1);
    }
    lcd_backlight(0);

    if (LCD_PIN_RST >= 0) {
        gpio_put(LCD_PIN_RST, 0);
        sleep_ms(20);
        gpio_put(LCD_PIN_RST, 1);
        sleep_ms(120);
    }
    cmd(0x01, NULL, 0);          /* SWRESET */
    sleep_ms(150);
#if LCD_CONTROLLER == LCD_ILI9341
    /* Power, VCOM, frame rate and gamma, from LCDwiki's init for the
     * MSP3222/MSP3223 3.2" IPS ILI9341V module (ILI9341V_Init.txt).
     * Encoded as command, argument count, arguments. */
    static const uint8_t ili9341_init[] = {
        0xCF, 3, 0x00, 0xC1, 0x30,             /* power control B */
        0xED, 4, 0x64, 0x03, 0x12, 0x81,       /* power on sequence */
        0xE8, 3, 0x85, 0x00, 0x78,             /* driver timing A */
        0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02, /* power control A */
        0xF7, 1, 0x20,                         /* pump ratio */
        0xEA, 2, 0x00, 0x00,                   /* driver timing B */
        0xC0, 1, 0x13,                         /* PWCTR1 */
        0xC1, 1, 0x13,                         /* PWCTR2 */
        0xC5, 2, 0x1C, 0x35,                   /* VMCTR1 */
        0xC7, 1, 0xC8,                         /* VMCTR2 */
        0xB6, 2, 0x0A, 0xA2,                   /* DFUNCTR */
        0xF6, 2, 0x01, 0x30,                   /* interface control */
        0xB1, 2, 0x00, 0x1B,                   /* FRMCTR1: 70 Hz */
        0xF2, 1, 0x00,                         /* 3-gamma off */
        0x26, 1, 0x01,                         /* gamma curve 1 */
        0xE0, 15, 0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7,
                  0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00, /* GMCTRP1 */
        0xE1, 15, 0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58,
                  0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F, /* GMCTRN1 */
    };
    for (size_t i = 0; i < sizeof ili9341_init; i += 2u + ili9341_init[i + 1])
        cmd(ili9341_init[i], &ili9341_init[i + 2], ili9341_init[i + 1]);
#endif
    cmd(0x11, NULL, 0);          /* SLPOUT */
    sleep_ms(120);
    cmd1(0x3A, 0x55);            /* COLMOD: 16 bpp */
    cmd1(0x36, LCD_MADCTL);      /* MADCTL: rotation / mirroring */
    cmd(LCD_INVERT ? 0x21 : 0x20, NULL, 0); /* INVON / INVOFF */
    cmd(0x13, NULL, 0);          /* NORON */
    cmd(0x29, NULL, 0);          /* DISPON */
    sleep_ms(20);

    dma_ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, spi_get_dreq(LCD_SPI, true));
    dma_channel_configure((uint)dma_ch, &c, &spi_get_hw(LCD_SPI)->dr, NULL, 0, false);

    lcd_fill(0x0000);
    lcd_backlight(1);
}

void lcd_begin_frame(void) {
    set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    uint8_t c = 0x2C; /* RAMWR */
    cs(0);
    dc(0);
    spi_write_blocking(LCD_SPI, &c, 1);
    dc(1);
}

void lcd_push_async(const void *buf, size_t bytes) {
    dma_channel_transfer_from_buffer_now((uint)dma_ch, buf, (uint32_t)bytes);
}

void lcd_wait(void) {
    dma_channel_wait_for_finish_blocking((uint)dma_ch);
}

void lcd_end_frame(void) {
    lcd_wait();
    while (spi_is_busy(LCD_SPI)) tight_loop_contents();
    /* TX-only DMA leaves junk in the RX FIFO and sets the overrun flag. */
    while (spi_is_readable(LCD_SPI)) (void)spi_get_hw(LCD_SPI)->dr;
    spi_get_hw(LCD_SPI)->icr = SPI_SSPICR_RORIC_BITS;
    cs(1);
}

void lcd_fill(uint16_t rgb565) {
    static uint8_t row[LCD_WIDTH * 2];
    for (int i = 0; i < LCD_WIDTH; i++) {
        row[i * 2] = (uint8_t)(rgb565 >> 8);
        row[i * 2 + 1] = (uint8_t)rgb565;
    }
    lcd_begin_frame();
    for (int y = 0; y < LCD_HEIGHT; y++) spi_write_blocking(LCD_SPI, row, sizeof row);
    lcd_end_frame();
}

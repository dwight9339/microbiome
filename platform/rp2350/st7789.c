/* ST7789 driver: SPI command interface + DMA pixel streaming. */
#include "st7789.h"

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

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

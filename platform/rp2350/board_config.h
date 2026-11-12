/* Board configuration for the RP2350 target.
 *
 * Defaults match a breadboard Pico 2 wired to a 2.0" 240x320 ST7789V module
 * (the 8-pin GND/VCC/SCL/SDA/RES/DC/CS/BLK kind). For another board, write a
 * header that #defines only what differs (see boards/) and configure with
 * -DMB_BOARD_CONFIG=<file>; everything it leaves out falls back to these.
 */
#ifndef MB_BOARD_CONFIG_H
#define MB_BOARD_CONFIG_H

#ifdef MB_BOARD_CONFIG_FILE
#include MB_BOARD_CONFIG_FILE
#endif

/* ---- LCD (SPI) ---------------------------------------------------------- */
#define LCD_ST7789     1
#define LCD_ILI9341    2
#ifndef LCD_CONTROLLER
#define LCD_CONTROLLER LCD_ST7789
#endif

/* SPI pin roles are fixed in silicon: on spi0, SCK must be GP2/6/18/22 and
 * MOSI GP3/7/19/23 (spi1: GP10/14/26 and GP11/15/27). st7789.c checks this.
 * CS, DC, RST and BL are plain GPIOs and can go anywhere. */
#ifndef LCD_SPI
#define LCD_SPI        spi0
#endif
#ifndef LCD_PIN_SCK
#define LCD_PIN_SCK    18   /* module "SCL" */
#endif
#ifndef LCD_PIN_MOSI
#define LCD_PIN_MOSI   19   /* module "SDA" */
#endif
#ifndef LCD_PIN_CS
#define LCD_PIN_CS     17
#endif
#ifndef LCD_PIN_DC
#define LCD_PIN_DC     16
#endif
#ifndef LCD_PIN_RST
#define LCD_PIN_RST    20   /* -1 if tied to the MCU reset */
#endif
#ifndef LCD_PIN_BL
#define LCD_PIN_BL     21   /* backlight enable, -1 if always on */
#endif

/* RP2350 SPI clocks are clk_peri / even divisor: at 150 MHz that's 75, 37.5,
 * 25 MHz... 75 MHz is out of the ST7789 spec but usually fine on short wires
 * and doubles the frame rate. Start at 37.5 and raise once things work. */
#ifndef LCD_SPI_HZ
#define LCD_SPI_HZ     (40 * 1000 * 1000)
#endif

/* Panel geometry after rotation. The panel is natively 240x320 portrait;
 * MADCTL 0x60 (MV|MX) turns it into 320x240 landscape. If the image is
 * mirrored or upside-down try 0x20, 0xA0 or 0xE0. */
#ifndef LCD_WIDTH
#define LCD_WIDTH      320
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT     240
#endif
#ifndef LCD_MADCTL
#define LCD_MADCTL     0x60
#endif
#ifndef LCD_X_OFFSET
#define LCD_X_OFFSET   0
#endif
#ifndef LCD_Y_OFFSET
#define LCD_Y_OFFSET   0
#endif
#ifndef LCD_INVERT
#define LCD_INVERT     1    /* most IPS ST7789 modules need colour inversion */
#endif

/* Rows per DMA strip. Two strips are double-buffered:
 * 2 * 320 * 16 * 2 bytes = 20 KiB of SRAM. */
#ifndef LCD_STRIP_ROWS
#define LCD_STRIP_ROWS 16
#endif

/* ---- buttons (active low, internal pull-ups; -1 = not fitted) ----------- */
#ifndef BTN_PIN_LEFT
#define BTN_PIN_LEFT   2
#endif
#ifndef BTN_PIN_RIGHT
#define BTN_PIN_RIGHT  3
#endif
#ifndef BTN_PIN_UP
#define BTN_PIN_UP     4
#endif
#ifndef BTN_PIN_DOWN
#define BTN_PIN_DOWN   5
#endif
#ifndef BTN_PIN_A
#define BTN_PIN_A      6
#endif
#ifndef BTN_PIN_B
#define BTN_PIN_B      7
#endif

/* ---- optional DS3231 RTC (enable with -DMB_RTC_DS3231=ON) --------------- */
#ifndef RTC_I2C
#define RTC_I2C        i2c0
#endif
#ifndef RTC_PIN_SDA
#define RTC_PIN_SDA    8
#endif
#ifndef RTC_PIN_SCL
#define RTC_PIN_SCL    9
#endif

/* ---- timing ------------------------------------------------------------- */
#ifndef TARGET_FRAME_MS
#define TARGET_FRAME_MS 33  /* ~30 fps cap; the SPI link is the real limit */
#endif

#endif

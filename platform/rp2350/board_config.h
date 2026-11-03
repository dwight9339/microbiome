/* Board configuration for the RP2350 target.
 *
 * Defaults match a breadboard Pico 2 wired to a 2.0" 240x320 ST7789V module
 * (the 8-pin GND/VCC/SCL/SDA/RES/DC/CS/BLK kind). For the custom PCB, copy
 * this file, change the pins, and point -DMB_BOARD_CONFIG=<file> at it.
 */
#ifndef MB_BOARD_CONFIG_H
#define MB_BOARD_CONFIG_H

/* ---- LCD (SPI) ---------------------------------------------------------- */
#define LCD_SPI        spi0
#define LCD_PIN_SCK    18   /* module "SCL" */
#define LCD_PIN_MOSI   19   /* module "SDA" */
#define LCD_PIN_CS     17
#define LCD_PIN_DC     16
#define LCD_PIN_RST    20   /* -1 if tied to the MCU reset */
#define LCD_PIN_BL     21   /* backlight enable, -1 if always on */

/* RP2350 SPI clocks are clk_peri / even divisor: at 150 MHz that's 75, 37.5,
 * 25 MHz... 75 MHz is out of the ST7789 spec but usually fine on short wires
 * and doubles the frame rate. Start at 37.5 and raise once things work. */
#ifndef LCD_SPI_HZ
#define LCD_SPI_HZ     (40 * 1000 * 1000)
#endif

/* Panel geometry after rotation. The panel is natively 240x320 portrait;
 * MADCTL 0x60 (MV|MX) turns it into 320x240 landscape. If the image is
 * mirrored or upside-down try 0x20, 0xA0 or 0xE0. */
#define LCD_WIDTH      320
#define LCD_HEIGHT     240
#define LCD_MADCTL     0x60
#define LCD_X_OFFSET   0
#define LCD_Y_OFFSET   0
#define LCD_INVERT     1    /* most IPS ST7789 modules need colour inversion */

/* Rows per DMA strip. Two strips are double-buffered:
 * 2 * 320 * 16 * 2 bytes = 20 KiB of SRAM. */
#define LCD_STRIP_ROWS 16

/* ---- buttons (active low, internal pull-ups; -1 = not fitted) ----------- */
#define BTN_PIN_LEFT   2
#define BTN_PIN_RIGHT  3
#define BTN_PIN_UP     4
#define BTN_PIN_DOWN   5
#define BTN_PIN_A      6
#define BTN_PIN_B      7

/* ---- optional DS3231 RTC (enable with -DMB_RTC_DS3231=ON) --------------- */
#define RTC_I2C        i2c0
#define RTC_PIN_SDA    8
#define RTC_PIN_SCL    9

/* ---- timing ------------------------------------------------------------- */
#define TARGET_FRAME_MS 33  /* ~30 fps cap; the SPI link is the real limit */

#endif

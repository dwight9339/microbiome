/* Breadboard Pico 2 + LCDwiki MSP3223: 3.2" 240x320 IPS, ILI9341V, 14-pin
 * header (VCC GND CS RST RS(DC) SDI(MOSI) SCK LED, then MISO, capacitive
 * touch and SD_CS, all unused here). https://www.lcdwiki.com/3.2inch_IPS_SPI_Module_ILI9341
 *
 *   cmake -S platform/rp2350 -B build-rp2350 -DMB_BOARD_CONFIG=boards/ili9341_32.h
 *
 * Wiring (module pin -> Pico 2):
 *   1 VCC -> VBUS (40) or 3V3 (36)    5 RS  -> GP17
 *   2 GND -> GND                      6 SDI -> GP19   (SPI0 TX)
 *   3 CS  -> GP21                     7 SCK -> GP18   (SPI0 SCK)
 *   4 RST -> GP20                     8 LED -> GP16
 * SCK and SDI are fixed by the silicon (SCK on GP18, TX on GP19); the
 * other four are plain GPIOs. The module's own regulator prefers 5 V on
 * VCC; on 3.3 V the backlight is dimmer.
 */
#define LCD_CONTROLLER LCD_ILI9341

#define LCD_PIN_CS     21
#define LCD_PIN_RST    20
#define LCD_PIN_SCK    18
#define LCD_PIN_MOSI   19
#define LCD_PIN_DC     17
#define LCD_PIN_BL     16

/* BGR and inverted, like most IPS panels (matches LCDwiki's init).
 * 0xA8 = MV|MY|BGR is 320x240 landscape; 0x68 (MV|MX|BGR) is the same
 * rotated 180 degrees. If red and blue are swapped clear the 0x08 bit. */
#define LCD_MADCTL     0xA8
#define LCD_INVERT     1

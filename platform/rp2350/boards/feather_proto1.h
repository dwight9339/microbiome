/* Adafruit Feather RP2350 + LCDwiki MSP3223 (3.2" ILI9341V IPS) + PAM8302,
 * wired as in hardware/prototype_1 (pin map taken from the schematic netlist
 * of the board as ordered, 2026-11-18).
 *
 *   cmake -S platform/rp2350 -B build-feather -DPICO_BOARD=adafruit_feather_rp2350 \
 *         -DMB_BOARD_CONFIG=boards/feather_proto1.h
 *
 *   TFT  SCK  GP2   MOSI GP3   CS GP5   DC GP9   RST GP6   LED (backlight) GP4
 *   BTN  up GP1  down GP23  left GP24  right GP20  center GP22
 *   I2C  SDA GP28  SCL GP29  (i2c0; DS3231 + 4.7k pull-ups)
 *   AMP  PWM audio GP11 -> 2x RC low-pass -> PAM8302 IN+   SD GP10 (100k pull-down)
 *
 * Feather silkscreen is not GPIO numbers: GP2/GP3 are labelled SDA/SCL.
 * Everything external (display VCC, PAM8302 VDD, DS3231) runs off the
 * Feather's 3V3 regulator. The 3V3 load-test breadboard used an earlier
 * mapping (audio GP7, SD GP11).
 */
#define LCD_CONTROLLER LCD_ILI9341

#define LCD_SPI        spi0
#define LCD_PIN_SCK    2
#define LCD_PIN_MOSI   3
#define LCD_PIN_CS     5
#define LCD_PIN_DC     9
#define LCD_PIN_RST    6
#define LCD_PIN_BL     4

#define LCD_MADCTL     0xA8
#define LCD_INVERT     1

#define BTN_PIN_UP     1
#define BTN_PIN_DOWN   23
#define BTN_PIN_LEFT   24
#define BTN_PIN_RIGHT  20
#define BTN_PIN_A      22   /* center */
#define BTN_PIN_B      (-1)

#define RTC_I2C        i2c0
#define RTC_PIN_SDA    28
#define RTC_PIN_SCL    29

#define AUDIO_PIN_PWM  11
#define AMP_PIN_SD     10

/* loadtest.c scope trigger: an unused header pin (silkscreen A0). */
#define LOADTEST_PIN_TRIG 26

/* Optional DS3231 real-time clock (battery-backed, I2C address 0x68). */
#include "ds3231.h"

#include "board_config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define DS3231_ADDR 0x68

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

void ds3231_init(void) {
    i2c_init(RTC_I2C, 100 * 1000);
    gpio_set_function(RTC_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(RTC_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(RTC_PIN_SDA);
    gpio_pull_up(RTC_PIN_SCL);
}

bool ds3231_read(mb_datetime_t *out) {
    uint8_t reg = 0, r[7];
    if (i2c_write_timeout_us(RTC_I2C, DS3231_ADDR, &reg, 1, true, 2000) != 1) return false;
    if (i2c_read_timeout_us(RTC_I2C, DS3231_ADDR, r, 7, false, 4000) != 7) return false;
    out->second = bcd2bin(r[0] & 0x7F);
    out->minute = bcd2bin(r[1] & 0x7F);
    out->hour = bcd2bin(r[2] & 0x3F); /* assumes 24h mode, which ds3231_write sets */
    out->day = bcd2bin(r[4] & 0x3F);
    out->month = bcd2bin(r[5] & 0x1F);
    out->year = (uint16_t)(2000 + bcd2bin(r[6]));
    return out->month >= 1 && out->month <= 12 && out->day >= 1;
}

bool ds3231_write(const mb_datetime_t *t) {
    uint8_t b[8] = {0x00, bin2bcd(t->second), bin2bcd(t->minute), bin2bcd(t->hour), 1,
                    bin2bcd(t->day), bin2bcd(t->month), bin2bcd((uint8_t)(t->year % 100))};
    return i2c_write_timeout_us(RTC_I2C, DS3231_ADDR, b, sizeof b, false, 4000) == (int)sizeof b;
}

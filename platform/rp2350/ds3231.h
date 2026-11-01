#ifndef MB_DS3231_H
#define MB_DS3231_H
#include <stdbool.h>

#include "mb/mb.h"

void ds3231_init(void);
bool ds3231_read(mb_datetime_t *out);
bool ds3231_write(const mb_datetime_t *t);
#endif

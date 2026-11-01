#ifndef MB_ST7789_H
#define MB_ST7789_H

#include <stddef.h>
#include <stdint.h>

void lcd_init(void);
void lcd_backlight(int on);

/* Streaming a full frame:
 *   lcd_begin_frame();
 *   for each strip: lcd_wait(); lcd_push_async(buf, bytes);
 *   lcd_end_frame();
 * lcd_push_async returns immediately (DMA); the buffer must stay untouched
 * until the next lcd_wait(). */
void lcd_begin_frame(void);
void lcd_push_async(const void *buf, size_t bytes);
void lcd_wait(void);
void lcd_end_frame(void);

/* Blocking fill, for boot / error screens. */
void lcd_fill(uint16_t rgb565);

#endif

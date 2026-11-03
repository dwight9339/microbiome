#ifndef MB_PNG_H
#define MB_PNG_H
#include <stdint.h>
int png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);
#endif

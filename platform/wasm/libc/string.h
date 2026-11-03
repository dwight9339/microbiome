/* The few <string.h> functions the core needs, for a freestanding wasm32 build
 * (no Emscripten, no wasi-libc). Implemented in libc.c. */
#ifndef MB_WASM_STRING_H
#define MB_WASM_STRING_H

#include <stddef.h>

void  *memset(void *dst, int c, size_t n);
void  *memcpy(void *restrict dst, const void *restrict src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
int    strcmp(const char *a, const char *b);
size_t strlen(const char *s);

#endif

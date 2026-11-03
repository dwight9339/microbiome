/* Minimal libc for the freestanding wasm32 build. With -mbulk-memory the
 * builtins lower to memory.fill / memory.copy instructions, so they can't
 * recurse back into these functions. */
#include <string.h>

void *memset(void *dst, int c, size_t n) {
    __builtin_memset(dst, c, n);
    return dst;
}

void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    __builtin_memcpy(dst, src, n);
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    __builtin_memmove(dst, src, n);
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q) return *p - *q;
    return 0;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

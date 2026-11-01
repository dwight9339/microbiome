/* Minimal dependency-free PNG writer (uncompressed deflate). Good enough for
 * snapshots; files are larger than necessary but any viewer opens them. */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t crc_table[256];

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    put32(hdr, len);
    for (int i = 0; i < 4; i++) hdr[4 + i] = (uint8_t)type[i];
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = crc_update(0xFFFFFFFFu, hdr + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    uint8_t c[4];
    put32(c, crc);
    fwrite(c, 1, 4, f);
}

int png_write_rgb(const char *path, const uint8_t *rgb, int w, int h) {
    crc_init();
    size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    size_t nblocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + nblocks * 5 + 4;
    uint8_t *raw = malloc(raw_len), *z = malloc(z_len);
    if (!raw || !z) { free(raw); free(z); return -1; }

    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (1 + (size_t)w * 3);
        row[0] = 0; /* filter: none */
        for (int i = 0; i < w * 3; i++) row[1 + i] = rgb[(size_t)y * w * 3 + i];
    }
    uint8_t *o = z;
    *o++ = 0x78; *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len;) {
        size_t n = raw_len - off > 65535 ? 65535 : raw_len - off;
        *o++ = (off + n == raw_len) ? 1 : 0;
        *o++ = (uint8_t)n; *o++ = (uint8_t)(n >> 8);
        *o++ = (uint8_t)~n; *o++ = (uint8_t)(~n >> 8);
        for (size_t i = 0; i < n; i++) {
            uint8_t v = raw[off + i];
            *o++ = v;
            a = (a + v) % 65521u;
            b = (b + a) % 65521u;
        }
        off += n;
    }
    put32(o, (b << 16) | a);
    o += 4;

    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -1; }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)(o - z));
    chunk(f, "IEND", NULL, 0);
    int err = ferror(f);
    fclose(f);
    free(raw);
    free(z);
    return err ? -1 : 0;
}

/* motion_test - rows inside a motion mask shift sideways, nothing else moves.
 *
 *   motion_test tests-motion.mbp
 *
 * Renders tests/packs/motion (see its pack.yaml for the layout) at 1x and
 * checks which column each pixel came from: the shift follows the row's
 * phase and the mask's weight, pixels never come from outside the mask, and
 * the wave moves on with time. */
#include <stdio.h>
#include <stdlib.h>

#include "mb/mb.h"

static mb_engine_t eng;
static uint32_t px[32 * 16];

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static int failures;

/* The background's colour for column x (see make_art.py). */
static uint32_t col(int x) { return (uint32_t)(8 * x + 4) << 16 | 0x4080u; }

/* Row y, columns x0..x1, should show the columns shift px to their left. */
static void expect_row(int y, int x0, int x1, int shift, const char *what) {
    for (int x = x0; x <= x1; x++) {
        uint32_t got = px[y * 32 + x] & 0xFFFFFFu;
        if (got != col(x - shift)) {
            failures++;
            printf("FAIL: (%d,%d) %s: got column %d, want %d\n", x, y, what, (int)(got >> 16) / 8, x - shift);
        }
    }
}

static void render(void) { mb_engine_render_strip(&eng, 0, 16, px, 32 * 4); }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: motion_test PACK\n"); return 2; }
    size_t size;
    unsigned char *pack = read_file(argv[1], &size);
    if (!pack) { fprintf(stderr, "motion_test: cannot read %s\n", argv[1]); return 2; }
    mb_err_t err = mb_engine_init(&eng, pack, size, &(mb_config_t){
        .width = 32, .height = 16, .scale = 1, .format = MB_PIXFMT_XRGB8888, .seed = 1});
    if (err) { fprintf(stderr, "motion_test: %s\n", mb_err_str(err)); return 2; }

    render(); /* t = 0 */
    for (int y = 0; y < 16; y++) {
        expect_row(y, 0, 7, 0, "left of the mask is still");
        expect_row(y, 24, 31, 0, "right of the mask is still");
    }
    expect_row(2, 8, 23, 0, "rows above the mask are still");
    expect_row(13, 8, 23, 0, "rows below the mask are still");
    expect_row(4, 8, 23, 0, "row at a zero of the wave");
    expect_row(6, 8, 9, 0, "would take from outside the mask");
    expect_row(6, 10, 15, 2, "crest, full weight");
    expect_row(6, 16, 23, 1, "crest, half weight");
    expect_row(10, 8, 15, -2, "trough, full weight");
    expect_row(10, 16, 22, -1, "trough, half weight");
    expect_row(10, 23, 23, 0, "would take from outside the mask");

    mb_engine_update(&eng, 250, 0); /* a quarter period on */
    render();
    expect_row(4, 8, 15, -2, "the wave has moved on, full weight");
    expect_row(4, 16, 22, -1, "the wave has moved on, half weight");
    expect_row(2, 0, 31, 0, "rows outside the mask are still");

    printf("motion_test: %s\n", failures ? "FAILED" : "ok");
    free(pack);
    return failures ? 1 : 0;
}

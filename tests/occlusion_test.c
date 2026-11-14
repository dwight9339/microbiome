/* occlusion_test - actors behind an occluder are hidden only under its mask.
 *
 *   occlusion_test tests-occlusion.mbp
 *
 * Renders tests/packs/occlusion (see its pack.yaml for the layout) at 1x and
 * checks individual pixels: hidden where an actor is behind the box, drawn
 * outside the mask, in front when its feet are on the base line, and not
 * hidden at all for a lane the occluder doesn't apply to. */
#include <stdio.h>
#include <stdlib.h>

#include "mb/mb.h"

static mb_engine_t eng;
static uint32_t px[64 * 40];

#define BG     0x204020u
#define BLUE   0x0000FFu
#define WHITE  0xFFFFFFu
#define YELLOW 0xFFFF00u

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

static void expect(int x, int y, uint32_t want, const char *what) {
    uint32_t got = px[y * 64 + x] & 0xFFFFFFu;
    if (got != want) {
        failures++;
        printf("FAIL: (%d,%d) %s: got #%06x, want #%06x\n", x, y, what, (unsigned)got, (unsigned)want);
    }
}

/* Every pixel of a 4x4 actor with feet (fx, fy) and anchor (2, 3). */
static void expect_box(int fx, int fy, int x0, int x1, uint32_t want, const char *what) {
    for (int y = fy - 3; y <= fy; y++)
        for (int x = fx - 2 + x0; x <= fx - 2 + x1; x++) expect(x, y, want, what);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: occlusion_test PACK\n"); return 2; }
    size_t size;
    unsigned char *pack = read_file(argv[1], &size);
    if (!pack) { fprintf(stderr, "occlusion_test: cannot read %s\n", argv[1]); return 2; }
    mb_err_t err = mb_engine_init(&eng, pack, size, &(mb_config_t){
        .width = 64, .height = 40, .scale = 1, .format = MB_PIXFMT_XRGB8888, .seed = 1, .warmup_s = 30});
    if (err) { fprintf(stderr, "occlusion_test: %s\n", mb_err_str(err)); return 2; }
    if (mb_actor_count(&eng) != 3) {
        printf("FAIL: expected 3 actors, have %d\n", mb_actor_count(&eng));
        return 1;
    }
    mb_engine_render_strip(&eng, 0, 40, px, 64 * 4);

    expect_box(20, 25, 0, 1, BLUE, "back actor outside the mask is drawn");
    expect_box(20, 25, 2, 3, BG, "back actor under the mask is hidden");
    expect_box(32, 30, 0, 3, WHITE, "feet on the base line is in front");
    expect_box(40, 25, 0, 3, YELLOW, "other lanes aren't occluded");
    expect(10, 10, BG, "background");

    printf("occlusion_test: %s\n", failures ? "FAILED" : "ok");
    free(pack);
    return failures ? 1 : 0;
}

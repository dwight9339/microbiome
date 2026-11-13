/* camera_test - the idle autopilot keeps its promises.
 *
 *   camera_test PACK [SEED [CLOCK_SCALE]]
 *
 * Runs 3 h of real time without input (by default at 8x clock: a whole day)
 * and checks that the camera only moves while gliding, never faster than the
 * pack's drift_speed, holds for MB_DRIFT_HOLD_MS between glides, doesn't
 * reverse within MB_DRIFT_REVERSE_MS, and that input hands control back. */
#include <stdio.h>
#include <stdlib.h>

#include "mb/mb.h"

static mb_engine_t eng;

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
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: camera_test PACK [SEED]\n"); return 2; }
    size_t size;
    unsigned char *pack = read_file(argv[1], &size);
    if (!pack) { fprintf(stderr, "camera_test: cannot read %s\n", argv[1]); return 2; }
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1;
    mb_err_t err = mb_engine_init(&eng, pack, size, &(mb_config_t){
        .width = 320, .height = 240, .format = MB_PIXFMT_RGB565_BE, .seed = seed, .warmup_s = 90});
    if (err) { fprintf(stderr, "camera_test: %s\n", mb_err_str(err)); return 2; }
    mb_set_time_scale(&eng, argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 0) : 8);

    const uint32_t dt = 50, total_ms = 3u * 3600u * 1000u;
    uint32_t speed = eng.world->drift_speed ? eng.world->drift_speed : MB_DRIFT_SPEED_DEFAULT;
    int32_t max_step = (int32_t)(speed * 256u / 10u * dt / 1000u) + 1; /* Q8 per update */
    uint32_t now = 0, glide_start = 0, last_end = 0, moving_ms = 0, longest = 0;
    int glides = 0, last_dir = 0;
    bool was_gliding = false;

    for (; now < total_ms; now += dt) {
        int32_t x0 = eng.cam_x, y0 = eng.cam_y;
        mb_engine_update(&eng, dt, 0);
        int32_t dx = eng.cam_x - x0, dy = eng.cam_y - y0;
        int32_t ax = abs(dx), ay = abs(dy);
        bool gliding = eng.drift_gliding;

        if (gliding && !was_gliding) {
            int dir = eng.drift_dir;
            if (glides) {
                CHECK(now - last_end >= MB_DRIFT_HOLD_MS, "glide %d starts %u s after the last ended",
                      glides + 1, (now - last_end) / 1000);
                if (dir == -last_dir)
                    CHECK(now - last_end >= MB_DRIFT_REVERSE_MS, "glide %d reverses after only %u s",
                          glides + 1, (now - last_end) / 1000);
            }
            glides++;
            glide_start = now;
            last_dir = dir;
        }
        if (!gliding && was_gliding) {
            last_end = now;
            if (now - glide_start > longest) longest = now - glide_start;
        }
        if (dx || dy) {
            moving_ms += dt;
            CHECK(gliding || was_gliding, "camera moved by %d,%d (Q8) at %u s without gliding", dx, dy, now / 1000);
            CHECK(ax <= max_step && ay <= max_step, "step %d,%d (Q8) at %u s exceeds %d", dx, dy, now / 1000, max_step);
        }
        was_gliding = gliding;
        if (failures > 10) break;
    }
    CHECK(longest <= 10u * 60u * 1000u, "a glide took %u s", longest / 1000);
    CHECK(moving_ms * 4 <= total_ms, "moving %u%% of the time", moving_ms * 100 / total_ms);

    /* Input hands control back: no autopilot movement until idle_drift again. */
    mb_engine_update(&eng, 1000, MB_IN_RIGHT);
    CHECK(!eng.drift_gliding, "input didn't cancel the glide");
    int32_t held = eng.cam_x;
    for (uint32_t t = 0; t + 1000 < eng.world->idle_drift_s * 1000u; t += dt) mb_engine_update(&eng, dt, 0);
    CHECK(eng.cam_x == held, "camera moved within idle_drift of input");

    printf("camera_test %s seed %u: %d glides, longest %u s, moving %u%% of the time\n", argv[1], seed, glides,
           longest / 1000, moving_ms * 100 / total_ms);
    free(pack);
    return failures ? 1 : 0;
}

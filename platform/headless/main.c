/* mbsnap - render a pack to PNG without any display hardware.
 *
 * Useful for reviewing art, checking a scene at a given time/weather, CI, and
 * for agents iterating on content:
 *
 *   mbsnap build/forest.mbp out.png --time 21:30 --set weather=clear --sim 120
 *   mbsnap build/forest.mbp pano.png --panorama --time 07:00
 *   mbsnap build/forest.mbp - --info
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mb/mb.h"
#include "png.h"

static mb_engine_t eng; /* large: keep off the stack */

static void usage(void) {
    fprintf(stderr,
            "usage: mbsnap PACK OUT.png [options]\n"
            "  --size WxH        display size in pixels (default 320x240)\n"
            "  --scale N         integer pixel scale (default: auto)\n"
            "  --rgb565          render via the RGB565 big-endian path the LCD uses\n"
            "  --panorama        render the whole world at 1x\n"
            "  --time HH:MM      time of day (simulated clock)\n"
            "  --set VAR=VALUE   pin a world variable (enum label or number); repeatable\n"
            "  --seed N          RNG seed (default 1)\n"
            "  --warmup S        seconds simulated during init (default 90)\n"
            "  --sim S           extra seconds to simulate before the shot (default 0)\n"
            "  --speed N         clock multiplier while simulating (default 1)\n"
            "  --center X,Y      centre the camera on world X,Y\n"
            "  --frames N        write N frames OUT-000.png.. spaced by --every\n"
            "  --every MS        ms between frames (default 250)\n"
            "  --info            print pack/world summary\n");
}

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

static int pin_var(mb_engine_t *e, const char *spec) {
    char name[64];
    const char *eq = strchr(spec, '=');
    if (!eq || (size_t)(eq - spec) >= sizeof name) return -1;
    memcpy(name, spec, (size_t)(eq - spec));
    name[eq - spec] = '\0';
    int idx = mb_var_find(e, name);
    if (idx < 0) { fprintf(stderr, "mbsnap: no variable '%s'\n", name); return -1; }
    const char *val = eq + 1;
    int n = mb_var_enum_count(e, idx);
    for (int i = 0; i < n; i++) {
        char lbl[64];
        mb_var_pin(e, idx, i);
        if (strcmp(mb_var_label(e, idx, lbl, sizeof lbl), val) == 0) return 0;
    }
    char *end;
    long v = strtol(val, &end, 10);
    if (*end) { fprintf(stderr, "mbsnap: bad value '%s' for %s\n", val, name); return -1; }
    mb_var_pin(e, idx, (int32_t)v);
    return 0;
}

static void print_info(const mb_engine_t *e) {
    printf("world %ux%u, display %ux%u at %ux -> viewport %ux%u\n", e->world->width, e->world->height,
           e->disp_w, e->disp_h, e->scale, e->view_w, e->view_h);
    printf("%u layers, %u actor types, %u spawn rules, %u particle systems, %u lighting keyframes\n",
           e->n_layers, e->n_actor_defs, e->n_spawns, e->n_psys, e->n_palettes);
    printf("time %02u:%02u  date %02u-%02u\n", mb_day_minute(e) / 60, mb_day_minute(e) % 60, e->month, e->mday);
    for (int i = 0; i < (int)e->n_vars; i++) {
        char lbl[64];
        printf("  var %-10s = %s\n", mb_var_name(e, i), mb_var_label(e, i, lbl, sizeof lbl));
    }
    for (int i = 0; i < mb_actor_count(e); i++) {
        mb_actor_info_t ai;
        if (mb_actor_info(e, i, &ai)) printf("  actor %-10s %-10s at %d,%d\n", ai.name, ai.state, ai.x, ai.y);
    }
    for (int i = 0; i < mb_spawn_count(e); i++) {
        mb_spawn_info_t si;
        if (!mb_spawn_info(e, i, &si)) continue;
        printf("  spawn %-10s %d/%d alive  %s", si.actor, si.alive, si.max_alive, si.blocked ? "blocked:" : "eligible");
        if (si.blocked & MB_SPAWN_BLOCK_COND) printf(" when(%s)", mb_var_name(e, si.fail_var));
        if (si.blocked & MB_SPAWN_BLOCK_MAX) printf(" max");
        if (si.blocked & MB_SPAWN_BLOCK_COOLDOWN) printf(" cooldown(%us)", (unsigned)((si.cooldown_ms + 999) / 1000));
        if (si.blocked & MB_SPAWN_BLOCK_CAP) printf(" max_actors");
        printf("\n");
    }
}

static int snap(mb_engine_t *e, const char *path) {
    int w = e->disp_w, h = e->disp_h;
    size_t bpp = mb_pixfmt_bytes((mb_pixfmt_t)e->format);
    uint8_t *px = malloc((size_t)w * h * 4);
    uint8_t *rgb = malloc((size_t)w * h * 3);
    if (!px || !rgb) return -1;
    for (int y = 0; y < h; y += 16) {
        int n = h - y < 16 ? h - y : 16;
        mb_engine_render_strip(e, y, n, px + (size_t)y * w * bpp, (size_t)w * bpp);
    }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint32_t r, g, b;
        if (bpp == 4) {
            uint32_t v = ((uint32_t *)px)[i];
            r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
        } else { /* RGB565 big-endian, as sent to the LCD */
            uint32_t v = ((uint32_t)px[i * 2] << 8) | px[i * 2 + 1];
            r = (v >> 11) << 3; g = ((v >> 5) & 0x3F) << 2; b = (v & 0x1F) << 3;
        }
        rgb[i * 3] = (uint8_t)r;
        rgb[i * 3 + 1] = (uint8_t)g;
        rgb[i * 3 + 2] = (uint8_t)b;
    }
    int rc = png_write_rgb(path, rgb, w, h);
    free(px);
    free(rgb);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(); return 2; }
    const char *pack_path = argv[1], *out = argv[2];
    mb_config_t cfg = {.width = 320, .height = 240, .format = MB_PIXFMT_XRGB8888, .seed = 1, .warmup_s = 90};
    const char *time_s = NULL, *center = NULL;
    const char *sets[16];
    int nsets = 0, panorama = 0, info = 0, frames = 1, every = 250;
    long sim_s = 0, speed = 1;

    for (int i = 3; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NEED() do { if (!v) { usage(); return 2; } i++; } while (0)
        if (!strcmp(a, "--size")) { NEED(); unsigned W, H; if (sscanf(v, "%ux%u", &W, &H) != 2) return 2; cfg.width = (uint16_t)W; cfg.height = (uint16_t)H; }
        else if (!strcmp(a, "--scale")) { NEED(); cfg.scale = (uint8_t)atoi(v); }
        else if (!strcmp(a, "--panorama")) panorama = 1;
        else if (!strcmp(a, "--rgb565")) cfg.format = MB_PIXFMT_RGB565_BE;
        else if (!strcmp(a, "--time")) { NEED(); time_s = v; }
        else if (!strcmp(a, "--set")) { NEED(); if (nsets < 16) sets[nsets++] = v; }
        else if (!strcmp(a, "--seed")) { NEED(); cfg.seed = (uint32_t)strtoul(v, NULL, 0); }
        else if (!strcmp(a, "--warmup")) { NEED(); cfg.warmup_s = (uint32_t)atoi(v); }
        else if (!strcmp(a, "--sim")) { NEED(); sim_s = atol(v); }
        else if (!strcmp(a, "--speed")) { NEED(); speed = atol(v); }
        else if (!strcmp(a, "--center")) { NEED(); center = v; }
        else if (!strcmp(a, "--frames")) { NEED(); frames = atoi(v); }
        else if (!strcmp(a, "--every")) { NEED(); every = atoi(v); }
        else if (!strcmp(a, "--info")) info = 1;
        else { usage(); return 2; }
    }

    size_t size;
    unsigned char *pack = read_file(pack_path, &size);
    if (!pack) { fprintf(stderr, "mbsnap: cannot read %s\n", pack_path); return 1; }

    if (panorama) {
        /* Peek the world size: bind with a dummy config first. */
        mb_err_t err = mb_engine_init(&eng, pack, size, &(mb_config_t){.width = 320, .height = 240});
        if (err) { fprintf(stderr, "mbsnap: %s\n", mb_err_str(err)); return 1; }
        cfg.width = eng.world->width;
        cfg.height = eng.world->height;
        cfg.scale = 1;
    }

    /* Time and pins apply before warmup so the warm-up population matches. */
    mb_err_t err = mb_engine_init(&eng, pack, size, &(mb_config_t){
        .width = cfg.width, .height = cfg.height, .format = cfg.format, .scale = cfg.scale, .seed = cfg.seed});
    if (err) { fprintf(stderr, "mbsnap: %s\n", mb_err_str(err)); return 1; }
    if (time_s) {
        unsigned hh, mm;
        if (sscanf(time_s, "%u:%u", &hh, &mm) != 2) { usage(); return 2; }
        mb_set_day_minute(&eng, hh * 60 + mm);
    }
    for (int i = 0; i < nsets; i++)
        if (pin_var(&eng, sets[i])) return 1;
    mb_set_time_scale(&eng, (uint32_t)speed);
    for (uint32_t t = 0; t < (cfg.warmup_s + (uint32_t)sim_s) * 10u; t++) mb_engine_update(&eng, 100, 0);

    if (center) {
        int cx, cy;
        if (sscanf(center, "%d,%d", &cx, &cy) != 2) { usage(); return 2; }
        mb_camera_center(&eng, cx, cy);
    }
    if (info) print_info(&eng);
    if (!strcmp(out, "-")) return 0;

    if (frames <= 1) {
        if (snap(&eng, out)) { fprintf(stderr, "mbsnap: cannot write %s\n", out); return 1; }
        printf("mbsnap: wrote %s (%ux%u)\n", out, eng.disp_w, eng.disp_h);
        return 0;
    }
    char base[512], path[600];
    snprintf(base, sizeof base, "%s", out);
    char *dot = strrchr(base, '.');
    if (dot && !strcmp(dot, ".png")) *dot = '\0';
    for (int f = 0; f < frames; f++) {
        snprintf(path, sizeof path, "%s-%03d.png", base, f);
        if (snap(&eng, path)) { fprintf(stderr, "mbsnap: cannot write %s\n", path); return 1; }
        for (int t = 0; t < every; t += 50) mb_engine_update(&eng, 50, 0);
    }
    printf("mbsnap: wrote %d frames %s-000.png..\n", frames, base);
    free(pack);
    return 0;
}

/* WebAssembly host: the engine core behind a small export table, driven from
 * JavaScript (js/microbiome.js wraps it; use that rather than these exports).
 *
 * Memory layout above the static data (__heap_base):
 *
 *   [ pack, copied in by JS ][ framebuffer, display w*h RGBA ][ JSON out ]
 *
 * All three are (re)placed on each load and linear memory grows as needed
 * (the JSON area doubles until the document fits, and keeps that size). The
 * engine reads the pack in place, so JS must not overwrite it (by calling
 * mbw_pack_buffer) and then keep stepping the old engine: mbw_pack_buffer
 * marks the engine dead until the next successful mbw_init.
 *
 * Strings go out as NUL-terminated UTF-8 pointers, and structured data as
 * JSON (mbw_pack_json for what is fixed per load, mbw_state_json per frame).
 */
#include <stdbool.h>
#include <stdint.h>

#include "mb/mb.h"

#define EXPORT(name) __attribute__((export_name(#name)))

extern unsigned char __heap_base;

static mb_engine_t eng;
static bool live;
static uint32_t pack_size;
static uint8_t *fb;
static uint32_t fb_end;    /* offset from heap() just past the framebuffer */
static uint16_t fb_w, fb_h;
static bool fb_565;

static uint32_t align16(uint32_t v) { return (v + 15u) & ~15u; }

static uint8_t *heap(void) { return (uint8_t *)(((uintptr_t)&__heap_base + 15u) & ~(uintptr_t)15u); }

static bool reserve(uint32_t bytes) {
    uintptr_t need = (uintptr_t)heap() + bytes;
    uintptr_t have = (uintptr_t)__builtin_wasm_memory_size(0) * 65536u;
    if (need <= have) return true;
    size_t pages = (need - have + 65535u) / 65536u;
    return __builtin_wasm_memory_grow(0, pages) != (size_t)-1;
}

/* ---- lifecycle ---------------------------------------------------------- */

/* Where JS copies a pack of `size` bytes before calling mbw_init; 0 if out of memory. */
EXPORT(mbw_pack_buffer) uint8_t *mbw_pack_buffer(uint32_t size) {
    live = false;
    pack_size = 0;
    if (!size || size > 0x40000000u || !reserve(align16(size))) return 0;
    pack_size = size;
    return heap();
}

enum { MBW_ERR_NO_PACK = 100, MBW_ERR_NO_MEMORY = 101 };

/* Returns an mb_err_t (0 = OK) or MBW_ERR_*. lcd565 renders through the
 * RGB565 path the LCD uses, so colours show the device's banding. */
EXPORT(mbw_init) int mbw_init(uint32_t w, uint32_t h, uint32_t scale, uint32_t seed, uint32_t warmup_s,
                              uint32_t lcd565) {
    live = false;
    if (!pack_size) return MBW_ERR_NO_PACK;
    if (!w || !h || w > 4096 || h > 4096 || scale > 16) return MB_ERR_DISPLAY;
    uint32_t fb_off = align16(pack_size);
    if (!reserve(fb_off + w * h * 4u)) return MBW_ERR_NO_MEMORY;
    fb = heap() + fb_off;
    fb_end = fb_off + w * h * 4u;
    fb_w = (uint16_t)w;
    fb_h = (uint16_t)h;
    fb_565 = lcd565 != 0;
    mb_config_t cfg = {
        .width = (uint16_t)w,
        .height = (uint16_t)h,
        .format = fb_565 ? MB_PIXFMT_RGB565_LE : MB_PIXFMT_XRGB8888,
        .scale = (uint8_t)scale,
        .seed = seed,
        .warmup_s = warmup_s,
    };
    mb_err_t err = mb_engine_init(&eng, heap(), pack_size, &cfg);
    live = err == MB_OK;
    return (int)err;
}

/* Drop the engine (e.g. when JS rejects a pack after init). */
EXPORT(mbw_unload) void mbw_unload(void) {
    live = false;
    pack_size = 0;
}

EXPORT(mbw_err_str) const char *mbw_err_str(int err) {
    if (err == MBW_ERR_NO_PACK) return "no pack loaded";
    if (err == MBW_ERR_NO_MEMORY) return "out of memory";
    return mb_err_str((mb_err_t)err);
}

EXPORT(mbw_update) void mbw_update(uint32_t dt_ms, uint32_t input) {
    if (live) mb_engine_update(&eng, dt_ms, input);
}

/* Simulate without moving the camera (see mb_engine_warmup). */
EXPORT(mbw_warmup) void mbw_warmup(uint32_t seconds) {
    if (live) mb_engine_warmup(&eng, seconds);
}

/* Renders the whole display; returns a pointer to w*h RGBA bytes (ImageData
 * layout), or 0 if no engine is loaded. */
EXPORT(mbw_render) uint8_t *mbw_render(void) {
    if (!live) return 0;
    uint32_t n = (uint32_t)fb_w * fb_h;
    uint32_t *px = (uint32_t *)fb;
    if (fb_565) {
        /* 2 bytes in, 4 out: expand back to front so nothing is overwritten
         * before it is read. Same 565 -> 888 expansion as mbsnap --rgb565.
         * Bytes only: reading as uint16_t while writing as uint32_t would
         * break strict aliasing, and the optimiser does reorder it. */
        mb_engine_render_strip(&eng, 0, fb_h, fb, (size_t)fb_w * 2u);
        for (uint32_t i = n; i-- > 0;) {
            uint32_t v = (uint32_t)fb[i * 2] | ((uint32_t)fb[i * 2 + 1] << 8);
            uint8_t *o = fb + i * 4;
            o[3] = 255;
            o[2] = (uint8_t)((v & 0x1Fu) << 3);
            o[1] = (uint8_t)(((v >> 5) & 0x3Fu) << 2);
            o[0] = (uint8_t)((v >> 11) << 3);
        }
    } else {
        mb_engine_render_strip(&eng, 0, fb_h, fb, (size_t)fb_w * 4u);
        for (uint32_t i = 0; i < n; i++) { /* 0x00RRGGBB -> bytes R,G,B,255 */
            uint32_t v = px[i];
            px[i] = 0xFF000000u | ((v & 0xFFu) << 16) | (v & 0xFF00u) | ((v >> 16) & 0xFFu);
        }
    }
    return fb;
}

/* ---- control ------------------------------------------------------------ */

EXPORT(mbw_set_minute) void mbw_set_minute(uint32_t minute) {
    if (live) mb_set_day_minute(&eng, minute % 1440u);
}
EXPORT(mbw_set_date) void mbw_set_date(uint32_t year, uint32_t month, uint32_t day) {
    if (live && month >= 1 && month <= 12 && day >= 1 && day <= 31)
        mb_set_date(&eng, (uint16_t)year, (uint8_t)month, (uint8_t)day);
}
EXPORT(mbw_set_time_scale) void mbw_set_time_scale(uint32_t scale) {
    if (live) mb_set_time_scale(&eng, scale);
}
EXPORT(mbw_var_pin) void mbw_var_pin(int idx, int32_t value) {
    if (live) mb_var_pin(&eng, idx, value);
}
EXPORT(mbw_var_unpin) void mbw_var_unpin(int idx) {
    if (live) mb_var_unpin(&eng, idx);
}
EXPORT(mbw_camera_center) void mbw_camera_center(int x, int y) {
    if (live) mb_camera_center(&eng, x, y);
}

/* ---- JSON --------------------------------------------------------------- */

static char *json;
static uint32_t jn, jcap;
static bool joverflow;

static void jraw(const char *s) {
    while (*s) {
        if (jn + 1 >= jcap) { joverflow = true; return; }
        json[jn++] = *s++;
    }
}

static void jstr(const char *s) {
    static const char hex[] = "0123456789abcdef";
    jraw("\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char esc[7] = {0};
        if (c == '"' || c == '\\') { esc[0] = '\\'; esc[1] = (char)c; }
        else if (c < 0x20) {
            esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
            esc[4] = hex[c >> 4]; esc[5] = hex[c & 15];
        } else esc[0] = (char)c;
        jraw(esc);
    }
    jraw("\"");
}

static void jint(int64_t v) {
    char tmp[24];
    int n = 0;
    uint64_t u = v < 0 ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    do { tmp[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    char out[26];
    int o = 0;
    if (v < 0) out[o++] = '-';
    while (n) out[o++] = tmp[--n];
    out[o] = '\0';
    jraw(out);
}

static void jkey(const char *k) { jstr(k); jraw(":"); }
static void jbool(bool b) { jraw(b ? "true" : "false"); }
static void jsep(int i) { if (i) jraw(","); }

/* Enum value labels, which packc caps at 255 bytes (MAX_LABEL). */
typedef char label_buf[256];

/* Runs a writer into the JSON area above the framebuffer, doubling the area
 * until the document fits. Returns 0 only if memory can't grow. Names in a
 * pack have no length limit, so neither does the output. The area keeps the
 * size that last worked, so a large state isn't rewritten several times on
 * every frame. */
static uint32_t json_cap = 64u * 1024u;

static const char *json_build(void (*write)(void)) {
    if (!live) return "null";
    uint32_t base = align16(fb_end);
    for (uint32_t cap = json_cap; cap <= (1u << 28); cap *= 2u) {
        if (!reserve(base + cap)) return 0;
        json = (char *)heap() + base;
        jcap = cap;
        jn = 0;
        joverflow = false;
        write();
        if (!joverflow) {
            json[jn] = '\0';
            json_cap = cap;
            return json;
        }
    }
    return 0;
}

static void write_pack_json(void) {
    const mb_engine_t *e = &eng;
    label_buf lbl;
    jraw("{"); jkey("name"); jstr(mb_str(e, e->world->name_str));
    jraw(","); jkey("packVersion"); jint(MB_PACK_VERSION);
    jraw(","); jkey("packSize"); jint(pack_size);
    jraw(","); jkey("world"); jraw("["); jint(e->world->width); jraw(","); jint(e->world->height); jraw("]");
    jraw(","); jkey("display"); jraw("["); jint(e->disp_w); jraw(","); jint(e->disp_h); jraw("]");
    jraw(","); jkey("view"); jraw("["); jint(e->view_w); jraw(","); jint(e->view_h); jraw("]");
    jraw(","); jkey("scale"); jint(e->scale);
    jraw(","); jkey("lcd565"); jbool(fb_565);

    jraw(","); jkey("vars"); jraw("[");
    for (int i = 0; i < (int)e->n_vars; i++) {
        jsep(i);
        jraw("{"); jkey("name"); jstr(mb_var_name(e, i));
        jraw(","); jkey("labels"); jraw("[");
        int n = mb_var_enum_count(e, i);
        for (int k = 0; k < n; k++) { jsep(k); jstr(mb_var_value_label(e, i, k, lbl, sizeof lbl)); }
        jraw("]}");
    }
    jraw("]");

    jraw(","); jkey("actors"); jraw("[");
    for (uint32_t i = 0; i < e->n_actor_defs; i++) {
        const mb_pk_actor_t *ad = &e->actor_defs[i];
        jsep((int)i);
        jraw("{"); jkey("name"); jstr(mb_str(e, ad->name_str));
        jraw(","); jkey("states"); jraw("[");
        for (uint32_t s = 0; s < ad->state_count; s++) {
            jsep((int)s);
            jstr(mb_str(e, e->states[ad->state_first + s].name_str));
        }
        jraw("]}");
    }
    jraw("]");

    jraw(","); jkey("spawns"); jraw("[");
    for (uint32_t i = 0; i < e->n_spawns; i++) {
        jsep((int)i);
        jraw("{"); jkey("actor"); jstr(mb_str(e, e->actor_defs[e->spawns[i].actor].name_str)); jraw("}");
    }
    jraw("]");

    jraw(","); jkey("layers"); jraw("[");
    for (uint32_t i = 0; i < e->n_layers; i++) {
        const mb_pk_layer_t *l = &e->layers[i];
        const mb_pk_image_t *im = &e->images[l->image];
        jsep((int)i);
        jraw("{"); jkey("name"); jstr(mb_str(e, l->name_str));
        jraw(","); jkey("size"); jraw("["); jint(im->w); jraw(","); jint(im->h); jraw("]");
        jraw(","); jkey("front"); jbool(l->flags & MB_LAYER_FRONT);
        jraw("}");
    }
    jraw("]");

    jraw(","); jkey("lanes"); jraw("[");
    for (uint32_t i = 0; i < e->n_lanes; i++) {
        jsep((int)i);
        jraw("{"); jkey("name"); jstr(mb_str(e, e->lanes[i].name_str));
        jraw(","); jkey("y"); jraw("["); jint(e->lanes[i].y_min); jraw(","); jint(e->lanes[i].y_max); jraw("]}");
    }
    jraw("]");

    jraw(","); jkey("places"); jraw("[");
    for (uint32_t i = 0; i < e->n_places; i++) {
        const mb_pk_place_t *p = &e->places[i];
        jsep((int)i);
        jraw("{"); jkey("name"); jstr(mb_str(e, p->name_str));
        jraw(","); jkey("at"); jraw("["); jint(p->x); jraw(","); jint(p->y); jraw("]");
        jraw(","); jkey("spread"); jint(p->spread); jraw("}");
    }
    jraw("]");

    jraw(","); jkey("obstacles"); jraw("[");
    for (uint32_t i = 0; i < e->n_obstacles; i++) {
        const mb_pk_obstacle_t *o = &e->obstacles[i];
        jsep((int)i);
        jraw("{"); jkey("name"); jstr(mb_str(e, o->name_str));
        jraw(","); jkey("lane"); jint(o->lane);
        jraw(","); jkey("box"); jraw("[");
        jint(o->x0); jraw(","); jint(o->y0); jraw(","); jint(o->x1); jraw(","); jint(o->y1); jraw("]}");
    }
    jraw("]");

    jraw(","); jkey("occluders"); jraw("[");
    for (uint32_t i = 0; i < e->n_occluders; i++) {
        const mb_pk_occluder_t *o = &e->occluders[i];
        const mb_pk_image_t *m = &e->images[o->image];
        jsep((int)i);
        jraw("{"); jkey("name"); jstr(mb_str(e, o->name_str));
        jraw(","); jkey("base"); jint(o->base_y);
        jraw(","); jkey("laneMask"); jint(o->lane_mask);
        jraw(","); jkey("box"); jraw("[");
        jint(o->x); jraw(","); jint(o->y); jraw(","); jint(o->x + m->w); jraw(","); jint(o->y + m->h); jraw("]}");
    }
    jraw("]}");
}

/* Fixed per load: names, enum labels, display geometry and scene layout.
 * "null" without a pack; 0 if out of memory. */
EXPORT(mbw_pack_json) const char *mbw_pack_json(void) { return json_build(write_pack_json); }

static void write_state_json(void) {
    const mb_engine_t *e = &eng;
    label_buf lbl;
    uint16_t year;
    uint8_t month, day;
    int cx, cy, cw, ch;
    mb_get_date(e, &year, &month, &day);
    mb_camera_view(e, &cx, &cy, &cw, &ch);

    jraw("{"); jkey("minute"); jint(mb_day_minute(e));
    jraw(","); jkey("date"); jraw("["); jint(year); jraw(","); jint(month); jraw(","); jint(day); jraw("]");
    jraw(","); jkey("timeScale"); jint(e->time_scale);
    jraw(","); jkey("camera"); jraw("[");
    jint(cx); jraw(","); jint(cy); jraw(","); jint(cw); jraw(","); jint(ch); jraw("]");

    jraw(","); jkey("vars"); jraw("[");
    for (int i = 0; i < (int)e->n_vars; i++) {
        jsep(i);
        jraw("{"); jkey("value"); jint(mb_var_get(e, i));
        jraw(","); jkey("label"); jstr(mb_var_label(e, i, lbl, sizeof lbl));
        jraw(","); jkey("pinned"); jbool(mb_var_pinned(e, i)); jraw("}");
    }
    jraw("]");

    jraw(","); jkey("actors"); jraw("[");
    for (int i = 0, n = 0; i < mb_actor_count(e); i++) {
        mb_actor_info_t a;
        if (!mb_actor_info(e, i, &a)) continue;
        jsep(n++);
        jraw("{"); jkey("id"); jint(a.id);
        jraw(","); jkey("type"); jint(a.type);
        jraw(","); jkey("name"); jstr(a.name);
        jraw(","); jkey("state"); jstr(a.state);
        jraw(","); jkey("stateIndex"); jint(a.state_index);
        jraw(","); jkey("spawnRule"); jint(a.spawn_rule);
        jraw(","); jkey("x"); jint(a.x);
        jraw(","); jkey("y"); jint(a.y);
        jraw(","); jkey("target"); jraw("["); jint(a.tx); jraw(","); jint(a.ty); jraw("]");
        jraw(","); jkey("rect"); jraw("[");
        jint(a.sx); jraw(","); jint(a.sy); jraw(","); jint(a.w); jraw(","); jint(a.h); jraw("]");
        jraw(","); jkey("stateMs"); jint(a.state_ms);
        jraw(","); jkey("stateDurMs"); jint(a.state_dur_ms);
        jraw(","); jkey("ageMs"); jint(a.age_ms);
        jraw(","); jkey("leaving"); jbool(a.leaving);
        jraw(","); jkey("faceLeft"); jbool(a.face_left);
        jraw("}");
    }
    jraw("]");

    jraw(","); jkey("nextSpawnMs"); jint(mb_spawn_next_tick_ms(e));
    jraw(","); jkey("spawns"); jraw("[");
    for (int i = 0; i < mb_spawn_count(e); i++) {
        mb_spawn_info_t s;
        jsep(i);
        if (!mb_spawn_info(e, i, &s)) { jraw("null"); continue; } /* keep indices aligned with info.spawns */
        jraw("{"); jkey("blocked"); jraw("[");
        int k = 0;
        if (s.blocked & MB_SPAWN_BLOCK_COND) { jsep(k++); jstr("when"); }
        if (s.blocked & MB_SPAWN_BLOCK_MAX) { jsep(k++); jstr("max"); }
        if (s.blocked & MB_SPAWN_BLOCK_COOLDOWN) { jsep(k++); jstr("cooldown"); }
        if (s.blocked & MB_SPAWN_BLOCK_CAP) { jsep(k++); jstr("max_actors"); }
        jraw("]");
        jraw(","); jkey("failVar");
        if (s.fail_var >= 0) jstr(mb_var_name(e, s.fail_var)); else jraw("null");
        jraw(","); jkey("alive"); jint(s.alive);
        jraw(","); jkey("max"); jint(s.max_alive);
        jraw(","); jkey("cooldownMs"); jint(s.cooldown_ms);
        jraw(","); jkey("weight"); jint(s.weight);
        jraw("}");
    }
    jraw("]}");
}

/* Changes every frame: clock, camera, variables, actors, spawn eligibility.
 * "null" without a pack; 0 if out of memory. */
EXPORT(mbw_state_json) const char *mbw_state_json(void) { return json_build(write_state_json); }

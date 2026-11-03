/* Engine lifecycle, main update, camera. */
#include <string.h>

#include "internal.h"

static int32_t ceil_div(int32_t a, int32_t b) { return (a + b - 1) / b; }

static void clamp_camera(mb_engine_t *e) {
    int32_t ww = e->world->width, wh = e->world->height;
    if (ww >= e->view_w) e->cam_x = mb_clamp(e->cam_x, 0, e->cam_max_x);
    else e->cam_x = -(((int32_t)e->view_w - ww) / 2 * 256);
    if (wh >= e->view_h) e->cam_y = mb_clamp(e->cam_y, 0, e->cam_max_y);
    else e->cam_y = -(((int32_t)e->view_h - wh) / 2 * 256);
}

void mb_camera_center(mb_engine_t *e, int wx, int wy) {
    e->cam_x = (wx - e->view_w / 2) * 256;
    e->cam_y = (wy - e->view_h / 2) * 256;
    clamp_camera(e);
}

void mb_camera_view(const mb_engine_t *e, int *x, int *y, int *w, int *h) {
    *x = e->cam_x >> 8;
    *y = e->cam_y >> 8;
    *w = e->view_w;
    *h = e->view_h;
}

void mb_get_date(const mb_engine_t *e, uint16_t *year, uint8_t *month, uint8_t *day) {
    *year = e->year;
    *month = e->month;
    *day = e->mday;
}

void mb_world_to_display(const mb_engine_t *e, int wx, int wy, int *dx, int *dy) {
    *dx = (wx - (e->cam_x >> 8)) * e->scale;
    *dy = (wy - (e->cam_y >> 8)) * e->scale;
}

static int32_t resolve_parallax(int16_t q8, int32_t layer_len, int32_t world_len, int32_t view_len) {
    if (q8 != MB_PARALLAX_AUTO) return q8;
    if (world_len <= view_len) return 256;       /* camera can't move: stay world-aligned */
    if (layer_len <= view_len) return 0;         /* too small to scroll: pin to screen */
    return (layer_len - view_len) * 256 / (world_len - view_len);
}

static mb_actor_t *find_actor(mb_engine_t *e, uint16_t id) {
    for (int i = 0; i < MB_MAX_ACTORS; i++)
        if (e->actors[i].used && e->actors[i].id == id) return &e->actors[i];
    return 0;
}

/* Autopilot: after a while without input, gently follow something interesting. */
static void camera_drift(mb_engine_t *e, uint32_t dt_ms, int32_t pan) {
    mb_actor_t *a = find_actor(e, e->drift_actor_id);
    e->drift_repick_ms = e->drift_repick_ms > dt_ms ? e->drift_repick_ms - dt_ms : 0;
    if (!a || !e->drift_repick_ms || (a->flags & MB_AF_LEAVING)) {
        int n = 0;
        a = 0;
        for (int i = 0; i < MB_MAX_ACTORS; i++) {
            mb_actor_t *c = &e->actors[i];
            if (!c->used || (c->flags & MB_AF_LEAVING)) continue;
            if (mb_rand(e) % (uint32_t)(++n) == 0) a = c;
        }
        e->drift_actor_id = a ? a->id : 0;
        e->drift_repick_ms = (uint32_t)mb_rand_range(e, 20000, 45000);
    }
    if (a) {
        int32_t want_x = a->x - (int32_t)e->view_w * 128;
        int32_t want_y = a->y - (int32_t)e->view_h * 170; /* subject in lower third */
        int32_t cap = pan / 2 + 1;
        int32_t sx = mb_clamp((int32_t)((int64_t)(want_x - e->cam_x) * dt_ms / 2500), -cap, cap);
        int32_t sy = mb_clamp((int32_t)((int64_t)(want_y - e->cam_y) * dt_ms / 2500), -cap, cap);
        e->cam_x += sx;
        e->cam_y += sy;
    } else {
        if (!e->drift_dir) e->drift_dir = 1;
        e->cam_x += e->drift_dir * (pan / 8 + 1);
        if (e->cam_x <= 0) e->drift_dir = 1;
        if (e->cam_x >= e->cam_max_x) e->drift_dir = -1;
    }
}

static void camera_update(mb_engine_t *e, uint32_t dt_ms, uint32_t input) {
    int32_t pan = (int32_t)((uint32_t)e->world->pan_speed * 256u * dt_ms / 1000u);
    if (input & MB_IN_LEFT) e->cam_x -= pan;
    if (input & MB_IN_RIGHT) e->cam_x += pan;
    if (input & MB_IN_UP) e->cam_y -= pan;
    if (input & MB_IN_DOWN) e->cam_y += pan;
    if (input) {
        e->idle_ms = 0;
        e->drift_actor_id = 0;
    } else if (e->idle_ms < 0x7FFFFFFF) {
        e->idle_ms += dt_ms;
    }
    if (e->world->idle_drift_s && e->idle_ms >= (uint32_t)e->world->idle_drift_s * 1000u)
        camera_drift(e, dt_ms, pan);
    clamp_camera(e);
}

static void simulate(mb_engine_t *e, uint32_t dt_ms) {
    e->real_ms += dt_ms;
    e->sensor_ms += dt_ms;
    mb_clock_update(e, dt_ms);
    mb_vars_update(e, false);
    mb_palette_update(e, false);
    mb_spawn_update(e, dt_ms);
    mb_actors_update(e, dt_ms);
    mb_particles_update(e, dt_ms);
}

void mb_engine_update(mb_engine_t *e, uint32_t dt_ms, uint32_t input) {
    if (dt_ms > 250) dt_ms = 250; /* stalls (debugger, flash writes) shouldn't teleport actors */
    camera_update(e, dt_ms, input);
    simulate(e, dt_ms);
    mb_actors_sort(e);
}

mb_err_t mb_engine_init(mb_engine_t *e, const void *pack, size_t pack_size, const mb_config_t *cfg) {
    memset(e, 0, sizeof *e);
    mb_err_t err = mb_pack_bind(e, pack, pack_size);
    if (err) return err;

    if (!cfg->width || !cfg->height || cfg->format > MB_PIXFMT_XRGB8888) return MB_ERR_DISPLAY;
    e->disp_w = cfg->width;
    e->disp_h = cfg->height;
    e->format = (uint8_t)cfg->format;
    e->host = cfg->host;

    /* Integer pixel scale: the smallest that doesn't show more than the world. */
    int32_t ww = e->world->width, wh = e->world->height;
    int32_t s = cfg->scale;
    if (!s) {
        s = ceil_div(cfg->height, wh);
        int32_t sw = ceil_div(cfg->width, ww);
        if (sw > s) s = sw;
        while (cfg->width / s > MB_MAX_VIEW_W) s++;
    }
    if (s < 1 || s > 16 || cfg->width / s > MB_MAX_VIEW_W) return MB_ERR_DISPLAY;
    e->scale = (uint8_t)s;
    e->view_w = (uint16_t)(cfg->width / s);
    e->view_h = (uint16_t)(cfg->height / s);
    e->cam_max_x = ww > e->view_w ? (ww - e->view_w) * 256 : 0;
    e->cam_max_y = wh > e->view_h ? (wh - e->view_h) * 256 : 0;

    for (uint32_t i = 0; i < e->n_layers; i++) {
        const mb_pk_layer_t *l = &e->layers[i];
        const mb_pk_image_t *im = &e->images[l->image];
        e->layer_px[i] = resolve_parallax(l->parallax_x_q8, im->w, ww, e->view_w);
        e->layer_py[i] = resolve_parallax(l->parallax_y_q8, im->h, wh, e->view_h);
    }

    e->rng = cfg->seed ? cfg->seed : 0x9E3779B9u;
    e->max_actors = e->world->max_actors ? e->world->max_actors : MB_MAX_ACTORS;
    if (e->max_actors > MB_MAX_ACTORS) e->max_actors = MB_MAX_ACTORS;

    mb_camera_center(e, e->world->cam_x, e->world->cam_y);
    mb_clock_init(e);
    mb_vars_update(e, true);
    mb_palette_update(e, true);
    mb_particles_init(e);

    for (uint32_t t = 0; t < cfg->warmup_s * 10u; t++) simulate(e, 100);
    mb_actors_sort(e);
    return MB_OK;
}

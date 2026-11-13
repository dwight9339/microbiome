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
    e->drift_gliding = false;
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

/* ---- autopilot ----------------------------------------------------------
 *
 * After a while without input the camera holds a framing and, rarely, glides
 * very slowly to a clearly better one. It never follows an actor, and an
 * empty scene doesn't move: the aim is that you look back and find the view
 * has changed without ever having seen it move.
 *  - Candidate framings are a quarter view apart, at most one view away.
 *  - A framing scores 3 per resting actor and 1 per moving one whose feet are
 *    inside it, away from the edges. Actors leaving or fleeing don't count.
 *  - A glide needs a clear win (best >= current + 2 and >= 1.5x current), and
 *    can't start within MB_DRIFT_HOLD_MS of the last one ending, nor reverse
 *    its direction within MB_DRIFT_REVERSE_MS.
 *  - Glides ease in, cruise at the pack's drift_speed and ease out, and once
 *    started they finish even if the scene changes meanwhile. */
#define DRIFT_RAMP_MS 4000   /* ease in: 0 -> full speed over this */
#define DRIFT_TAIL_MS 4000   /* ease out: speed <= distance / this */
#define DRIFT_MIN_V   128    /* Q8 px/s: the end of the ease-out still arrives */

/* Score the framing whose left edge is cx (Q8); *feet_y gets the mean feet y
 * of the actors counted, or -1 if none. */
static int32_t drift_score(mb_engine_t *e, int32_t cx, int32_t *feet_y) {
    int32_t m = e->view_w / 10, x0 = (cx >> 8) + m, x1 = (cx >> 8) + e->view_w - m;
    int32_t score = 0, ysum = 0, n = 0;
    for (int i = 0; i < MB_MAX_ACTORS; i++) {
        const mb_actor_t *a = &e->actors[i];
        if (!a->used || (a->flags & MB_AF_LEAVING)) continue;
        uint8_t mv = mb_actor_state(e, a)->move;
        if (mv == MB_MOVE_EXIT || mv == MB_MOVE_FLEE) continue;
        int32_t x = a->x >> 8;
        if (x < x0 || x > x1) continue;
        score += mv == MB_MOVE_IDLE ? 3 : 1;
        ysum += a->y >> 8;
        n++;
    }
    if (feet_y) *feet_y = n ? ysum / n : -1;
    return score;
}

/* Look for a clearly better framing and start gliding to it. */
static void drift_consider(mb_engine_t *e) {
    if (e->cam_max_x <= 0) return; /* nowhere to go */
    int32_t step = (int32_t)e->view_w / 4 * 256, reach = (int32_t)e->view_w * 256;
    if (step < 256) step = 256;
    int32_t cur = drift_score(e, e->cam_x, 0);
    int32_t best = cur, best_x = e->cam_x, best_d = 0;
    for (int32_t cx = 0;; cx += step) {
        if (cx > e->cam_max_x) cx = e->cam_max_x;
        int32_t d = cx > e->cam_x ? cx - e->cam_x : e->cam_x - cx;
        if (d >= step / 2 && d <= reach) {
            int32_t s = drift_score(e, cx, 0);
            if (s > best || (s == best && best_x != e->cam_x && d < best_d)) {
                best = s;
                best_x = cx;
                best_d = d;
            }
        }
        if (cx >= e->cam_max_x) break;
    }
    if (best_x == e->cam_x || best < cur + 2 || best * 2 < cur * 3) return;
    int8_t dir = best_x > e->cam_x ? 1 : -1;
    if (dir == -e->drift_dir && e->drift_since_ms < MB_DRIFT_REVERSE_MS) return;

    int32_t fy;
    drift_score(e, best_x, &fy);
    e->drift_tx = best_x;
    /* Vertical: put the subjects' feet about two-thirds down, if there's room. */
    e->drift_ty = fy < 0 || e->cam_max_y <= 0 ? e->cam_y
                : mb_clamp(fy * 256 - (int32_t)e->view_h * 170, 0, e->cam_max_y);
    e->drift_dir = dir;
    e->drift_v = 0;
    e->drift_gliding = true;
}

static void drift_glide(mb_engine_t *e, uint32_t dt_ms) {
    int32_t dx = e->drift_tx - e->cam_x, dy = e->drift_ty - e->cam_y;
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, dist = ax > ay ? ax : ay;
    int32_t vmax = (int32_t)(e->world->drift_speed ? e->world->drift_speed : MB_DRIFT_SPEED_DEFAULT) * 256 / 10;
    int32_t v = e->drift_v + (int32_t)((int64_t)vmax * dt_ms / DRIFT_RAMP_MS);
    int32_t tail = (int32_t)((int64_t)dist * 1000 / DRIFT_TAIL_MS);
    if (v > vmax) v = vmax;
    if (v > tail) v = tail;
    if (v < DRIFT_MIN_V) v = DRIFT_MIN_V;
    e->drift_v = v;
    int32_t step = (int32_t)((int64_t)v * dt_ms / 1000);
    if (step >= dist) {
        e->cam_x = e->drift_tx;
        e->cam_y = e->drift_ty;
        e->drift_gliding = false;
        e->drift_v = 0;
        e->drift_since_ms = 0;
        e->drift_hold_ms = MB_DRIFT_HOLD_MS;
        return;
    }
    e->cam_x += (int32_t)((int64_t)dx * step / dist);
    e->cam_y += (int32_t)((int64_t)dy * step / dist);
}

static void camera_drift(mb_engine_t *e, uint32_t dt_ms) {
    e->drift_since_ms = e->drift_since_ms < 0x7FFFFFFF ? e->drift_since_ms + dt_ms : e->drift_since_ms;
    if (e->drift_gliding) {
        drift_glide(e, dt_ms);
    } else if (e->drift_hold_ms > dt_ms) {
        e->drift_hold_ms -= dt_ms;
    } else {
        e->drift_hold_ms = MB_DRIFT_EVAL_MS;
        drift_consider(e);
    }
}

static void camera_update(mb_engine_t *e, uint32_t dt_ms, uint32_t input) {
    int32_t pan = (int32_t)((uint32_t)e->world->pan_speed * 256u * dt_ms / 1000u);
    if (input & MB_IN_LEFT) e->cam_x -= pan;
    if (input & MB_IN_RIGHT) e->cam_x += pan;
    if (input & MB_IN_UP) e->cam_y -= pan;
    if (input & MB_IN_DOWN) e->cam_y += pan;
    if (input) {
        /* The viewer chose this view: start over, with no memory of past glides. */
        e->idle_ms = 0;
        e->drift_gliding = false;
        e->drift_hold_ms = 0;
        e->drift_dir = 0;
    } else if (e->idle_ms < 0x7FFFFFFF) {
        e->idle_ms += dt_ms;
    }
    if (e->world->idle_drift_s && e->idle_ms >= (uint32_t)e->world->idle_drift_s * 1000u)
        camera_drift(e, dt_ms);
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

void mb_engine_warmup(mb_engine_t *e, uint32_t seconds) {
    for (uint32_t t = 0; t < seconds * 10u; t++) simulate(e, 100);
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

    mb_engine_warmup(e, cfg->warmup_s);
    return MB_OK;
}

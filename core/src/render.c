/* Strip renderer.
 *
 * Each viewport row is composed into an 8-bit indexed line buffer
 * (back layers -> actors sorted by y -> front layers -> particles), then
 * expanded through the palette LUT into the display's native format with
 * integer scaling. Only a strip-sized output buffer is ever needed. */
#include <string.h>

#include "internal.h"

MB_HOT static void draw_layer_row(const mb_engine_t *e, uint32_t li, int32_t vy, uint8_t *line) {
    const mb_pk_layer_t *l = &e->layers[li];
    const mb_pk_image_t *im = &e->images[l->image];
    int32_t sy = l->y - (int32_t)(((int64_t)e->cam_y * e->layer_py[li]) >> 16);
    int32_t iy = vy - sy;
    if (iy < 0 || iy >= im->h) return;
    int32_t sx = l->x - (int32_t)(((int64_t)e->cam_x * e->layer_px[li]) >> 16);
    const uint8_t *row = e->blob + im->data_off + (uint32_t)iy * im->w;
    int32_t vw = e->view_w;

    if (l->flags & MB_LAYER_REPEAT_X) {
        int32_t ix = mb_pmod(-sx, im->w);
        for (int32_t vx = 0; vx < vw; vx++) {
            uint8_t c = row[ix];
            if (c) line[vx] = c;
            if (++ix == im->w) ix = 0;
        }
    } else {
        int32_t x0 = sx < 0 ? 0 : sx, x1 = sx + im->w;
        if (x1 > vw) x1 = vw;
        for (int32_t vx = x0; vx < x1; vx++) {
            uint8_t c = row[vx - sx];
            if (c) line[vx] = c;
        }
    }
}

MB_HOT static void draw_actors_row(const mb_engine_t *e, int32_t vy, uint8_t *line) {
    int32_t camx = e->cam_x >> 8, camy = e->cam_y >> 8, vw = e->view_w;
    for (int k = 0; k < e->n_order; k++) {
        const mb_actor_t *a = &e->actors[e->order[k]];
        const mb_pk_actor_t *ad = &e->actor_defs[a->archetype];
        int32_t sy = (a->y >> 8) - ad->anchor_y - camy;
        int32_t iy = vy - sy;
        if (iy < 0 || iy >= ad->frame_h) continue;

        bool flip = ((a->flags & MB_AF_FACE_LEFT) != 0) != ((ad->flags & MB_ACTOR_FACES_LEFT) != 0);
        int32_t ax = flip ? ad->frame_w - 1 - ad->anchor_x : ad->anchor_x;
        int32_t sx = (a->x >> 8) - ax - camx;
        if (sx >= vw || sx + ad->frame_w <= 0) continue;

        const mb_pk_state_t *st = mb_actor_state(e, a);
        const mb_pk_anim_t *an = &e->anims[st->anim];
        uint32_t cell = e->frames[an->frame_first + a->anim_frame];
        const mb_pk_image_t *im = &e->images[ad->image];
        uint32_t cols = im->w / ad->frame_w;
        uint32_t fx = (cell % cols) * ad->frame_w, fy = (cell / cols) * ad->frame_h;
        const uint8_t *src = e->blob + im->data_off + (fy + (uint32_t)iy) * im->w + fx;

        for (int32_t i = 0; i < ad->frame_w; i++) {
            int32_t dx = sx + i;
            if (dx < 0) continue;
            if (dx >= vw) break;
            uint8_t c = flip ? src[ad->frame_w - 1 - i] : src[i];
            if (c) line[dx] = c;
        }
    }
}

MB_HOT static void draw_particles_row(const mb_engine_t *e, int32_t vy, uint8_t *line, uint8_t z) {
    int32_t camx = e->cam_x >> 8, camy = e->cam_y >> 8, vw = e->view_w;
    for (uint32_t s = 0; s < e->n_psys; s++) {
        const mb_pk_particle_t *ps = &e->psys[s];
        if (ps->after_layer != z) continue;
        int32_t size = ps->size ? ps->size : 1;
        for (uint32_t i = 0; i < e->psys_active[s]; i++) {
            const mb_particle_t *p = &e->particles[e->psys_base[s] + i];
            int32_t sy = (p->y >> 8) - camy;
            if (vy < sy || vy >= sy + size) continue;
            if (ps->flags & MB_PART_BLINK) {
                uint32_t period = 1200u + p->phase * 12u;
                if ((e->real_ms + p->phase * 97u) % period >= period * 2u / 5u) continue;
            }
            int32_t wob = 0;
            if (ps->wobble) {
                int32_t t = (int32_t)((e->real_ms / 12u + p->phase * 8u) & 255u);
                int32_t tri = t < 128 ? t : 255 - t; /* 0..127 */
                wob = tri * ps->wobble * 2 / 127 - ps->wobble;
            }
            int32_t sx = mb_pmod((p->x >> 8) - camx + wob, vw);
            for (int32_t k = 0; k < size; k++) line[(sx + k) % vw] = ps->color;
        }
    }
}

MB_HOT static void compose_line(mb_engine_t *e, int32_t vy) {
    uint8_t *line = e->line;
    memset(line, e->world->clear_color, e->view_w);
    bool has_z = e->psys_layered;
    for (uint32_t i = 0; i < e->n_layers; i++) {
        if (e->layers[i].flags & MB_LAYER_FRONT) continue;
        draw_layer_row(e, i, vy, line);
        if (has_z) draw_particles_row(e, vy, line, (uint8_t)i);
    }
    draw_actors_row(e, vy, line);
    for (uint32_t i = 0; i < e->n_layers; i++) {
        if (!(e->layers[i].flags & MB_LAYER_FRONT)) continue;
        draw_layer_row(e, i, vy, line);
        if (has_z) draw_particles_row(e, vy, line, (uint8_t)i);
    }
    draw_particles_row(e, vy, line, MB_PART_TOP);
}

MB_HOT void mb_engine_render_strip(mb_engine_t *e, int y0, int h, void *out, size_t stride) {
    const uint32_t *lut = e->lut;
    uint32_t clear = lut[e->world->clear_color];
    int32_t s = e->scale, vw = e->view_w, last_vy = -1, W = e->disp_w;

    for (int r = 0; r < h; r++) {
        uint8_t *row = (uint8_t *)out + (size_t)r * stride;
        int32_t vy = (y0 + r) / s;
        bool blank = vy >= e->view_h;
        if (!blank && vy != last_vy) {
            compose_line(e, vy);
            last_vy = vy;
        }
        if (e->format == MB_PIXFMT_XRGB8888) {
            uint32_t *o = (uint32_t *)row;
            int32_t x = 0;
            if (!blank)
                for (int32_t vx = 0; vx < vw; vx++)
                    for (int32_t k = 0; k < s; k++) o[x++] = lut[e->line[vx]];
            for (; x < W; x++) o[x] = clear;
        } else {
            uint16_t *o = (uint16_t *)row;
            int32_t x = 0;
            if (!blank) {
                if (s == 1) {
                    for (int32_t vx = 0; vx < vw; vx++) o[vx] = (uint16_t)lut[e->line[vx]];
                    x = vw;
                } else {
                    for (int32_t vx = 0; vx < vw; vx++) {
                        uint16_t v = (uint16_t)lut[e->line[vx]];
                        for (int32_t k = 0; k < s; k++) o[x++] = v;
                    }
                }
            }
            for (; x < W; x++) o[x] = (uint16_t)clear;
        }
    }
}

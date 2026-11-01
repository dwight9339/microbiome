/* Ambient particle systems: snow, rain, fireflies, stars, pollen...
 *
 * Particles live in viewport-wrapped world space: horizontally they tile every
 * view_w pixels, so they always fill the screen yet still move with the world
 * when the camera pans. Systems fade in/out gradually when their condition
 * changes. */
#include "internal.h"

static void reroll_velocity(mb_engine_t *e, mb_particle_t *p, const mb_pk_particle_t *ps) {
    p->vx = (int16_t)mb_rand_range(e, ps->vx_min, ps->vx_max);
    p->vy = (int16_t)mb_rand_range(e, ps->vy_min, ps->vy_max);
}

static void band(const mb_engine_t *e, const mb_pk_particle_t *ps, int32_t *lo, int32_t *hi) {
    *lo = ps->y_min;
    *hi = ps->y_max > ps->y_min ? ps->y_max : e->world->height;
}

void mb_particles_init(mb_engine_t *e) {
    uint32_t base = 0;
    for (uint32_t s = 0; s < e->n_psys; s++) {
        const mb_pk_particle_t *ps = &e->psys[s];
        uint32_t cap = ps->count;
        if (base + cap > MB_MAX_PARTICLES) cap = MB_MAX_PARTICLES - base;
        e->psys_base[s] = (uint16_t)base;
        e->psys_cap[s] = (uint16_t)cap;
        int32_t lo, hi;
        band(e, ps, &lo, &hi);
        for (uint32_t i = 0; i < cap; i++) {
            mb_particle_t *p = &e->particles[base + i];
            p->x = mb_rand_range(e, 0, e->view_w - 1) * 256;
            p->y = mb_rand_range(e, lo, hi - 1) * 256;
            p->phase = (uint8_t)mb_rand(e);
            p->sys = (uint8_t)s;
            reroll_velocity(e, p, ps);
        }
        e->psys_active[s] = mb_cond(e, ps->cond) ? (uint16_t)cap : 0;
        if (ps->after_layer != MB_PART_TOP) e->psys_layered = true;
        base += cap;
    }
}

void mb_particles_update(mb_engine_t *e, uint32_t dt_ms) {
    e->psys_ramp_ms += dt_ms;
    bool ramp = e->psys_ramp_ms >= 50;
    if (ramp) e->psys_ramp_ms = 0;
    int32_t wrap_w = (int32_t)e->view_w * 256;

    for (uint32_t s = 0; s < e->n_psys; s++) {
        const mb_pk_particle_t *ps = &e->psys[s];
        if (ramp) {
            uint16_t target = mb_cond(e, ps->cond) ? e->psys_cap[s] : 0;
            if (e->psys_active[s] < target) e->psys_active[s]++;
            else if (e->psys_active[s] > target) e->psys_active[s]--;
        }
        int32_t lo, hi;
        band(e, ps, &lo, &hi);
        lo *= 256;
        hi *= 256;
        for (uint32_t i = 0; i < e->psys_cap[s]; i++) {
            mb_particle_t *p = &e->particles[e->psys_base[s] + i];
            if ((ps->flags & MB_PART_WANDER) && (mb_rand(e) & 63) == 0) reroll_velocity(e, p, ps);
            /* v is px/s * 16 -> Q8 delta = v * 16 * dt / 1000 */
            p->x += (int32_t)p->vx * 16 * (int32_t)dt_ms / 1000;
            p->y += (int32_t)p->vy * 16 * (int32_t)dt_ms / 1000;
            if (p->y >= hi) {
                p->y = lo + (p->y - hi) % (hi - lo + 1);
                p->x = mb_rand_range(e, 0, e->view_w - 1) * 256;
            } else if (p->y < lo) {
                p->y = (ps->flags & MB_PART_WANDER) ? lo : hi - 256;
                if (ps->flags & MB_PART_WANDER) p->vy = (int16_t)(p->vy < 0 ? -p->vy : p->vy);
            }
            p->x = mb_pmod(p->x, wrap_w);
        }
    }
}

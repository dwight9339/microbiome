#ifndef MB_INTERNAL_H
#define MB_INTERNAL_H

#include "mb/mb.h"

#define MB_DAY_MS 86400000u

/* actor runtime flags */
#define MB_AF_FACE_LEFT 0x01u
#define MB_AF_LEAVING   0x02u
#define MB_AF_ARRIVED   0x04u

/* ---- rng (xorshift32) --------------------------------------------------- */
static inline uint32_t mb_rand(mb_engine_t *e) {
    uint32_t x = e->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return e->rng = x;
}
/* inclusive range */
static inline int32_t mb_rand_range(mb_engine_t *e, int32_t lo, int32_t hi) {
    if (hi <= lo) return lo;
    return lo + (int32_t)(mb_rand(e) % (uint32_t)(hi - lo + 1));
}

static inline int32_t mb_clamp(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int32_t mb_pmod(int32_t a, int32_t m) {
    int32_t r = a % m;
    return r < 0 ? r + m : r;
}

/* pack.c */
mb_err_t mb_pack_bind(mb_engine_t *e, const void *pack, size_t size);

/* world.c */
bool mb_cond(const mb_engine_t *e, uint16_t cond);
void mb_clock_init(mb_engine_t *e);
void mb_clock_update(mb_engine_t *e, uint32_t dt_ms);
void mb_vars_update(mb_engine_t *e, bool force);
void mb_palette_update(mb_engine_t *e, bool force);

/* actors.c */
void mb_actors_update(mb_engine_t *e, uint32_t dt_ms);
void mb_spawn_update(mb_engine_t *e, uint32_t dt_ms);
void mb_actors_sort(mb_engine_t *e);
const mb_pk_state_t *mb_actor_state(const mb_engine_t *e, const mb_actor_t *a);

/* particles.c */
void mb_particles_init(mb_engine_t *e);
void mb_particles_update(mb_engine_t *e, uint32_t dt_ms);

#endif

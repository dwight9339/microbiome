/* Pack binding and validation.
 *
 * Packs are untrusted-ish data (they may come from an SD card or an OTA
 * download someday), so every cross-reference is range-checked once here and
 * the hot paths can then index without checks. */
#include <string.h>

#include "internal.h"

static const uint32_t k_elem_size[MB_SEC_COUNT] = {
    [MB_SEC_WORLD]     = sizeof(mb_pk_world_t),
    [MB_SEC_PALETTES]  = sizeof(mb_pk_palette_t),
    [MB_SEC_IMAGES]    = sizeof(mb_pk_image_t),
    [MB_SEC_LAYERS]    = sizeof(mb_pk_layer_t),
    [MB_SEC_ANIMS]     = sizeof(mb_pk_anim_t),
    [MB_SEC_FRAMES]    = sizeof(uint16_t),
    [MB_SEC_ACTORS]    = sizeof(mb_pk_actor_t),
    [MB_SEC_STATES]    = sizeof(mb_pk_state_t),
    [MB_SEC_NEXT]      = sizeof(mb_pk_next_t),
    [MB_SEC_REACTIONS] = sizeof(mb_pk_reaction_t),
    [MB_SEC_CONDS]     = sizeof(mb_pk_cond_t),
    [MB_SEC_CLAUSES]   = sizeof(mb_pk_clause_t),
    [MB_SEC_VARS]      = sizeof(mb_pk_var_t),
    [MB_SEC_SPAWNS]    = sizeof(mb_pk_spawn_t),
    [MB_SEC_LANES]     = sizeof(mb_pk_lane_t),
    [MB_SEC_PLACES]    = sizeof(mb_pk_place_t),
    [MB_SEC_PARTICLES] = sizeof(mb_pk_particle_t),
    [MB_SEC_OBSTACLES] = sizeof(mb_pk_obstacle_t),
    [MB_SEC_OCCLUDERS] = sizeof(mb_pk_occluder_t),
    [MB_SEC_BLOB]      = 1,
    [MB_SEC_STRINGS]   = 1,
};

const char *mb_err_str(mb_err_t err) {
    switch (err) {
    case MB_OK: return "ok";
    case MB_ERR_BAD_MAGIC: return "not a microbiome pack (bad magic)";
    case MB_ERR_BAD_VERSION: return "unsupported pack version";
    case MB_ERR_TRUNCATED: return "pack truncated";
    case MB_ERR_BAD_SECTION: return "malformed pack section";
    case MB_ERR_BAD_INDEX: return "pack contains an out-of-range reference";
    case MB_ERR_CAPACITY: return "pack exceeds engine capacity (see MB_MAX_* in mb.h)";
    case MB_ERR_DISPLAY: return "unsupported display configuration";
    }
    return "unknown error";
}

const char *mb_str(const mb_engine_t *e, uint32_t off) {
    if (!e->strings || off >= e->strings_size) return "";
    return e->strings + off;
}

#define CHECK(c, err) do { if (!(c)) return (err); } while (0)

static bool str_ok(const mb_engine_t *e, uint32_t off) { return off < e->strings_size; }

static bool cond_ok(const mb_engine_t *e, uint16_t c) { return c < e->n_conds; }

static mb_err_t validate(const mb_engine_t *e) {
    CHECK(e->n_palettes >= 1, MB_ERR_BAD_SECTION);
    CHECK(e->strings_size >= 1 && e->strings[e->strings_size - 1] == '\0', MB_ERR_BAD_SECTION);
    CHECK(e->n_conds >= 1 && e->conds[0].clause_count == 0, MB_ERR_BAD_SECTION);
    CHECK(e->n_layers <= MB_MAX_LAYERS, MB_ERR_CAPACITY);
    CHECK(e->n_vars <= MB_MAX_VARS, MB_ERR_CAPACITY);
    CHECK(e->n_spawns <= MB_MAX_SPAWNS, MB_ERR_CAPACITY);
    CHECK(e->n_psys <= MB_MAX_PSYS, MB_ERR_CAPACITY);
    CHECK(e->n_obstacles <= MB_MAX_OBSTACLES, MB_ERR_CAPACITY);
    CHECK(e->n_occluders <= MB_MAX_OCCLUDERS, MB_ERR_CAPACITY);
    CHECK(e->world->width > 0 && e->world->height > 0, MB_ERR_BAD_SECTION);

    for (uint32_t i = 0; i < e->n_images; i++) {
        const mb_pk_image_t *im = &e->images[i];
        uint64_t end = (uint64_t)im->data_off + (uint64_t)im->w * im->h;
        CHECK(end <= e->blob_size, MB_ERR_BAD_INDEX);
    }
    for (uint32_t i = 0; i < e->n_layers; i++) {
        CHECK(e->layers[i].image < e->n_images, MB_ERR_BAD_INDEX);
        CHECK(str_ok(e, e->layers[i].name_str), MB_ERR_BAD_INDEX);
    }
    for (uint32_t i = 0; i < e->n_anims; i++) {
        const mb_pk_anim_t *an = &e->anims[i];
        CHECK(an->frame_count >= 1, MB_ERR_BAD_SECTION);
        CHECK((uint32_t)an->frame_first + an->frame_count <= e->n_frames, MB_ERR_BAD_INDEX);
    }
    for (uint32_t i = 0; i < e->n_clauses; i++)
        CHECK(e->clauses[i].var < e->n_vars, MB_ERR_BAD_INDEX);
    for (uint32_t i = 0; i < e->n_conds; i++)
        CHECK((uint32_t)e->conds[i].clause_first + e->conds[i].clause_count <= e->n_clauses,
              MB_ERR_BAD_INDEX);
    for (uint32_t i = 0; i < e->n_actor_defs; i++) {
        const mb_pk_actor_t *ad = &e->actor_defs[i];
        CHECK(str_ok(e, ad->name_str), MB_ERR_BAD_INDEX);
        CHECK(ad->image < e->n_images, MB_ERR_BAD_INDEX);
        const mb_pk_image_t *im = &e->images[ad->image];
        CHECK(ad->frame_w > 0 && ad->frame_h > 0 && ad->frame_w <= im->w && ad->frame_h <= im->h,
              MB_ERR_BAD_SECTION);
        uint32_t cells = (uint32_t)(im->w / ad->frame_w) * (im->h / ad->frame_h);
        CHECK(ad->state_count >= 1, MB_ERR_BAD_SECTION);
        CHECK((uint32_t)ad->state_first + ad->state_count <= e->n_states, MB_ERR_BAD_INDEX);
        CHECK((uint32_t)ad->react_first + ad->react_count <= e->n_reactions, MB_ERR_BAD_INDEX);
        CHECK(ad->lane == MB_NO_LANE || ad->lane < e->n_lanes, MB_ERR_BAD_INDEX);
        CHECK(ad->leave_state == MB_NO_STATE || ad->leave_state < ad->state_count, MB_ERR_BAD_INDEX);
        CHECK(cond_ok(e, ad->despawn_cond), MB_ERR_BAD_INDEX);
        for (uint32_t s = 0; s < ad->state_count; s++) {
            const mb_pk_state_t *st = &e->states[ad->state_first + s];
            CHECK(str_ok(e, st->name_str), MB_ERR_BAD_INDEX);
            CHECK(st->anim < e->n_anims, MB_ERR_BAD_INDEX);
            const mb_pk_anim_t *an = &e->anims[st->anim];
            for (uint32_t f = 0; f < an->frame_count; f++)
                CHECK(e->frames[an->frame_first + f] < cells, MB_ERR_BAD_INDEX);
            CHECK((uint32_t)st->next_first + st->next_count <= e->n_nexts, MB_ERR_BAD_INDEX);
            for (uint32_t n = 0; n < st->next_count; n++) {
                const mb_pk_next_t *nx = &e->nexts[st->next_first + n];
                CHECK(nx->to < ad->state_count && cond_ok(e, nx->cond), MB_ERR_BAD_INDEX);
            }
        }
        for (uint32_t r = 0; r < ad->react_count; r++)
            CHECK(e->reactions[ad->react_first + r].goto_state < ad->state_count, MB_ERR_BAD_INDEX);
    }
    for (uint32_t i = 0; i < e->n_vars; i++) {
        const mb_pk_var_t *v = &e->var_defs[i];
        CHECK(str_ok(e, v->name_str) && str_ok(e, v->sensor_str) && str_ok(e, v->values_str),
              MB_ERR_BAD_INDEX);
        uint64_t need = 0;
        if (v->kind == MB_VAR_STEP) {
            CHECK(v->param < i && v->count >= 1, MB_ERR_BAD_INDEX); /* source evaluated first */
            need = (uint64_t)v->count * 4;
        } else if (v->kind == MB_VAR_MARKOV) {
            CHECK(v->count >= 1 && v->param >= 1, MB_ERR_BAD_SECTION);
            need = (uint64_t)v->count * v->count;
        }
        CHECK((uint64_t)v->data_off + need <= e->blob_size, MB_ERR_BAD_INDEX);
    }
    for (uint32_t i = 0; i < e->n_spawns; i++) {
        const mb_pk_spawn_t *sp = &e->spawns[i];
        CHECK(sp->actor < e->n_actor_defs && cond_ok(e, sp->cond), MB_ERR_BAD_INDEX);
        CHECK(sp->group_min >= 1 && sp->group_max >= sp->group_min, MB_ERR_BAD_SECTION);
    }
    for (uint32_t i = 0; i < e->n_psys; i++) {
        CHECK(cond_ok(e, e->psys[i].cond), MB_ERR_BAD_INDEX);
        CHECK(e->psys[i].after_layer == MB_PART_TOP || e->psys[i].after_layer < e->n_layers,
              MB_ERR_BAD_INDEX);
    }
    for (uint32_t i = 0; i < e->n_obstacles; i++) {
        const mb_pk_obstacle_t *ob = &e->obstacles[i];
        CHECK(ob->lane < e->n_lanes && str_ok(e, ob->name_str), MB_ERR_BAD_INDEX);
        CHECK(ob->shape == MB_OBSTACLE_ELLIPSE && ob->x0 < ob->x1 && ob->y0 < ob->y1, MB_ERR_BAD_SECTION);
        CHECK(ob->x1 - ob->x0 <= MB_OBSTACLE_MAX_SIZE && ob->y1 - ob->y0 <= MB_OBSTACLE_MAX_SIZE,
              MB_ERR_BAD_SECTION);
    }
    for (uint32_t i = 0; i < e->n_occluders; i++) {
        const mb_pk_occluder_t *oc = &e->occluders[i];
        CHECK(oc->image < e->n_images && str_ok(e, oc->name_str), MB_ERR_BAD_INDEX);
        CHECK(e->n_lanes >= 16 || (oc->lane_mask >> e->n_lanes) == 0, MB_ERR_BAD_INDEX);
    }
    for (uint32_t i = 0; i < e->n_palettes; i++)
        CHECK(e->palettes[i].minute < 1440, MB_ERR_BAD_SECTION);
    return MB_OK;
}

mb_err_t mb_pack_bind(mb_engine_t *e, const void *pack, size_t size) {
    const uint8_t *p = (const uint8_t *)pack;
    CHECK(size >= sizeof(mb_pk_header_t), MB_ERR_TRUNCATED);
    CHECK(((uintptr_t)p & 3u) == 0, MB_ERR_BAD_SECTION);
    const mb_pk_header_t *h = (const mb_pk_header_t *)p;
    CHECK(h->magic == MB_PACK_MAGIC, MB_ERR_BAD_MAGIC);
    CHECK(h->version == MB_PACK_VERSION, MB_ERR_BAD_VERSION);
    CHECK(h->section_count >= MB_SEC_COUNT, MB_ERR_BAD_SECTION);
    CHECK(h->total_size <= size, MB_ERR_TRUNCATED);

    const void *sec[MB_SEC_COUNT];
    uint32_t cnt[MB_SEC_COUNT];
    for (int i = 0; i < MB_SEC_COUNT; i++) {
        const mb_pk_section_t *s = &h->sections[i];
        uint64_t end = (uint64_t)s->offset + (uint64_t)s->count * k_elem_size[i];
        CHECK((s->offset & 3u) == 0, MB_ERR_BAD_SECTION);
        CHECK(end <= h->total_size, MB_ERR_TRUNCATED);
        sec[i] = p + s->offset;
        cnt[i] = s->count;
    }
    CHECK(cnt[MB_SEC_WORLD] == 1, MB_ERR_BAD_SECTION);

    e->pack = p;
    e->world = sec[MB_SEC_WORLD];
    e->palettes = sec[MB_SEC_PALETTES];     e->n_palettes = cnt[MB_SEC_PALETTES];
    e->images = sec[MB_SEC_IMAGES];         e->n_images = cnt[MB_SEC_IMAGES];
    e->layers = sec[MB_SEC_LAYERS];         e->n_layers = cnt[MB_SEC_LAYERS];
    e->anims = sec[MB_SEC_ANIMS];           e->n_anims = cnt[MB_SEC_ANIMS];
    e->frames = sec[MB_SEC_FRAMES];         e->n_frames = cnt[MB_SEC_FRAMES];
    e->actor_defs = sec[MB_SEC_ACTORS];     e->n_actor_defs = cnt[MB_SEC_ACTORS];
    e->states = sec[MB_SEC_STATES];         e->n_states = cnt[MB_SEC_STATES];
    e->nexts = sec[MB_SEC_NEXT];            e->n_nexts = cnt[MB_SEC_NEXT];
    e->reactions = sec[MB_SEC_REACTIONS];   e->n_reactions = cnt[MB_SEC_REACTIONS];
    e->conds = sec[MB_SEC_CONDS];           e->n_conds = cnt[MB_SEC_CONDS];
    e->clauses = sec[MB_SEC_CLAUSES];       e->n_clauses = cnt[MB_SEC_CLAUSES];
    e->var_defs = sec[MB_SEC_VARS];         e->n_vars = cnt[MB_SEC_VARS];
    e->spawns = sec[MB_SEC_SPAWNS];         e->n_spawns = cnt[MB_SEC_SPAWNS];
    e->lanes = sec[MB_SEC_LANES];           e->n_lanes = cnt[MB_SEC_LANES];
    e->places = sec[MB_SEC_PLACES];         e->n_places = cnt[MB_SEC_PLACES];
    e->psys = sec[MB_SEC_PARTICLES];        e->n_psys = cnt[MB_SEC_PARTICLES];
    e->obstacles = sec[MB_SEC_OBSTACLES];   e->n_obstacles = cnt[MB_SEC_OBSTACLES];
    e->occluders = sec[MB_SEC_OCCLUDERS];   e->n_occluders = cnt[MB_SEC_OCCLUDERS];
    e->blob = sec[MB_SEC_BLOB];             e->blob_size = cnt[MB_SEC_BLOB];
    e->strings = sec[MB_SEC_STRINGS];       e->strings_size = cnt[MB_SEC_STRINGS];
    return validate(e);
}

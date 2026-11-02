/* Actors: spawning, state machines, movement, reactions, despawning. */
#include "internal.h"

#define Q8(v) ((int32_t)(v) * 256)

static const mb_pk_actor_t *def_of(const mb_engine_t *e, const mb_actor_t *a) {
    return &e->actor_defs[a->archetype];
}

const mb_pk_state_t *mb_actor_state(const mb_engine_t *e, const mb_actor_t *a) {
    return &e->states[def_of(e, a)->state_first + a->state];
}

static uint32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

static void lane_y_range(const mb_engine_t *e, const mb_pk_actor_t *ad, int32_t *lo, int32_t *hi) {
    if (ad->lane != MB_NO_LANE) {
        *lo = e->lanes[ad->lane].y_min;
        *hi = e->lanes[ad->lane].y_max;
    } else {
        *lo = *hi = e->world->height - 1;
    }
}

/* ---- obstacles -----------------------------------------------------------
 * Ellipses are tested in doubled, scaled coordinates so everything stays
 * integer: a point is inside when u^2 + v^2 < r^2 with
 * u = (2x - x0 - x1) * h, v = (2y - y0 - y1) * w, r = w * h. Obstacle sides are
 * capped at MB_OBSTACLE_MAX_SIZE, which keeps every product below 2^62. */

#define OB_PAD 2 /* detour corners sit this far outside an obstacle's box */
#define MAX_NODES (2 + 4 * MB_MAX_OBSTACLES)

/* The edge counts as inside, so paths can't skim along the water's top row. */
static bool ob_contains(const mb_pk_obstacle_t *ob, int32_t x, int32_t y) {
    if (x < ob->x0 || x > ob->x1 || y < ob->y0 || y > ob->y1) return false;
    int64_t w = ob->x1 - ob->x0, h = ob->y1 - ob->y0, r = w * h;
    int64_t u = (2 * (int64_t)x - ob->x0 - ob->x1) * h, v = (2 * (int64_t)y - ob->y0 - ob->y1) * w;
    return u * u + v * v <= r * r;
}

/* Does the segment a-b pass strictly through the ellipse filling the box,
 * other than at an endpoint? */
static bool seg_hits_ellipse(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                             int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    if ((ax > bx ? ax : bx) < x0 || (ax < bx ? ax : bx) > x1 || (ay > by ? ay : by) < y0 ||
        (ay < by ? ay : by) > y1)
        return false;
    int64_t w = x1 - x0, h = y1 - y0, r = w * h;
    int64_t pu = (2 * (int64_t)ax - x0 - x1) * h, pv = (2 * (int64_t)ay - y0 - y1) * w;
    int64_t du = 2 * (int64_t)(bx - ax) * h, dv = 2 * (int64_t)(by - ay) * w;
    int64_t len2 = du * du + dv * dv, along = -(pu * du + pv * dv);
    if (along <= 0 || along >= len2) return false; /* nearest point to the centre is an endpoint */
    int64_t off = (pu * dv - pv * du) / (int64_t)isqrt64((uint64_t)len2); /* distance from centre */
    return off * off <= r * r;
}

/* Does walking straight from a to b cross the obstacle? Endpoints may sit on
 * the shore, but the rest of the walk keeps 1 px clear of it, so fixed-point
 * drift along a near-tangent path can't dip in. */
static bool ob_blocks(const mb_pk_obstacle_t *ob, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    return ob_contains(ob, ax, ay) || ob_contains(ob, bx, by) ||
           seg_hits_ellipse(ob->x0 - 1, ob->y0 - 1, ob->x1 + 1, ob->y1 + 1, ax, ay, bx, by);
}

static bool in_obstacle(const mb_engine_t *e, uint8_t lane, int32_t x, int32_t y) {
    for (uint32_t i = 0; i < e->n_obstacles; i++)
        if (e->obstacles[i].lane == lane && ob_contains(&e->obstacles[i], x, y)) return true;
    return false;
}

static bool path_clear(const mb_engine_t *e, uint32_t mask, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    for (uint32_t i = 0; i < e->n_obstacles; i++)
        if ((mask >> i & 1u) && ob_blocks(&e->obstacles[i], ax, ay, bx, by)) return false;
    return true;
}

/* Move a point inside the ellipse out to its edge, along the ray from the centre. */
static void push_out(const mb_pk_obstacle_t *ob, int32_t *x, int32_t *y) {
    int64_t w = ob->x1 - ob->x0, h = ob->y1 - ob->y0, r = w * h;
    int64_t X = 2 * (int64_t)*x - ob->x0 - ob->x1, Y = 2 * (int64_t)*y - ob->y0 - ob->y1;
    uint64_t len = isqrt64((uint64_t)(X * h * X * h + Y * w * Y * w));
    if (!len) { *y = ob->y0 - 1; return; } /* dead centre: step out behind it */
    *x = (int32_t)((ob->x0 + ob->x1 + X * r / (int64_t)len) / 2);
    *y = (int32_t)((ob->y0 + ob->y1 + Y * r / (int64_t)len) / 2);
    for (int i = 0; i < 8 && ob_contains(ob, *x, *y); i++) { /* rounding */
        *x += (X > 0) - (X < 0);
        *y += (Y > 0) - (Y < 0);
    }
}

/* Point the actor at the first waypoint of the shortest path to its target.
 * The graph is the start, the target, and the padded bounding-box corners of
 * the obstacles in its lane; the actor replans each time it reaches a corner. */
static void plan_route(mb_engine_t *e, mb_actor_t *a) {
    const mb_pk_actor_t *ad = def_of(e, a);
    a->wx = a->tx;
    a->wy = a->ty;
    if (ad->lane == MB_NO_LANE || !e->n_obstacles) return;

    /* Plan in whole pixels and walk exactly that line (a sub-pixel offset
     * could clip an edge the plan just clears). Neither end may be inside an
     * obstacle: a target there (a drinking spot whose spread reaches into the
     * pond) moves to the shore, and so does an actor that rounding nudged in. */
    int32_t sx = a->x >> 8, sy = a->y >> 8, gx = a->tx >> 8, gy = a->ty >> 8;
    uint32_t mask = 0;
    for (uint32_t i = 0; i < e->n_obstacles; i++) {
        const mb_pk_obstacle_t *ob = &e->obstacles[i];
        if (ob->lane != ad->lane) continue;
        mask |= 1u << i;
        if (ob_contains(ob, sx, sy)) push_out(ob, &sx, &sy);
        if (ob_contains(ob, gx, gy)) push_out(ob, &gx, &gy);
    }
    a->x = Q8(sx);
    a->y = Q8(sy);
    a->wx = a->tx = Q8(gx);
    a->wy = a->ty = Q8(gy);
    if (!mask || path_clear(e, mask, sx, sy, gx, gy)) return;

    int32_t ylo, yhi;
    lane_y_range(e, ad, &ylo, &yhi);
    int32_t nx[MAX_NODES], ny[MAX_NODES];
    int n = 2;
    nx[0] = sx; ny[0] = sy;
    nx[1] = gx; ny[1] = gy;
    for (uint32_t i = 0; i < e->n_obstacles; i++) {
        if (!(mask >> i & 1u)) continue;
        const mb_pk_obstacle_t *ob = &e->obstacles[i];
        for (int c = 0; c < 4; c++) {
            int32_t cx = c & 1 ? ob->x1 + OB_PAD : ob->x0 - OB_PAD;
            int32_t cy = c & 2 ? ob->y1 + OB_PAD : ob->y0 - OB_PAD;
            if (cy < ylo || cy > yhi || (cx == sx && cy == sy)) continue;
            bool buried = false;
            for (uint32_t j = 0; j < e->n_obstacles && !buried; j++)
                buried = (mask >> j & 1u) && ob_contains(&e->obstacles[j], cx, cy);
            if (buried) continue;
            nx[n] = cx;
            ny[n] = cy;
            n++;
        }
    }

    /* Dijkstra; the graph is tiny, so a linear scan for the next node is fine. */
    uint32_t dist[MAX_NODES];
    int8_t prev[MAX_NODES];
    uint64_t done = 0;
    for (int i = 0; i < n; i++) { dist[i] = UINT32_MAX; prev[i] = -1; }
    dist[0] = 0;
    for (;;) {
        int u = -1;
        for (int i = 0; i < n; i++)
            if (!(done >> i & 1u) && dist[i] != UINT32_MAX && (u < 0 || dist[i] < dist[u])) u = i;
        if (u < 0 || u == 1) break;
        done |= 1ull << u;
        for (int v = 1; v < n; v++) {
            if (done >> v & 1u || !path_clear(e, mask, nx[u], ny[u], nx[v], ny[v])) continue;
            int64_t dx = nx[v] - nx[u], dy = ny[v] - ny[u];
            uint32_t d = dist[u] + isqrt64((uint64_t)(dx * dx + dy * dy));
            if (d < dist[v]) { dist[v] = d; prev[v] = (int8_t)u; }
        }
    }
    if (dist[1] == UINT32_MAX) return; /* boxed in: walk straight rather than freeze */
    int hop = 1;
    while (prev[hop] != 0) hop = prev[hop];
    if (hop != 1) {
        a->wx = Q8(nx[hop]);
        a->wy = Q8(ny[hop]);
    }
}

static void set_target(mb_engine_t *e, mb_actor_t *a, int32_t tx, int32_t ty) {
    a->tx = tx;
    a->ty = ty;
    plan_route(e, a);
}

static void pick_wander_target(mb_engine_t *e, mb_actor_t *a) {
    const mb_pk_actor_t *ad = def_of(e, a);
    int32_t w = e->world->width, x = a->x >> 8;
    int32_t margin = ad->frame_w / 2 + 2;
    int32_t lo = mb_clamp(x - 140, margin, w - margin), hi = mb_clamp(x + 140, margin, w - margin);
    if (x < 0) { lo = margin; hi = mb_clamp(margin + 160, margin, w - margin); }
    if (x >= w) { hi = w - margin; lo = mb_clamp(w - margin - 160, margin, w - margin); }
    int32_t ylo, yhi;
    lane_y_range(e, ad, &ylo, &yhi);
    for (int tries = 0; tries < 8; tries++) {
        int32_t tx = mb_rand_range(e, lo, hi), ty = mb_rand_range(e, ylo, yhi);
        if (!in_obstacle(e, ad->lane, tx, ty)) {
            set_target(e, a, Q8(tx), Q8(ty));
            return;
        }
    }
    set_target(e, a, a->x, a->y); /* unlucky: stay put, try again next time */
}

static void set_exit_target(mb_engine_t *e, mb_actor_t *a, bool go_right) {
    const mb_pk_actor_t *ad = def_of(e, a);
    int32_t off = ad->frame_w + 4;
    set_target(e, a, go_right ? Q8(e->world->width + off) : Q8(-off), a->y);
}

static int find_place(mb_engine_t *e, uint32_t aff_mask) {
    int found = -1, n = 0;
    for (uint32_t i = 0; i < e->n_places; i++) {
        if (!(e->places[i].affordances & aff_mask)) continue;
        /* reservoir sampling: uniform among matching places */
        if (mb_rand(e) % (uint32_t)(++n) == 0) found = (int)i;
    }
    return found;
}

static void enter_state(mb_engine_t *e, mb_actor_t *a, uint8_t s) {
    const mb_pk_actor_t *ad = def_of(e, a);
    if (s >= ad->state_count) s = 0;
    a->state = s;
    a->state_ms = 0;
    a->anim_ms = 0;
    a->anim_frame = 0;
    a->flags &= (uint8_t)~MB_AF_ARRIVED;
    const mb_pk_state_t *st = mb_actor_state(e, a);
    uint32_t lo = st->dur_min_ds, hi = st->dur_max_ds;
    a->state_dur_ms = (uint32_t)mb_rand_range(e, (int32_t)lo, (int32_t)hi) * 100u;

    switch (st->move) {
    case MB_MOVE_WANDER:
        pick_wander_target(e, a);
        break;
    case MB_MOVE_SEEK: {
        int p = find_place(e, st->move_param);
        if (p >= 0) {
            int32_t sp = e->places[p].spread;
            set_target(e, a, Q8(e->places[p].x + mb_rand_range(e, -sp, sp)), Q8(e->places[p].y));
        } else {
            pick_wander_target(e, a);
        }
        break;
    }
    case MB_MOVE_EXIT:
        set_exit_target(e, a, (a->x >> 8) >= e->world->width / 2);
        break;
    case MB_MOVE_FLEE:
        set_exit_target(e, a, a->threat_x <= a->x);
        break;
    default: /* idle */
        set_target(e, a, a->x, a->y);
        if (!a->state_dur_ms) a->state_dur_ms = 5000; /* idle must end eventually */
        break;
    }
}

static void despawn(mb_engine_t *e, mb_actor_t *a);

static void finish_state(mb_engine_t *e, mb_actor_t *a) {
    const mb_pk_actor_t *ad = def_of(e, a);
    if (mb_actor_state(e, a)->flags & MB_STATE_DESPAWN_END) {
        despawn(e, a);
        return;
    }
    if ((a->flags & MB_AF_LEAVING) && ad->leave_state != MB_NO_STATE) {
        enter_state(e, a, ad->leave_state);
        return;
    }
    const mb_pk_state_t *st = mb_actor_state(e, a);
    uint32_t total = 0;
    for (uint32_t i = 0; i < st->next_count; i++) {
        const mb_pk_next_t *nx = &e->nexts[st->next_first + i];
        if (mb_cond(e, nx->cond)) total += nx->weight;
    }
    if (!total) { enter_state(e, a, a->state); return; }
    uint32_t r = mb_rand(e) % total;
    for (uint32_t i = 0; i < st->next_count; i++) {
        const mb_pk_next_t *nx = &e->nexts[st->next_first + i];
        if (!mb_cond(e, nx->cond)) continue;
        if (r < nx->weight) { enter_state(e, a, nx->to); return; }
        r -= nx->weight;
    }
}

static void despawn(mb_engine_t *e, mb_actor_t *a) {
    if (a->spawn_rule < e->n_spawns && e->spawn_alive[a->spawn_rule]) e->spawn_alive[a->spawn_rule]--;
    a->used = 0;
}

static void start_leaving(mb_engine_t *e, mb_actor_t *a) {
    const mb_pk_actor_t *ad = def_of(e, a);
    if (a->flags & MB_AF_LEAVING) return;
    a->flags |= MB_AF_LEAVING;
    if (ad->leave_state != MB_NO_STATE) {
        enter_state(e, a, ad->leave_state);
    } else {
        despawn(e, a); /* no graceful exit defined */
    }
}

static mb_actor_t *alloc_actor(mb_engine_t *e) {
    uint32_t alive = 0;
    mb_actor_t *free_slot = 0;
    for (int i = 0; i < MB_MAX_ACTORS; i++) {
        if (e->actors[i].used) alive++;
        else if (!free_slot) free_slot = &e->actors[i];
    }
    return alive < e->max_actors ? free_slot : 0;
}

static bool on_camera(const mb_engine_t *e, int32_t x_px, int32_t margin) {
    int32_t cx = e->cam_x >> 8;
    return x_px > cx - margin && x_px < cx + e->view_w + margin;
}

static void spawn_one(mb_engine_t *e, uint16_t rule, int nth) {
    const mb_pk_spawn_t *sp = &e->spawns[rule];
    const mb_pk_actor_t *ad = &e->actor_defs[sp->actor];
    mb_actor_t *a = alloc_actor(e);
    if (!a) return;
    *a = (mb_actor_t){0};
    a->used = 1;
    a->archetype = sp->actor;
    a->spawn_rule = rule;
    a->id = ++e->next_id ? e->next_id : ++e->next_id;

    int32_t ylo, yhi, x, y;
    lane_y_range(e, ad, &ylo, &yhi);
    y = mb_rand_range(e, ylo, yhi);
    switch (sp->enter) {
    case MB_ENTER_PLACE: {
        int p = find_place(e, 1u << (sp->enter_param & 31));
        if (p >= 0) {
            x = e->places[p].x + nth * 6;
            y = e->places[p].y;
            break;
        }
    }
        /* no such place: enter anywhere */
        /* fall through */
    case MB_ENTER_ANYWHERE:
        x = mb_rand_range(e, 0, e->world->width - 1);
        for (int tries = 0; tries < 8 && (on_camera(e, x, ad->frame_w) || in_obstacle(e, ad->lane, x, y));
             tries++) {
            x = mb_rand_range(e, 0, e->world->width - 1);
            y = mb_rand_range(e, ylo, yhi);
        }
        break;
    default: { /* edge */
        bool right = mb_rand(e) & 1;
        int32_t off = ad->frame_w + nth * (ad->frame_w + 6);
        x = right ? e->world->width + off : -off;
        break;
    }
    }
    a->x = Q8(x);
    a->y = Q8(y);
    a->threat_x = a->x;
    e->spawn_alive[rule]++;
    enter_state(e, a, 0);
}

void mb_spawn_update(mb_engine_t *e, uint32_t dt_ms) {
    for (uint32_t i = 0; i < e->n_spawns; i++)
        e->spawn_cd_ms[i] = e->spawn_cd_ms[i] > dt_ms ? e->spawn_cd_ms[i] - dt_ms : 0;

    uint32_t interval = (uint32_t)e->world->spawn_interval_ds * 100u;
    if (!interval) interval = 5000;
    e->spawn_timer_ms += dt_ms;
    if (e->spawn_timer_ms < interval) return;
    e->spawn_timer_ms -= interval;
    if (e->spawn_timer_ms > interval) e->spawn_timer_ms = 0; /* don't burst after a stall */

    if ((mb_rand(e) % 100u) >= e->world->spawn_chance) return;

    uint32_t total = 0;
    for (uint32_t i = 0; i < e->n_spawns; i++) {
        const mb_pk_spawn_t *sp = &e->spawns[i];
        if (e->spawn_cd_ms[i] || e->spawn_alive[i] >= sp->max_alive || !mb_cond(e, sp->cond)) continue;
        total += sp->weight;
    }
    if (!total) return;
    uint32_t r = mb_rand(e) % total;
    for (uint32_t i = 0; i < e->n_spawns; i++) {
        const mb_pk_spawn_t *sp = &e->spawns[i];
        if (e->spawn_cd_ms[i] || e->spawn_alive[i] >= sp->max_alive || !mb_cond(e, sp->cond)) continue;
        if (r >= sp->weight) { r -= sp->weight; continue; }
        int n = mb_rand_range(e, sp->group_min, sp->group_max);
        for (int k = 0; k < n && e->spawn_alive[i] < sp->max_alive; k++) spawn_one(e, (uint16_t)i, k);
        e->spawn_cd_ms[i] = (uint32_t)sp->cooldown_s * 1000u;
        return;
    }
}

static void check_reactions(mb_engine_t *e, mb_actor_t *a) {
    const mb_pk_actor_t *ad = def_of(e, a);
    for (uint32_t r = 0; r < ad->react_count; r++) {
        const mb_pk_reaction_t *re = &e->reactions[ad->react_first + r];
        if (a->state == re->goto_state) continue;
        if (mb_actor_state(e, a)->move == MB_MOVE_FLEE) return; /* already running */
        int64_t rad = (int64_t)re->radius * 256;
        for (int j = 0; j < MB_MAX_ACTORS; j++) {
            const mb_actor_t *b = &e->actors[j];
            if (!b->used || b == a || !(e->actor_defs[b->archetype].tags & re->tags)) continue;
            int64_t dx = b->x - a->x, dy = b->y - a->y;
            if (dx * dx + dy * dy > rad * rad) continue;
            a->threat_x = b->x;
            a->calm_ms = 8000; /* don't re-trigger while acting on it */
            enter_state(e, a, re->goto_state);
            return;
        }
    }
}

static void move_actor(mb_engine_t *e, mb_actor_t *a, const mb_pk_state_t *st, uint32_t dt_ms) {
    const mb_pk_actor_t *ad = def_of(e, a);
    if (a->x == a->tx && a->y == a->ty) { a->flags |= MB_AF_ARRIVED; return; }
    int64_t dx = a->wx - a->x, dy = a->wy - a->y;
    /* Q8 px per second */
    int64_t speed = (int64_t)ad->speed_q4 * 16 * st->speed_x16 / 16;
    int64_t step = speed * dt_ms / 1000;
    if (step < 1) step = 1;
    uint64_t dist = isqrt64((uint64_t)(dx * dx + dy * dy));
    if (dist <= (uint64_t)step) {
        a->x = a->wx;
        a->y = a->wy;
        if (a->x == a->tx && a->y == a->ty) a->flags |= MB_AF_ARRIVED;
        else plan_route(e, a); /* reached a detour corner: on to the next leg */
    } else {
        a->x += (int32_t)(dx * step / (int64_t)dist);
        a->y += (int32_t)(dy * step / (int64_t)dist);
    }
    if (dx < -128) a->flags |= MB_AF_FACE_LEFT;
    else if (dx > 128) a->flags &= (uint8_t)~MB_AF_FACE_LEFT;
}

void mb_actors_update(mb_engine_t *e, uint32_t dt_ms) {
    bool do_react = false, do_despawn = false;
    e->react_ms += dt_ms;
    e->despawn_check_ms += dt_ms;
    if (e->react_ms >= 200) { e->react_ms = 0; do_react = true; }
    if (e->despawn_check_ms >= 1000) { e->despawn_check_ms = 0; do_despawn = true; }

    for (int i = 0; i < MB_MAX_ACTORS; i++) {
        mb_actor_t *a = &e->actors[i];
        if (!a->used) continue;
        const mb_pk_actor_t *ad = def_of(e, a);

        if (do_despawn && !(a->flags & MB_AF_LEAVING)) {
            bool expired = ad->lifetime_s && a->age_ms >= (uint32_t)ad->lifetime_s * 1000u;
            if (expired || (ad->despawn_cond && mb_cond(e, ad->despawn_cond))) {
                start_leaving(e, a);
                if (!a->used) continue;
            }
        }
        a->calm_ms = a->calm_ms > dt_ms ? (uint16_t)(a->calm_ms - dt_ms) : 0;
        if (do_react && ad->react_count && !a->calm_ms) check_reactions(e, a);

        a->age_ms += dt_ms;
        a->state_ms += dt_ms;
        const mb_pk_state_t *st = mb_actor_state(e, a);

        /* animation */
        const mb_pk_anim_t *an = &e->anims[st->anim];
        a->anim_ms += dt_ms;
        uint32_t fms = an->frame_ms ? an->frame_ms : 100;
        while (a->anim_ms >= fms) {
            a->anim_ms -= fms;
            if (a->anim_frame + 1 < an->frame_count) a->anim_frame++;
            else if (an->flags & MB_ANIM_LOOP) a->anim_frame = 0;
        }

        /* movement */
        if (st->move != MB_MOVE_IDLE) move_actor(e, a, st, dt_ms);

        bool arrived = a->flags & MB_AF_ARRIVED;
        if (arrived && (st->move == MB_MOVE_EXIT || st->move == MB_MOVE_FLEE)) {
            despawn(e, a);
            continue;
        }
        bool timed_out = a->state_dur_ms && a->state_ms >= a->state_dur_ms;
        if (st->move == MB_MOVE_EXIT || st->move == MB_MOVE_FLEE) continue;
        if (arrived && st->move == MB_MOVE_WANDER && !(st->flags & MB_STATE_UNTIL_ARRIVE) &&
            a->state_dur_ms && !timed_out) {
            pick_wander_target(e, a); /* keep strolling until the timer ends */
            a->flags &= (uint8_t)~MB_AF_ARRIVED;
            continue;
        }
        bool arrive_ends = st->move == MB_MOVE_SEEK || (st->flags & MB_STATE_UNTIL_ARRIVE) ||
                           !a->state_dur_ms;
        if (timed_out || (arrived && arrive_ends && st->move != MB_MOVE_IDLE)) finish_state(e, a);
    }
}

void mb_actors_sort(mb_engine_t *e) {
    uint8_t n = 0;
    for (int i = 0; i < MB_MAX_ACTORS; i++) {
        if (!e->actors[i].used) continue;
        int32_t y = e->actors[i].y;
        int j = n++;
        while (j > 0 && e->actors[e->order[j - 1]].y > y) {
            e->order[j] = e->order[j - 1];
            j--;
        }
        e->order[j] = (uint8_t)i;
    }
    e->n_order = n;
}

int mb_actor_count(const mb_engine_t *e) { return e->n_order; }

bool mb_actor_info(const mb_engine_t *e, int nth, mb_actor_info_t *out) {
    if (nth < 0 || nth >= e->n_order) return false;
    const mb_actor_t *a = &e->actors[e->order[nth]];
    if (!a->used) return false;
    const mb_pk_actor_t *ad = def_of(e, a);
    out->name = mb_str(e, ad->name_str);
    out->state = mb_str(e, mb_actor_state(e, a)->name_str);
    out->x = a->x >> 8;
    out->y = a->y >> 8;
    mb_world_to_display(e, out->x - ad->anchor_x, out->y - ad->anchor_y, &out->sx, &out->sy);
    out->w = ad->frame_w * e->scale;
    out->h = ad->frame_h * e->scale;
    return true;
}

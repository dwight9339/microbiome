/* Clock, world variables, conditions and palette (lighting). */
#include <string.h>

#include "internal.h"

/* ---- conditions --------------------------------------------------------- */

static bool clause_holds(const mb_engine_t *e, const mb_pk_clause_t *cl) {
    int32_t v = e->vars[cl->var];
    switch (cl->op) {
    case MB_OP_IN:      return v >= 0 && v < 32 && ((uint32_t)cl->a >> v) & 1u;
    case MB_OP_NOT_IN:  return !(v >= 0 && v < 32 && ((uint32_t)cl->a >> v) & 1u);
    case MB_OP_EQ:      return v == cl->a;
    case MB_OP_NE:      return v != cl->a;
    case MB_OP_LT:      return v < cl->a;
    case MB_OP_LE:      return v <= cl->a;
    case MB_OP_GT:      return v > cl->a;
    case MB_OP_GE:      return v >= cl->a;
    case MB_OP_BETWEEN:
        return cl->a <= cl->b ? (v >= cl->a && v <= cl->b) : (v >= cl->a || v <= cl->b);
    }
    return false;
}

bool mb_cond(const mb_engine_t *e, uint16_t c) {
    if (c == 0 || c >= e->n_conds) return true;
    const mb_pk_cond_t *cd = &e->conds[c];
    for (uint32_t i = 0; i < cd->clause_count; i++)
        if (!clause_holds(e, &e->clauses[cd->clause_first + i])) return false;
    return true;
}

/* ---- clock -------------------------------------------------------------- */

static uint8_t days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t k[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return k[(month - 1) % 12];
}

static void next_day(mb_engine_t *e) {
    if (++e->mday > days_in_month(e->year, e->month)) {
        e->mday = 1;
        if (++e->month > 12) { e->month = 1; e->year++; }
    }
}

static bool sync_wall(mb_engine_t *e) {
    mb_datetime_t dt;
    if (!e->host.wall_time || !e->host.wall_time(e->host.ctx, &dt)) return false;
    if (dt.month < 1 || dt.month > 12 || dt.hour > 23 || dt.minute > 59) return false;
    e->year = dt.year;
    e->month = dt.month;
    e->mday = dt.day ? dt.day : 1;
    e->day_ms = ((uint32_t)dt.hour * 3600u + dt.minute * 60u + (dt.second % 60u)) * 1000u;
    return true;
}

void mb_clock_init(mb_engine_t *e) {
    e->time_scale = 1;
    e->clock_wall = sync_wall(e);
    if (!e->clock_wall) {
        e->year = 2026;
        e->month = e->world->start_month ? e->world->start_month : 1;
        e->mday = e->world->start_day ? e->world->start_day : 1;
        e->day_ms = (uint32_t)(e->world->clock_start_minute % 1440u) * 60000u;
    }
    e->game_ms = e->day_ms;
}

void mb_clock_update(mb_engine_t *e, uint32_t dt_ms) {
    uint64_t adv;
    if (e->clock_wall) {
        adv = (uint64_t)dt_ms * e->time_scale;
    } else {
        uint32_t len = e->world->day_length_s ? e->world->day_length_s : 86400u;
        adv = (uint64_t)dt_ms * e->time_scale * 86400u / len;
    }
    e->game_ms += adv;
    uint64_t d = (uint64_t)e->day_ms + adv;
    while (d >= MB_DAY_MS) { d -= MB_DAY_MS; next_day(e); }
    e->day_ms = (uint32_t)d;

    if (e->clock_wall && e->time_scale == 1) {
        e->wall_sync_ms += dt_ms;
        if (e->wall_sync_ms >= 1000) {
            e->wall_sync_ms = 0;
            sync_wall(e);
        }
    }
}

void mb_set_time_scale(mb_engine_t *e, uint32_t scale) {
    e->time_scale = scale ? scale : 1;
}

void mb_set_day_minute(mb_engine_t *e, uint32_t minute) {
    e->clock_wall = false;
    e->day_ms = (minute % 1440u) * 60000u;
    mb_vars_update(e, true);
    mb_palette_update(e, true);
}

uint32_t mb_day_minute(const mb_engine_t *e) { return e->day_ms / 60000u; }

void mb_set_date(mb_engine_t *e, uint16_t year, uint8_t month, uint8_t day) {
    if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month)) return;
    e->year = year;
    e->month = month;
    e->mday = day;
    mb_vars_update(e, true);
}

bool mb_clock_use_wall(mb_engine_t *e) {
    e->clock_wall = sync_wall(e);
    e->wall_sync_ms = 0;
    mb_vars_update(e, true);
    mb_palette_update(e, true);
    return e->clock_wall;
}

/* ---- world variables ---------------------------------------------------- */

static int32_t eval_step(const mb_engine_t *e, const mb_pk_var_t *v) {
    const int16_t *tab = (const int16_t *)(e->blob + v->data_off);
    int32_t src = e->vars[v->param];
    int32_t out = tab[(v->count - 1) * 2 + 1]; /* wraps: before first key = last value */
    for (uint32_t i = 0; i < v->count; i++) {
        if (src >= tab[i * 2]) out = tab[i * 2 + 1];
        else break;
    }
    return out;
}

static void step_markov(mb_engine_t *e, int idx) {
    const mb_pk_var_t *v = &e->var_defs[idx];
    uint64_t now_min = e->game_ms / 60000u;
    if (e->markov_next_min[idx] == 0) e->markov_next_min[idx] = now_min + v->param;
    int steps = 0;
    while (now_min >= e->markov_next_min[idx] && steps < 8) {
        int32_t cur = mb_clamp(e->vars[idx], 0, v->count - 1);
        const uint8_t *row = e->blob + v->data_off + (uint32_t)cur * v->count;
        uint32_t total = 0;
        for (uint32_t j = 0; j < v->count; j++) total += row[j];
        if (total) {
            uint32_t r = mb_rand(e) % total;
            for (uint32_t j = 0; j < v->count; j++) {
                if (r < row[j]) { e->vars[idx] = (int32_t)j; break; }
                r -= row[j];
            }
        }
        e->markov_next_min[idx] += v->param;
        steps++;
    }
    if (now_min >= e->markov_next_min[idx]) e->markov_next_min[idx] = now_min + v->param;
}

void mb_vars_update(mb_engine_t *e, bool force) {
    bool poll_sensors = force || e->sensor_ms >= 1000;
    if (poll_sensors) e->sensor_ms = 0;
    for (uint32_t i = 0; i < e->n_vars; i++) {
        if (e->var_pinned & (1u << i)) continue;
        const mb_pk_var_t *v = &e->var_defs[i];
        if (v->sensor_str && e->host.read_sensor) {
            if (poll_sensors) {
                int32_t val;
                if (e->host.read_sensor(e->host.ctx, mb_str(e, v->sensor_str), &val)) {
                    e->vars[i] = val;
                    e->sensor_ok |= 1u << i;
                    continue;
                }
                e->sensor_ok &= ~(1u << i);
            } else if (e->sensor_ok & (1u << i)) {
                continue;
            }
        }
        switch (v->kind) {
        case MB_VAR_CONST:  e->vars[i] = (int16_t)v->param; break;
        case MB_VAR_MINUTE: e->vars[i] = (int32_t)(e->day_ms / 60000u); break;
        case MB_VAR_HOUR:   e->vars[i] = (int32_t)(e->day_ms / 3600000u); break;
        case MB_VAR_MONTH:  e->vars[i] = e->month; break;
        case MB_VAR_DAY:    e->vars[i] = e->mday; break;
        case MB_VAR_STEP:   e->vars[i] = eval_step(e, v); break;
        case MB_VAR_MARKOV: step_markov(e, (int)i); break;
        default: break;
        }
    }
}

int mb_var_find(const mb_engine_t *e, const char *name) {
    for (uint32_t i = 0; i < e->n_vars; i++)
        if (strcmp(mb_str(e, e->var_defs[i].name_str), name) == 0) return (int)i;
    return -1;
}

int32_t mb_var_get(const mb_engine_t *e, int idx) {
    return (idx >= 0 && (uint32_t)idx < e->n_vars) ? e->vars[idx] : 0;
}

const char *mb_var_name(const mb_engine_t *e, int idx) {
    return (idx >= 0 && (uint32_t)idx < e->n_vars) ? mb_str(e, e->var_defs[idx].name_str) : "";
}

int mb_var_enum_count(const mb_engine_t *e, int idx) {
    if (idx < 0 || (uint32_t)idx >= e->n_vars || !e->var_defs[idx].values_str) return 0;
    const char *s = mb_str(e, e->var_defs[idx].values_str);
    int n = 1;
    for (; *s; s++) n += (*s == '|');
    return n;
}

const char *mb_var_label(const mb_engine_t *e, int idx, char *buf, size_t n) {
    if (!n) return buf;
    buf[0] = '\0';
    if (idx < 0 || (uint32_t)idx >= e->n_vars) return buf;
    int32_t val = e->vars[idx];
    const mb_pk_var_t *v = &e->var_defs[idx];
    if (v->values_str && val >= 0) {
        const char *s = mb_str(e, v->values_str);
        for (int32_t k = 0; k < val && *s; s++)
            if (*s == '|') k++;
        size_t i = 0;
        while (s[i] && s[i] != '|' && i + 1 < n) { buf[i] = s[i]; i++; }
        buf[i] = '\0';
        if (i) return buf;
    }
    /* numeric fallback */
    char tmp[12];
    int len = 0;
    uint32_t u = val < 0 ? (uint32_t)(-(int64_t)val) : (uint32_t)val;
    do { tmp[len++] = (char)('0' + u % 10); u /= 10; } while (u && len < 11);
    size_t o = 0;
    if (val < 0 && o + 1 < n) buf[o++] = '-';
    while (len && o + 1 < n) buf[o++] = tmp[--len];
    buf[o] = '\0';
    return buf;
}

void mb_var_pin(mb_engine_t *e, int idx, int32_t value) {
    if (idx < 0 || (uint32_t)idx >= e->n_vars) return;
    e->var_pinned |= 1u << idx;
    e->vars[idx] = value;
    mb_vars_update(e, true); /* re-derive dependants */
}

void mb_var_unpin(mb_engine_t *e, int idx) {
    if (idx < 0 || (uint32_t)idx >= e->n_vars) return;
    e->var_pinned &= ~(1u << idx);
    mb_vars_update(e, true);
}

/* ---- palette ------------------------------------------------------------ */

static uint32_t to_native(uint8_t fmt, uint32_t c) {
    uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    uint16_t p565 = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    switch (fmt) {
    case MB_PIXFMT_RGB565_BE: return (uint16_t)((p565 >> 8) | (p565 << 8));
    case MB_PIXFMT_RGB565_LE: return p565;
    default: return c & 0xFFFFFFu;
    }
}

void mb_palette_update(mb_engine_t *e, bool force) {
    uint32_t sec = e->day_ms / 1000u;
    if (!force && sec == e->lut_second) return;
    e->lut_second = sec;

    uint32_t n = e->n_palettes, k = n - 1;
    for (uint32_t i = 0; i < n; i++)
        if ((uint32_t)e->palettes[i].minute * 60u <= sec) k = i;
    uint32_t nx = (k + 1) % n;
    uint32_t t0 = e->palettes[k].minute * 60u, t1 = e->palettes[nx].minute * 60u;
    uint32_t s = sec;
    if (t1 <= t0) t1 += 86400u;
    if (s < t0) s += 86400u;
    uint32_t f = (n > 1 && t1 > t0) ? (s - t0) * 256u / (t1 - t0) : 0; /* 0..256 */

    const uint32_t *a = e->palettes[k].colors, *b = e->palettes[nx].colors;
    for (int i = 0; i < 256; i++) {
        uint32_t ca = a[i], cb = b[i], out = 0;
        for (int sh = 0; sh <= 16; sh += 8) {
            int32_t va = (int32_t)((ca >> sh) & 0xFF), vb = (int32_t)((cb >> sh) & 0xFF);
            out |= (uint32_t)(va + (((vb - va) * (int32_t)f) >> 8)) << sh;
        }
        e->lut[i] = to_native(e->format, out);
    }
}

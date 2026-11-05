/*
 * microbiome engine — public API
 *
 * The core is hardware-agnostic C11 with no heap allocation and no platform
 * dependencies. A platform ("host") owns the main loop:
 *
 *     mb_engine_init(&eng, pack, pack_size, &cfg);
 *     for (;;) {
 *         mb_engine_update(&eng, dt_ms, input_bits);
 *         for (y = 0; y < height; y += strip_h)
 *             mb_engine_render_strip(&eng, y, strip_h, buf, stride);  // then push buf
 *     }
 *
 * The engine renders horizontal strips straight into the display's native
 * pixel format, so a host needs only a strip-sized buffer (no framebuffer).
 */
#ifndef MB_MB_H
#define MB_MB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mb/pack_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- compile-time capacities (override with -D for bigger/smaller targets) */
#ifndef MB_MAX_ACTORS
#define MB_MAX_ACTORS 32
#endif
#ifndef MB_MAX_PARTICLES
#define MB_MAX_PARTICLES 256
#endif
#ifndef MB_MAX_VARS
#define MB_MAX_VARS 32
#endif
#ifndef MB_MAX_SPAWNS
#define MB_MAX_SPAWNS 64
#endif
#ifndef MB_MAX_LAYERS
#define MB_MAX_LAYERS 16
#endif
#ifndef MB_MAX_PSYS
#define MB_MAX_PSYS 8
#endif
#ifndef MB_MAX_OBSTACLES
#define MB_MAX_OBSTACLES 8
#endif
#ifndef MB_MAX_VIEW_W
#define MB_MAX_VIEW_W 640 /* viewport width in art pixels (display width / scale) */
#endif

/* Platforms can place the per-pixel hot path in fast memory, e.g. on RP2350:
 * -DMB_HOT='__attribute__((section(".time_critical.mb")))' (runs from SRAM). */
#ifndef MB_HOT
#define MB_HOT
#endif

/* ---- platform-facing types ---------------------------------------------- */

typedef enum {
    MB_PIXFMT_RGB565_BE = 0, /* 16-bit, high byte first in memory (SPI LCDs) */
    MB_PIXFMT_RGB565_LE,     /* 16-bit, native little-endian */
    MB_PIXFMT_XRGB8888,      /* 32-bit 0x00RRGGBB (desktop) */
} mb_pixfmt_t;

/* Logical inputs. Hosts map whatever hardware they have onto these. */
enum {
    MB_IN_LEFT  = 1u << 0,
    MB_IN_RIGHT = 1u << 1,
    MB_IN_UP    = 1u << 2,
    MB_IN_DOWN  = 1u << 3,
    MB_IN_A     = 1u << 4,
    MB_IN_B     = 1u << 5,
};

typedef struct {
    uint16_t year;
    uint8_t  month;  /* 1..12 */
    uint8_t  day;    /* 1..31 */
    uint8_t  hour, minute, second;
} mb_datetime_t;

/* Optional host services. Any callback may be NULL. */
typedef struct {
    void *ctx;
    /* Real wall-clock time (RTC, NTP, OS). Return false if unknown. */
    bool (*wall_time)(void *ctx, mb_datetime_t *out);
    /* Named sensor reading, e.g. "light", "temperature". Return false if absent. */
    bool (*read_sensor)(void *ctx, const char *name, int32_t *out);
} mb_host_t;

typedef struct {
    uint16_t    width, height; /* display size in physical pixels */
    mb_pixfmt_t format;
    uint8_t     scale;         /* integer pixel scale; 0 = auto from world size */
    uint32_t    seed;
    uint32_t    warmup_s;      /* seconds simulated at init so the scene isn't empty */
    mb_host_t   host;
} mb_config_t;

typedef enum {
    MB_OK = 0,
    MB_ERR_BAD_MAGIC,
    MB_ERR_BAD_VERSION,
    MB_ERR_TRUNCATED,
    MB_ERR_BAD_SECTION,
    MB_ERR_BAD_INDEX,
    MB_ERR_CAPACITY,
    MB_ERR_DISPLAY,
} mb_err_t;

const char *mb_err_str(mb_err_t err);

/* ---- engine state (treat as opaque; public only for static allocation) -- */

typedef struct {
    int32_t  x, y;          /* world position of feet, Q8 */
    int32_t  tx, ty;        /* movement target, Q8 */
    int32_t  wx, wy;        /* next waypoint on the way to the target, Q8 */
    int32_t  threat_x;      /* Q8 */
    uint32_t state_ms, state_dur_ms, anim_ms, age_ms;
    uint16_t archetype;
    uint16_t spawn_rule;
    uint16_t id;
    uint16_t calm_ms;       /* reactions suppressed while > 0 */
    uint8_t  used;
    uint8_t  state;         /* local state index */
    uint8_t  anim_frame;
    uint8_t  flags;
} mb_actor_t;

typedef struct {
    int32_t  x, y;          /* Q8 */
    int16_t  vx, vy;        /* px/s * 16 */
    uint8_t  phase;
    uint8_t  sys;
    uint16_t reserved;
} mb_particle_t;

typedef struct mb_engine {
    /* pack, read in place */
    const uint8_t          *pack;
    const mb_pk_world_t    *world;
    const mb_pk_palette_t  *palettes;  uint32_t n_palettes;
    const mb_pk_image_t    *images;    uint32_t n_images;
    const mb_pk_layer_t    *layers;    uint32_t n_layers;
    const mb_pk_anim_t     *anims;     uint32_t n_anims;
    const uint16_t         *frames;    uint32_t n_frames;
    const mb_pk_actor_t    *actor_defs; uint32_t n_actor_defs;
    const mb_pk_state_t    *states;    uint32_t n_states;
    const mb_pk_next_t     *nexts;     uint32_t n_nexts;
    const mb_pk_reaction_t *reactions; uint32_t n_reactions;
    const mb_pk_cond_t     *conds;     uint32_t n_conds;
    const mb_pk_clause_t   *clauses;   uint32_t n_clauses;
    const mb_pk_var_t      *var_defs;  uint32_t n_vars;
    const mb_pk_spawn_t    *spawns;    uint32_t n_spawns;
    const mb_pk_lane_t     *lanes;     uint32_t n_lanes;
    const mb_pk_place_t    *places;    uint32_t n_places;
    const mb_pk_particle_t *psys;      uint32_t n_psys;
    const mb_pk_obstacle_t *obstacles; uint32_t n_obstacles;
    const uint8_t          *blob;      uint32_t blob_size;
    const char             *strings;   uint32_t strings_size;

    /* display / viewport */
    uint16_t disp_w, disp_h, view_w, view_h;
    uint8_t  scale, format;
    mb_host_t host;

    /* clock */
    uint64_t game_ms;        /* total simulated time */
    uint32_t day_ms;         /* time of day */
    uint32_t time_scale;     /* debug fast-forward multiplier */
    uint32_t wall_sync_ms;
    uint16_t year;
    uint8_t  month, mday;
    bool     clock_wall;     /* following the host's wall clock */

    /* world variables */
    int32_t  vars[MB_MAX_VARS];
    uint32_t var_pinned;     /* bitmask of debug-overridden vars */
    uint64_t markov_next_min[MB_MAX_VARS];
    uint32_t sensor_ms, sensor_ok;

    /* palette */
    uint32_t lut[256];
    uint32_t lut_second;

    /* camera (top-left of viewport, Q8) */
    int32_t  cam_x, cam_y, cam_max_x, cam_max_y;
    int32_t  layer_px[MB_MAX_LAYERS], layer_py[MB_MAX_LAYERS]; /* resolved parallax, Q8 */
    uint32_t idle_ms, drift_repick_ms;
    uint16_t drift_actor_id;
    int8_t   drift_dir;

    /* spawning */
    uint32_t spawn_timer_ms;
    uint32_t spawn_cd_ms[MB_MAX_SPAWNS];
    uint8_t  spawn_alive[MB_MAX_SPAWNS];

    /* actors */
    mb_actor_t actors[MB_MAX_ACTORS];
    uint8_t  order[MB_MAX_ACTORS];  /* draw order, sorted by feet y */
    uint8_t  n_order;
    uint8_t  max_actors;
    uint16_t next_id;
    uint32_t react_ms, despawn_check_ms;

    /* particles */
    mb_particle_t particles[MB_MAX_PARTICLES];
    uint16_t psys_base[MB_MAX_PSYS], psys_cap[MB_MAX_PSYS], psys_active[MB_MAX_PSYS];
    uint32_t psys_ramp_ms;
    bool     psys_layered;   /* any system drawn below the top? */

    /* misc */
    uint32_t real_ms;
    uint32_t rng;
    uint8_t  line[MB_MAX_VIEW_W];
} mb_engine_t;

/* ---- lifecycle ---------------------------------------------------------- */

mb_err_t mb_engine_init(mb_engine_t *e, const void *pack, size_t pack_size, const mb_config_t *cfg);
void     mb_engine_update(mb_engine_t *e, uint32_t dt_ms, uint32_t input);
/* Simulate `seconds` in 100 ms steps without touching the camera or its idle
 * timer: the warm-up mb_engine_init runs, for hosts that set the time or pins
 * after init (with cfg.warmup_s = 0) and want the warm-up to reflect them. */
void     mb_engine_warmup(mb_engine_t *e, uint32_t seconds);

/* Render display rows [y0, y0+h) into `out` (native format, `stride` bytes per row). */
void     mb_engine_render_strip(mb_engine_t *e, int y0, int h, void *out, size_t stride);

static inline size_t mb_pixfmt_bytes(mb_pixfmt_t f) { return f == MB_PIXFMT_XRGB8888 ? 4 : 2; }

/* ---- introspection & debug (simulator, overlays, tests) ---------------- */

const char *mb_str(const mb_engine_t *e, uint32_t off);

void     mb_set_time_scale(mb_engine_t *e, uint32_t scale);
void     mb_set_day_minute(mb_engine_t *e, uint32_t minute); /* switches to simulated clock */
uint32_t mb_day_minute(const mb_engine_t *e);
void     mb_set_date(mb_engine_t *e, uint16_t year, uint8_t month, uint8_t day);
/* Re-read the host wall clock (e.g. after setting the RTC); false if unavailable. */
bool     mb_clock_use_wall(mb_engine_t *e);

int         mb_var_find(const mb_engine_t *e, const char *name);   /* -1 if absent */
int32_t     mb_var_get(const mb_engine_t *e, int idx);
const char *mb_var_name(const mb_engine_t *e, int idx);
/* Writes the enum label of the var's current value into buf; returns buf. */
const char *mb_var_label(const mb_engine_t *e, int idx, char *buf, size_t n);
int         mb_var_enum_count(const mb_engine_t *e, int idx);      /* 0 if not an enum */
/* Label for any value of the var (not just the current one); numeric if not an enum label. */
const char *mb_var_value_label(const mb_engine_t *e, int idx, int32_t value, char *buf, size_t n);
void        mb_var_pin(mb_engine_t *e, int idx, int32_t value);    /* debug override */
void        mb_var_unpin(mb_engine_t *e, int idx);
bool        mb_var_pinned(const mb_engine_t *e, int idx);

typedef struct {
    const char *name, *state;
    int x, y;              /* feet, world px */
    int sx, sy, w, h;      /* sprite rect, display px */
    int id;                /* unique while alive; stable across frames */
    int type;              /* actor definition, in pack order */
    int state_index;       /* local state, in pack order */
    int spawn_rule;        /* in pack order */
    int tx, ty;            /* movement target, world px */
    uint32_t state_ms;     /* time spent in the current state */
    uint32_t state_dur_ms; /* planned duration; 0 = until the movement completes */
    uint32_t age_ms;
    bool leaving;          /* despawning (in its leave state) */
    bool face_left;
} mb_actor_info_t;

int  mb_actor_count(const mb_engine_t *e);
bool mb_actor_info(const mb_engine_t *e, int nth, mb_actor_info_t *out);
/* World px -> display px for the current camera. */
void mb_world_to_display(const mb_engine_t *e, int wx, int wy, int *dx, int *dy);
/* Point the camera so world (x, y) is centred. */
void mb_camera_center(mb_engine_t *e, int wx, int wy);
/* The viewport in world px: top-left corner and size. */
void mb_camera_view(const mb_engine_t *e, int *x, int *y, int *w, int *h);
void mb_get_date(const mb_engine_t *e, uint16_t *year, uint8_t *month, uint8_t *day);

/* Why a spawn rule is or isn't eligible right now (bitmask; 0 = eligible). */
enum {
    MB_SPAWN_BLOCK_COND     = 1u << 0, /* its `when` condition is false */
    MB_SPAWN_BLOCK_MAX      = 1u << 1, /* `max` of its actors are alive */
    MB_SPAWN_BLOCK_COOLDOWN = 1u << 2, /* still cooling down */
    MB_SPAWN_BLOCK_CAP      = 1u << 3, /* the world's max_actors is reached */
};

typedef struct {
    const char *actor;     /* actor definition name */
    uint32_t blocked;      /* MB_SPAWN_BLOCK_* */
    int fail_var;          /* with BLOCK_COND: the first var whose clause fails, else -1 */
    int alive, max_alive;
    uint32_t cooldown_ms;  /* remaining */
    int weight;
} mb_spawn_info_t;

int      mb_spawn_count(const mb_engine_t *e);
bool     mb_spawn_info(const mb_engine_t *e, int rule, mb_spawn_info_t *out);
uint32_t mb_spawn_next_tick_ms(const mb_engine_t *e);  /* until the next spawn roll */

#ifdef __cplusplus
}
#endif
#endif /* MB_MB_H */

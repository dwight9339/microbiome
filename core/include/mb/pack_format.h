/*
 * microbiome pack binary format (version 1)
 *
 * A pack is a single little-endian blob produced by tools/packc/packc.py.
 * The engine reads it in place (e.g. straight from memory-mapped flash), so
 * every struct here is naturally aligned and every section starts on a
 * 4-byte boundary. Any change to these structs MUST be mirrored in packc.py
 * and MB_PACK_VERSION bumped.
 *
 * Index conventions:
 *   - string offsets point into MB_SEC_STRINGS; offset 0 is the empty string
 *   - condition index 0 is "always true"
 *   - blob offsets point into MB_SEC_BLOB
 */
#ifndef MB_PACK_FORMAT_H
#define MB_PACK_FORMAT_H

#include <stdint.h>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "microbiome packs are little-endian; big-endian hosts need a byte-swapping loader"
#endif

#define MB_PACK_MAGIC   0x4B50424Du /* "MBPK" */
#define MB_PACK_VERSION 2u

enum {
    MB_SEC_WORLD = 0,
    MB_SEC_PALETTES,
    MB_SEC_IMAGES,
    MB_SEC_LAYERS,
    MB_SEC_ANIMS,
    MB_SEC_FRAMES,
    MB_SEC_ACTORS,
    MB_SEC_STATES,
    MB_SEC_NEXT,
    MB_SEC_REACTIONS,
    MB_SEC_CONDS,
    MB_SEC_CLAUSES,
    MB_SEC_VARS,
    MB_SEC_SPAWNS,
    MB_SEC_LANES,
    MB_SEC_PLACES,
    MB_SEC_PARTICLES,
    MB_SEC_OBSTACLES,
    MB_SEC_BLOB,
    MB_SEC_STRINGS,
    MB_SEC_COUNT
};

typedef struct {
    uint32_t offset; /* bytes from start of pack */
    uint32_t count;  /* number of elements (bytes for BLOB/STRINGS) */
} mb_pk_section_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t section_count; /* may exceed MB_SEC_COUNT in newer packs */
    uint32_t total_size;
    uint32_t reserved;
    mb_pk_section_t sections[MB_SEC_COUNT];
} mb_pk_header_t;

/* ---- world -------------------------------------------------------------- */

#define MB_WORLD_FLAG_NONE 0u

typedef struct {
    uint16_t width, height;      /* world size in art pixels */
    uint16_t flags;
    uint8_t  max_actors;
    uint8_t  spawn_chance;       /* percent per spawn tick */
    uint16_t spawn_interval_ds;  /* deciseconds between spawn ticks */
    uint16_t pan_speed;          /* camera pan speed, px/s */
    int16_t  cam_x, cam_y;       /* initial camera centre */
    uint16_t idle_drift_s;       /* seconds without input before autopilot; 0 = off */
    uint16_t clock_start_minute; /* simulated clock start (minute of day) */
    uint32_t day_length_s;       /* simulated day length in real seconds */
    uint8_t  clear_color;        /* palette index for letterbox / empty pixels */
    uint8_t  start_month;        /* simulated clock start date */
    uint8_t  start_day;
    uint8_t  reserved;
    uint32_t name_str;
} mb_pk_world_t;

/* ---- palettes ----------------------------------------------------------- */

/* One lighting keyframe. The engine blends between consecutive keyframes by
 * time of day. Colours are 0x00RRGGBB; index 0 is transparent. */
typedef struct {
    uint16_t minute;
    uint16_t reserved;
    uint32_t colors[256];
} mb_pk_palette_t;

/* ---- images (8bpp indexed, row-major, data in BLOB) ---------------------- */

typedef struct {
    uint16_t w, h;
    uint32_t data_off;
} mb_pk_image_t;

/* ---- layers ------------------------------------------------------------- */

#define MB_LAYER_REPEAT_X 0x01u
#define MB_LAYER_FRONT    0x02u   /* drawn above actors */
#define MB_PARALLAX_AUTO  (-1)    /* edges of layer line up with edges of world */

typedef struct {
    uint16_t image;
    uint16_t flags;
    int16_t  x, y;                 /* offset in world px */
    int16_t  parallax_x_q8;        /* 256 = moves with world; MB_PARALLAX_AUTO */
    int16_t  parallax_y_q8;
    uint32_t name_str;
} mb_pk_layer_t;

/* ---- animations --------------------------------------------------------- */

#define MB_ANIM_LOOP 0x01u

typedef struct {
    uint16_t frame_first;  /* into FRAMES */
    uint16_t frame_count;
    uint16_t frame_ms;
    uint16_t flags;
} mb_pk_anim_t;

/* FRAMES is an array of uint16_t sprite-sheet cell indices. */

/* ---- actors ------------------------------------------------------------- */

#define MB_ACTOR_FACES_LEFT 0x01u  /* source art faces left */
#define MB_NO_LANE          0xFFu
#define MB_NO_STATE         0xFFu

typedef struct {
    uint32_t name_str;
    uint32_t tags;           /* bitmask of pack-defined tags */
    uint16_t image;          /* sprite sheet */
    uint16_t frame_w;
    uint16_t frame_h;
    int16_t  anchor_x;       /* "feet" point within a frame */
    int16_t  anchor_y;
    uint16_t speed_q4;       /* base speed, px/s * 16 */
    uint16_t state_first;    /* into STATES; first state is the initial one */
    uint8_t  state_count;
    uint8_t  lane;           /* MB_NO_LANE or index into LANES */
    uint16_t react_first;    /* into REACTIONS */
    uint8_t  react_count;
    uint8_t  leave_state;    /* local state index used to leave, or MB_NO_STATE */
    uint16_t despawn_cond;   /* 0 = never (NB: not "always") */
    uint16_t lifetime_s;     /* 0 = unlimited */
    uint16_t flags;
    uint16_t reserved;
} mb_pk_actor_t;

enum {
    MB_MOVE_IDLE = 0,
    MB_MOVE_WANDER,  /* stroll to random points within the lane */
    MB_MOVE_SEEK,    /* go to a place offering move_param affordances */
    MB_MOVE_EXIT,    /* walk off the nearest world edge, then despawn */
    MB_MOVE_FLEE,    /* run off the edge away from the threat, then despawn */
};

#define MB_STATE_UNTIL_ARRIVE 0x01u  /* wander: end on arrival even if a duration is set */
#define MB_STATE_DESPAWN_END  0x02u  /* despawn when the state ends (e.g. into a burrow) */

typedef struct {
    uint32_t name_str;
    uint16_t anim;
    uint8_t  move;
    uint8_t  flags;
    uint16_t dur_min_ds;     /* deciseconds; 0/0 = until movement completes */
    uint16_t dur_max_ds;
    uint16_t next_first;     /* into NEXT */
    uint8_t  next_count;
    uint8_t  speed_x16;      /* speed multiplier * 16 */
    uint32_t move_param;     /* SEEK: affordance mask */
} mb_pk_state_t;

typedef struct {
    uint8_t  to;             /* local state index */
    uint8_t  weight;
    uint16_t cond;
} mb_pk_next_t;

typedef struct {
    uint32_t tags;           /* react to any actor carrying any of these */
    uint16_t radius;         /* px */
    uint8_t  goto_state;     /* local state index */
    uint8_t  flags;
} mb_pk_reaction_t;

/* ---- conditions --------------------------------------------------------- */

typedef struct {
    uint16_t clause_first;
    uint16_t clause_count;   /* all clauses must hold (AND) */
} mb_pk_cond_t;

enum {
    MB_OP_IN = 0,   /* (1 << v) & a */
    MB_OP_NOT_IN,
    MB_OP_EQ,
    MB_OP_NE,
    MB_OP_LT,
    MB_OP_LE,
    MB_OP_GT,
    MB_OP_GE,
    MB_OP_BETWEEN,  /* a <= v <= b, wrapping when a > b (e.g. 22:00..04:00) */
};

typedef struct {
    uint8_t  var;
    uint8_t  op;
    uint16_t reserved;
    int32_t  a, b;
} mb_pk_clause_t;

/* ---- world variables ---------------------------------------------------- */

enum {
    MB_VAR_CONST = 0,  /* param = value */
    MB_VAR_MINUTE,     /* minute of day 0..1439 */
    MB_VAR_HOUR,       /* 0..23 */
    MB_VAR_MONTH,      /* 1..12 */
    MB_VAR_DAY,        /* day of month 1..31 */
    MB_VAR_STEP,       /* step function of var[param]: count (int16 key, int16 value) pairs */
    MB_VAR_MARKOV,     /* count states, param = step minutes, count*count uint8 weights */
};

typedef struct {
    uint32_t name_str;
    uint32_t sensor_str;   /* if non-zero and the host provides it, overrides the value */
    uint32_t values_str;   /* "a|b|c" names for enum values, or 0 */
    uint32_t data_off;     /* into BLOB */
    uint8_t  kind;
    uint8_t  count;
    uint16_t param;
} mb_pk_var_t;

/* ---- spawning ----------------------------------------------------------- */

enum {
    MB_ENTER_EDGE = 0,   /* walk in from the left or right world edge */
    MB_ENTER_PLACE,      /* appear at a place offering affordance bit enter_param */
    MB_ENTER_ANYWHERE,   /* appear in the lane, preferably off-camera */
};

typedef struct {
    uint16_t actor;
    uint16_t cond;
    uint16_t weight;
    uint8_t  max_alive;
    uint8_t  enter;
    uint16_t cooldown_s;
    uint16_t enter_param;
    uint8_t  group_min, group_max;
    uint16_t reserved;
} mb_pk_spawn_t;

typedef struct {
    int16_t  y_min, y_max;   /* feet y band */
    uint32_t name_str;
} mb_pk_lane_t;

typedef struct {
    int16_t  x, y;
    uint32_t affordances;
    uint32_t name_str;
    uint16_t spread;         /* visitors pick a spot within +/- spread px in x */
    uint16_t reserved;
} mb_pk_place_t;

enum {
    MB_OBSTACLE_ELLIPSE = 0,  /* ellipse inscribed in the box */
};
#define MB_OBSTACLE_MAX_SIZE 1024 /* px per side; keeps the path maths in int64 */

/* An area actors in `lane` walk around (a pond, a boulder). */
typedef struct {
    int16_t  x0, y0, x1, y1; /* bounding box, world px, x0 < x1 and y0 < y1 */
    uint8_t  lane;
    uint8_t  shape;
    uint16_t reserved;
    uint32_t name_str;
} mb_pk_obstacle_t;

/* ---- particles ---------------------------------------------------------- */

#define MB_PART_BLINK  0x01u
#define MB_PART_WANDER 0x02u
#define MB_PART_TOP    0xFFu   /* after_layer: above everything */

typedef struct {
    uint16_t cond;
    uint16_t count;
    uint8_t  color;          /* palette index */
    uint8_t  flags;
    uint8_t  size;           /* px */
    uint8_t  wobble;         /* horizontal sway amplitude, px */
    int16_t  vx_min, vx_max; /* px/s * 16 */
    int16_t  vy_min, vy_max;
    int16_t  y_min, y_max;   /* world y band */
    uint8_t  after_layer;    /* drawn right after this layer, or MB_PART_TOP */
    uint8_t  reserved[3];
} mb_pk_particle_t;

/* Compile-time layout checks: packc.py relies on these exact sizes. */
#define MB_STATIC_ASSERT(c, m) typedef char mb_static_assert_##m[(c) ? 1 : -1]
MB_STATIC_ASSERT(sizeof(mb_pk_header_t) == 16 + 8 * MB_SEC_COUNT, header);
MB_STATIC_ASSERT(sizeof(mb_pk_world_t) == 32, world);
MB_STATIC_ASSERT(sizeof(mb_pk_palette_t) == 1028, palette);
MB_STATIC_ASSERT(sizeof(mb_pk_image_t) == 8, image);
MB_STATIC_ASSERT(sizeof(mb_pk_layer_t) == 16, layer);
MB_STATIC_ASSERT(sizeof(mb_pk_anim_t) == 8, anim);
MB_STATIC_ASSERT(sizeof(mb_pk_actor_t) == 36, actor);
MB_STATIC_ASSERT(sizeof(mb_pk_state_t) == 20, state);
MB_STATIC_ASSERT(sizeof(mb_pk_next_t) == 4, next);
MB_STATIC_ASSERT(sizeof(mb_pk_reaction_t) == 8, reaction);
MB_STATIC_ASSERT(sizeof(mb_pk_cond_t) == 4, cond);
MB_STATIC_ASSERT(sizeof(mb_pk_clause_t) == 12, clause);
MB_STATIC_ASSERT(sizeof(mb_pk_var_t) == 20, var);
MB_STATIC_ASSERT(sizeof(mb_pk_spawn_t) == 16, spawn);
MB_STATIC_ASSERT(sizeof(mb_pk_lane_t) == 8, lane);
MB_STATIC_ASSERT(sizeof(mb_pk_place_t) == 16, place);
MB_STATIC_ASSERT(sizeof(mb_pk_obstacle_t) == 16, obstacle);
MB_STATIC_ASSERT(sizeof(mb_pk_particle_t) == 24, particle);

#endif /* MB_PACK_FORMAT_H */

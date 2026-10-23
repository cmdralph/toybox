/*
 * config.h - every tunable constant in one place.
 *
 * Gameplay numbers live here so balancing never means hunting through
 * module code. Values are grouped by system.
 */
#ifndef PZ_CONFIG_H
#define PZ_CONFIG_H

#define PZ_TITLE            "PATIENT ZERO"
#define PZ_VERSION          "1.0.0"

/* ---- window / timing ---------------------------------------------------- */
#define WINDOW_W            1280
#define WINDOW_H            720
#define WINDOW_MIN_W        960
#define WINDOW_MIN_H        540
#define SIM_HZ              60
#define SIM_DT              (1.0f / (float)SIM_HZ)
#define MAX_SIM_STEPS       5          /* spiral-of-death guard               */
#define MAX_FRAME_DT        0.10f
#define CAM_NEAR            0.08
#define CAM_FAR             900.0

/* ---- world volume (signed-distance grid) -------------------------------- */
#define GRID_NX             160        /* cells per axis                      */
#define GRID_NY             64
#define GRID_NZ             160
#define GRID_CELL           2.5f       /* world units per cell                */
#define WORLD_SX            (GRID_NX * GRID_CELL)
#define WORLD_SY            (GRID_NY * GRID_CELL)
#define WORLD_SZ            (GRID_NZ * GRID_CELL)
#define WORLD_MIN_X         (-WORLD_SX * 0.5f)
#define WORLD_MIN_Y         (-WORLD_SY * 0.5f)
#define WORLD_MIN_Z         (-WORLD_SZ * 0.5f)
#define CHUNK_CELLS         32         /* surface-nets mesh chunk size        */
#define MAX_SURF_VERTS      600000
#define MAX_SHAPES          512        /* tubes/spheres stamped per region    */
#define MAX_PATH            160
#define MAX_BRANCHES        8
#define FLOW_CELL           10.0f
#define FLOW_NX             ((int)(WORLD_SX / FLOW_CELL) + 1)
#define FLOW_NY             ((int)(WORLD_SY / FLOW_CELL) + 1)
#define FLOW_NZ             ((int)(WORLD_SZ / FLOW_CELL) + 1)

/* ---- pools --------------------------------------------------------------- */
#define MAX_AGENTS          4096
#define MAX_PROJECTILES     1024
#define MAX_PICKUPS         512
#define MAX_HAZARDS         160
#define MAX_PARTICLES       14000
#define MAX_POPUPS          96
#define MAX_TOASTS          6
#define MAX_LIGHTS          16
#define MAX_LIGHT_CANDS     320
#define MAX_DECOR           256
#define MAX_BOSS_SEGMENTS   14

/* ---- spatial hash -------------------------------------------------------- */
#define HASH_CELL           8.0f
#define HASH_NX             50         /* WORLD_SX / HASH_CELL                */
#define HASH_NY             20
#define HASH_NZ             50

/* ---- player -------------------------------------------------------------- */
#define STAGE_COUNT         5
#define PLAYER_DRAG         2.4f       /* fluid drag (1/s)                    */
#define PLAYER_DASH_TIME    0.28f
#define PLAYER_DASH_IFRAMES 0.22f
#define PLAYER_REGEN_DELAY  5.0f
#define INFECT_RANGE        3.2f       /* extra reach beyond radii           */
#define RHYTHM_HZ           1.15f      /* injection pulse frequency          */
#define RHYTHM_WINDOW       0.16f      /* sweet-spot width (fraction)        */
#define RHYTHM_GOOD_BONUS   0.18f
#define RHYTHM_BAD_PENALTY  0.07f
#define INJECT_BASE_TIME    3.4f       /* seconds to infect at 1x speed      */
#define CLOAK_COOLDOWN      14.0f
#define CLOAK_ATP_DRAIN     4.0f
#define SWARM_COOLDOWN      3.0f
#define ANTIBODY_SLOW       0.06f      /* per stuck antibody                 */
#define ANTIBODY_AMP        0.05f      /* damage amplification per antibody  */
#define ANTIBODY_MAX        10
#define ENGULF_TIME         1.6f

/* ---- infection / alert --------------------------------------------------- */
#define BOSS_INFECTION_PCT  0.60f
#define ALERT_MAX           100.0f
#define ALERT_SEEN_RATE     7.0f       /* per second while observed          */
#define ALERT_INFECT        4.0f
#define ALERT_KILL          1.5f
#define ALERT_DECAY         3.0f
#define ALERT_DECAY_DELAY   3.5f
#define HOST_GM_REWARD      8
#define HOST_EP_REWARD      3

/* ---- immune memory ------------------------------------------------------- */
#define IMMUNE_ANNOUNCE_1   0.25f
#define IMMUNE_ANNOUNCE_2   0.50f
#define IMMUNE_ANNOUNCE_3   0.75f
#define IMMUNE_DECAY        0.006f     /* per second when unused             */
#define IMMUNE_CARRY        0.40f      /* fraction carried to next region    */

/* ---- enemies ------------------------------------------------------------- */
#define ENEMY_PERCEPT_TICK  0.22f
#define ENEMY_GROUP_ALERT   28.0f
#define ENEMY_SPAWN_MIN_D   45.0f
#define ENEMY_SPAWN_MAX_D   95.0f
#define DESPAWN_DIST        190.0f

/* ---- rendering ----------------------------------------------------------- */
#define BLOOM_THRESHOLD     1.25f
#define INSTANCE_CAP        4096       /* instances per instanced draw       */
#define CAVE_DRAW_DIST      360.0f
#define AGENT_DRAW_DIST     240.0f

/* ---- save ---------------------------------------------------------------- */
#define SAVE_MAGIC          0x505A5356u   /* "PZSV" */
#define SAVE_VERSION        4

#endif

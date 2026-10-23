/*
 * game.h - shared types and the Game singleton.
 *
 * All simulation state lives in one `Game G`. Large GPU/audio/particle
 * resources are owned by their modules (clearly named module singletons)
 * so this header stays the single source of truth for gameplay data.
 */
#ifndef PZ_GAME_H
#define PZ_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/config.h"

/* ------------------------------------------------------------------------- */
/* Basic helpers                                                              */
/* ------------------------------------------------------------------------- */

typedef struct { uint64_t state; uint64_t inc; } Rng;

/* Generational handle into the agent pool. idx < 0 means "none". */
typedef struct { int32_t idx; uint16_t gen; } AgentRef;
#define REF_NONE ((AgentRef){ -1, 0 })

/* ------------------------------------------------------------------------- */
/* Enumerations                                                               */
/* ------------------------------------------------------------------------- */

typedef enum {
    GS_BOOT = 0, GS_MENU, GS_NEWGAME, GS_LOADING, GS_PLAY, GS_PAUSE, GS_MUTATION,
    GS_EVOLVE, GS_BOSS_INTRO, GS_REGION_CLEAR, GS_GAMEOVER, GS_VICTORY,
    GS_SETTINGS, GS_BESTIARY, GS_CREDITS, GS_PHOTO, GS_COUNT
} GameState;

typedef enum { MODE_CAMPAIGN = 0, MODE_OUTBREAK } RunMode;
typedef enum { DIFF_CAUTIOUS = 0, DIFF_STANDARD, DIFF_PANDEMIC, DIFF_COUNT } Difficulty;
typedef enum { REGION_BLOOD = 0, REGION_LUNGS, REGION_GUT, REGION_LYMPH, REGION_BRAIN, REGION_COUNT } RegionId;

typedef enum {
    AK_NONE = 0,
    AK_DRIFTER,      /* ambient region cell (RBC, surfactant, chyme, ...)    */
    AK_PLATELET,     /* small ambient fragment                               */
    AK_HOST,         /* infectable host cell                                 */
    AK_BACTERIA,     /* gut flora: neutral, ally with Symbiosis              */
    AK_MINION,       /* player virion                                        */
    AK_DECOY,        /* stealth decoy                                        */
    AK_NEUTROPHIL,
    AK_MACROPHAGE,
    AK_NK,
    AK_TCELL,
    AK_BCELL,
    AK_ANTIBODY,
    AK_BOSS_PART,    /* shield nodes, worm segments                          */
    AK_COUNT
} AgentKind;

typedef enum { TEAM_NEUTRAL = 0, TEAM_PLAYER, TEAM_IMMUNE } Team;

typedef enum {
    AI_IDLE = 0, AI_PATROL, AI_INVESTIGATE, AI_CHASE, AI_ATTACK, AI_FLEE,
    AI_STUNNED, AI_WINDUP, AI_ENGULF, AI_RETURN
} AiState;

/* agent flags */
#define AF_ATTACHED   0x0001   /* anchored to cave wall                      */
#define AF_INFECTED   0x0002
#define AF_SHIELDED   0x0004   /* immune adaptation: toxin shield            */
#define AF_ELITE      0x0008
#define AF_ALERTED    0x0010
#define AF_STUCK      0x0020   /* antibody stuck to player                   */
#define AF_SEEN       0x0040   /* codex-registered this region               */
#define AF_TIER2      0x0080   /* elite minion                               */
#define AF_CMD        0x0100   /* minion following an explicit command       */
#define AF_BURSTSPAWN 0x0200   /* minion spawned by burst replication        */
#define AF_ALLY       0x0400   /* allied bacteria                            */

typedef enum { PT_TOXIN = 0, PT_ACID, PT_PERFORIN, PT_BOSS_ORB, PT_MUCUS, PT_ACIDGLOB, PT_BOLT, PT_COUNT } ProjType;
typedef enum { PK_GM = 0, PK_ATP, PK_BIOMASS, PK_CACHE, PK_POD, PK_COUNT } PickupType;

typedef enum {
    HZ_NONE = 0, HZ_ACID_POOL, HZ_MUCUS, HZ_CILIA, HZ_COMPLEMENT, HZ_ANTIBIOTIC,
    HZ_STORM, HZ_CLOT, HZ_TOXIN_CLOUD, HZ_VORTEX, HZ_SHOCKWAVE, HZ_COUNT
} HazardType;

typedef enum {
    EV_NONE = 0, EV_FEVER, EV_CLOTTING, EV_ANTIBIOTIC, EV_PERISTALSIS, EV_STORM,
    EV_COUGH, EV_SURGE, EV_COUNT
} EventType;

typedef enum { BOSS_SENTINEL = 0, BOSS_WARDEN, BOSS_LEVIATHAN, BOSS_CITADEL, BOSS_OVERMIND, BOSS_COUNT } BossType;

typedef enum {
    BP_NONE = 0, BP_RING, BP_VOLLEY, BP_SUMMON, BP_SHOCKWAVE, BP_VORTEX, BP_CHARGE,
    BP_STORM, BP_MUCUS, BP_ACID, BP_SPIRAL, BP_TELEPORT, BP_SHIELD, BP_GUST, BP_COUNT
} BossPattern;

typedef enum { IM_TOXIN = 0, IM_DASH, IM_SWARM, IM_STEALTH, IM_BURST, IM_COUNT } ImmuneCat;
typedef enum { BR_MORPH = 0, BR_REPL, BR_TOXIN, BR_STEALTH, BR_SWARM, BR_COUNT } MutBranch;

typedef enum {
    ACT_FORWARD = 0, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN,
    ACT_DASH, ACT_BURST, ACT_SWARM, ACT_CLOAK, ACT_MUTATIONS, ACT_COUNT
} Action;

typedef enum {
    CX_PLAYER = 0, CX_RBC, CX_PLATELET, CX_SURFACTANT, CX_CHYME, CX_LYMPHGLOB, CX_VESICLE,
    CX_ENDOTHELIAL, CX_PNEUMOCYTE, CX_ENTEROCYTE, CX_LYMPHOCYTE, CX_NEURON,
    CX_BACTERIA, CX_NEUTROPHIL, CX_MACROPHAGE, CX_NK, CX_TCELL, CX_BCELL, CX_ANTIBODY,
    CX_BOSS_SENTINEL, CX_BOSS_WARDEN, CX_BOSS_LEVIATHAN, CX_BOSS_CITADEL, CX_BOSS_OVERMIND,
    CX_COUNT
} CodexId;

typedef enum {
    ACH_FIRST_INFECTION = 0, ACH_RHYTHM, ACH_STAGE2, ACH_STAGE3, ACH_STAGE5, ACH_HIVEMIND,
    ACH_ARMS_RACE, ACH_GHOST, ACH_GIANT_SLAYER, ACH_TOTAL_INFECTION, ACH_CHIMERA,
    ACH_OUTBREAK10, ACH_SATURATION, ACH_SYNERGIST, ACH_COUNT
} AchId;

typedef enum {
    TUT_MOVE = 0, TUT_INFECT, TUT_RHYTHM, TUT_MUTATE, TUT_COMBAT, TUT_ALERT, TUT_SWARM,
    TUT_BOSS, TUT_ADAPT, TUT_NK, TUT_COMPLEMENT, TUT_ANTIBODY, TUT_ENGULF, TUT_PORTAL, TUT_EVOLVE, TUT_COUNT
} TutorialId;

typedef enum { CAM_FOLLOW = 0, CAM_CINEMATIC, CAM_FREE, CAM_MENU } CamMode;

/* ------------------------------------------------------------------------- */
/* Entities                                                                   */
/* ------------------------------------------------------------------------- */

typedef struct Agent {
    uint16_t gen;
    bool alive;
    uint8_t kind, team, state, variant;
    uint16_t flags;
    Vector3 pos, prev, vel;
    Vector3 dir;            /* facing / orientation axis                     */
    Vector3 anchor;         /* wall normal for attached cells / home point   */
    Vector3 goal;           /* patrol / investigate target                   */
    float radius;
    float hp, maxHp;
    float shield;           /* fraction of toxin damage absorbed             */
    float timer, timer2, cooldown, perceptT;
    float phase, spin;
    float flash;            /* hit flash 0..1                                */
    float speedMul;
    float poisonDps, poisonT;
    float progress;         /* host: injection progress / cleanse; boss part */
    float produce;          /* host: replication timer                       */
    float vision;
    int   produced;         /* host: virions alive from this host            */
    int   group;
    AgentRef target;
    bool  targetPlayer;
    AgentRef owner;         /* minion home host / antibody source            */
} Agent;

typedef struct {
    bool alive;
    uint8_t type, team;
    Vector3 pos, prev, vel;
    float radius, damage, life, maxLife;
    float homing;
    int pierce;
    AgentRef target;
    bool targetPlayer;
    uint8_t flags;
} Projectile;

typedef struct {
    bool alive;
    uint8_t type;
    Vector3 pos, prev, vel;
    float value, life, phase;
} Pickup;

typedef struct {
    bool alive;
    uint8_t type;
    Vector3 pos, dir;
    float radius, radius2;      /* radius2: ring thickness / max radius      */
    float timer, duration;
    float strength, phase, hp;
    bool fromPlayer;
    bool triggered;
} Hazard;

typedef struct {
    Vector3 pos;
    Vector3 color;      /* linear HDR color                                  */
    float radius;
} PointLight;

typedef struct {
    bool active;
    Vector3 pos;
    char text[24];
    Color color;
    float life, maxLife, size;
} Popup;

typedef struct {
    char title[48];
    char text[96];
    float life;
    Color color;
} Toast;

typedef struct {
    int type;           /* 0 ring landmark, 1 synapse anchor, 2 cilia tuft  */
    Vector3 pos, normal;
    float scale, phase;
} Decor;

/* ------------------------------------------------------------------------- */
/* Player                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    float speed, accel;
    float dashPower, dashCost, dashCd;
    float armor;
    float maxBiomass, regen;
    float maxAtp, atpRegen;
    float sizeMul;
    float contactDps, dashDamage;
    float toxinDamage, toxinInterval, toxinSpeed, toxinCost;
    int   toxinCount;
    float toxinSpread;
    float poisonDps, poisonTime;
    float cloudChance, chainLysis;
    int   pierce;
    bool  shieldShred;
    float burstDamage, burstRadius, burstCost, burstCd;
    float infectSpeed, virionInterval;
    int   hostCapacity, burstReplicate;
    float gmMul, epMul;
    bool  hostDrip, latentInfection, perfectRhythm;
    int   minionMax;
    float minionDamage, minionSpeed, minionHp;
    bool  minionExplode, hiveLink, bacteriaAllies;
    int   minionTier;
    float swarmCd;
    float alertMul, adaptMul, detectMul, alertDecayMul, adaptDecayMul, injectAlertMul;
    bool  decoys, cloakUnlocked;
    float cloakDuration, cloakAmbush;
    /* visuals derived from mutations */
    int   flagella, spikes, membraneLayers, glowNodes, organelles;
    /* synergies */
    bool  synCytoBurst, synViralLoad, synNecrotic, synGhostSwarm, synSymbiotic, synApex;
} PlayerStats;

typedef struct {
    Vector3 pos, prev, vel;
    Vector3 forward;            /* body facing (smoothed)                     */
    float radius;
    float biomass, atp;
    int   gm;
    float ep;
    int   stage;
    float dashT, dashCd, invuln;
    float toxinCd, burstCd, swarmCd, cloakCd, cloakT;
    bool  cloaked, ambushReady;
    AgentRef attach;            /* host being injected                        */
    float rhythmPhase;
    int   rhythmCombo, bestCombo;
    float rhythmFlash;          /* +1 good / -1 bad feedback                  */
    AgentRef engulfedBy;
    float engulfT;
    int   struggle;
    int   antibodies;
    float antibodyDecay;
    float hurtFlash, squash, regenDelay, contactTick, deathT;
    float animT;
    bool  dead;
    Vector3 aimPoint;
    AgentRef aimAgent;
    bool  aimBoss;
    float lastAttackT;
} Player;

/* ------------------------------------------------------------------------- */
/* Systems                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
    float usage[IM_COUNT];
    float resist[IM_COUNT];
    float lastUse[IM_COUNT];
    int   announced[IM_COUNT];
    int   adaptations;
} ImmuneMemory;

#define MAX_MUTATIONS 48
typedef struct {
    bool owned[MAX_MUTATIONS];
    int  gmSpent;
    int  respecs;
    float unlockAnim[MAX_MUTATIONS];
    int  count;
} MutationState;

typedef struct {
    bool active;
    uint8_t type;
    Vector3 pos, prev, vel, facing;
    float radius;
    float hp, maxHp;
    int   phase;
    float timer, attackT, patternT, flash, anim, invuln;
    int   pattern, patternStep;
    float spiralAngle;
    Vector3 chargeDir;
    bool  shielded;
    int   shieldNodes;
    float hpMulPhase;
    Vector3 seg[MAX_BOSS_SEGMENTS];
    int   segCount;
    bool  introDone;
    float deathT;
    bool  dying;
} Boss;

typedef struct {
    int   kills[AK_COUNT];
    int   killsTotal, infections, bossKills;
    int   gmEarned, mutations;
    float time, damageDealt, damageTaken;
    float maxAlert;
    int   regionsCleared, bestCombo, maxMinions;
    int   deaths;
} RunStats;

typedef struct {
    RunMode mode;
    Difficulty difficulty;
    int ngPlus;
    uint32_t seed;
    bool daily;
    int regionIndex;
    RunStats stats;
    float immuneCarry[IM_COUNT];
    int wave;                       /* outbreak mode                          */
    float waveT;
    bool active;
} RunState;

typedef struct {
    Vector3 a, b;
    float r0, r1;
    uint8_t kind;   /* 0 tube (open), 1 sphere (open), 2 solid blob (closed) */
    float s0, s1;   /* path distance at a/b, for flow + peristalsis          */
    int branch;     /* -1 main path                                          */
} CaveShape;

typedef struct {
    int index;
    uint32_t seed;
    float infection;                 /* 0..1                                   */
    int hostTotal, hostInfected;
    float alert, alertDelay, peakAlert;
    bool bossSpawned, bossDefeated, portalOpen;
    Vector3 startPos, bossPos, portalPos;
    float bossRadius;
    EventType event;
    float eventT, eventDur, nextEventT;
    float fever;
    float spawnT;
    float time;
    float flowStrength, peristalsisT;
    CaveShape shapes[MAX_SHAPES];
    int shapeCount;
    Vector3 path[MAX_PATH];
    float pathS[MAX_PATH];
    int pathCount;
    float pathLength;
    Decor decor[MAX_DECOR];
    int decorCount;
    Vector3 lightDir;
    float clearT;
    bool fullInfectionAwarded;
} RegionState;

typedef struct {
    Camera3D cam;
    CamMode mode;
    float yaw, pitch, dist, targetDist;
    Vector3 pos, focus, velPos;
    float fov, fovKick;
    float trauma;
    float cineT, cineDur;
    Vector3 cineCenter;
    float cineRadius, cineHeight;
    Vector3 freePos;
    float freeYaw, freePitch;
    Vector3 shakeOffset;
} CameraRig;

typedef struct {
    float moveX, moveY, moveZ;
    float lookX, lookY;
    bool fire, firePressed;
    bool infect, infectPressed;
    bool dash, burst, swarm, cloak, mutations, pause;
    bool usingGamepad;
} InputState;

typedef struct {
    int resIndex;
    bool fullscreen, vsync, msaa;
    int quality;                    /* 0 low, 1 medium, 2 high                */
    float renderScale;
    bool postfx, bloom, dof, chromatic, grain;
    float master, sfx, music;
    float sensitivity;
    bool invertY;
    float shake;
    bool reduceFlashing;
    float uiScale;
    bool highContrast;
    int keys[ACT_COUNT];
    Difficulty difficulty;
} Settings;

typedef struct {
    int regionIndex;
    uint32_t seed;
    int mode, difficulty, ngPlus;
    bool daily;
    int stage;
    float ep;
    int gm;
    bool owned[MAX_MUTATIONS];
    int gmSpent, respecs;
    float immuneCarry[IM_COUNT];
    RunStats stats;
    int wave;
} RunSave;

typedef struct {
    uint32_t magic;
    int version;
    Settings settings;
    uint8_t codex[CX_COUNT];
    uint8_t achievements[ACH_COUNT];
    uint8_t tutorials[TUT_COUNT];
    int bestRegion, victories, ngPlusUnlocked, bestWave;
    bool outbreakUnlocked;
    int totalKills, totalInfections, runsStarted;
    bool hasRun;
    RunSave run;
} Profile;

typedef struct {
    float hover[64];
    int menuSel;
    int newGameDiff, newGameMode, newGameSeedMode;
    char seedText[16];
    bool seedEditing;
    Vector2 mutPan;
    float mutZoom;
    int mutHover, mutSelected;
    bool mutDragging;
    Vector2 mutDragStart;
    int settingsTab;
    int rebindAction;
    int codexSel;
    float codexSpin;
    char banner[64], bannerSub[128];
    float bannerT;
    Color bannerColor;
    char hint[160];
    float hintT;
    float regionTitleT;
    char objective[128];
    float heartPulse;
    float evolveT;
    int evolveFrom;
    float fade;
    GameState returnState;
    float clickCooldown;
} UiState;

typedef struct {
    bool overlay, wireframe, collision, aiPaths, godMode;
    bool autotest;
    char shot[64];
    float simMs, drawMs, frameSimSteps;
    float frameTimes[120];
    int frameIdx;
    int drawCalls, instances;
} DebugState;

typedef struct Game {
    GameState state, pendingState;
    bool quit;
    Settings settings;
    Profile profile;
    float realTime, accumulator, alpha, timeScale, hitStop, slowmoT;
    uint64_t frame;
    Rng rng;
    RunState run;
    RegionState region;
    Player player;
    PlayerStats stats;
    MutationState mut;
    ImmuneMemory immune;
    Agent agents[MAX_AGENTS];
    int agentHigh;
    int agentCount[AK_COUNT];
    Projectile proj[MAX_PROJECTILES];
    Pickup pickups[MAX_PICKUPS];
    Hazard hazards[MAX_HAZARDS];
    Boss boss;
    CameraRig rig;
    InputState in;
    UiState ui;
    DebugState debug;
    Popup popups[MAX_POPUPS];
    Toast toasts[MAX_TOASTS];
    float simTime;
    int menuRegionReady;
    int loadingFrames;
    bool regionReady;
} Game;

extern Game G;

/* ------------------------------------------------------------------------- */
/* Small inline utilities used everywhere                                     */
/* ------------------------------------------------------------------------- */

static inline float Sat(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static inline float SmoothStepF(float a, float b, float x) { float t = Sat((x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }
static inline Vector3 V3(float x, float y, float z) { return (Vector3){ x, y, z }; }
static inline Color ColorF(float r, float g, float b, float a)
{
    return (Color){ (unsigned char)(Sat(r) * 255.0f), (unsigned char)(Sat(g) * 255.0f), (unsigned char)(Sat(b) * 255.0f), (unsigned char)(Sat(a) * 255.0f) };
}
static inline Vector3 V3Damp(Vector3 a, Vector3 b, float rate, float dt) { return Vector3Lerp(a, b, 1.0f - expf(-rate * dt)); }
static inline float FDamp(float a, float b, float rate, float dt) { return a + (b - a) * (1.0f - expf(-rate * dt)); }

/* generational agent handles are used everywhere */
#include "core/pool.h"

#endif

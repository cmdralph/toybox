/*
 * sfx.h - procedurally synthesised sound effects with 3D panning,
 * voice pooling and per-sound throttling.
 */
#ifndef PZ_SFX_H
#define PZ_SFX_H
#include "core/game.h"

typedef enum {
    SFX_UI_HOVER = 0, SFX_UI_CLICK, SFX_UI_BACK, SFX_UI_DENY,
    SFX_SPIT, SFX_HIT, SFX_HIT_SHIELD, SFX_KILL, SFX_KILL_BIG, SFX_POP, SFX_SPLAT,
    SFX_DASH, SFX_BURST, SFX_HURT, SFX_DEATH,
    SFX_LATCH, SFX_DETACH, SFX_TICK, SFX_BEAT_GOOD, SFX_BEAT_BAD, SFX_INFECT, SFX_CLEANSE,
    SFX_PICKUP, SFX_CACHE, SFX_MUTATE, SFX_RESPEC, SFX_EVOLVE, SFX_ADAPT,
    SFX_WARNING, SFX_RUMBLE, SFX_ZAP, SFX_EXPLODE,
    SFX_SWARM, SFX_CLOAK, SFX_DECLOAK, SFX_SPAWN_VIRION,
    SFX_ENGULF, SFX_ESCAPE, SFX_NK_CHARGE, SFX_PERFORIN, SFX_ANTIBODY,
    SFX_BOSS_ROAR, SFX_BOSS_CHARGE, SFX_BOSS_SHOOT, SFX_BOSS_DEATH, SFX_PORTAL,
    SFX_ACHIEVE, SFX_DETECTED,
    SFX_COUNT
} SfxId;

void Sfx_Init(void);
void Sfx_Shutdown(void);
void Sfx_Play(SfxId id, float vol, float pitch);
void Sfx_Play3D(SfxId id, Vector3 pos, float vol);
void Sfx_PlayThrottled(SfxId id, float vol, float minInterval);
void Sfx_Update(float dt);
bool Sfx_Ready(void);

#endif

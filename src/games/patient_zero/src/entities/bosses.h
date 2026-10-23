/*
 * bosses.h - the five region guardians. Each has 2-3 phases, a pattern
 * scheduler and telegraphed attacks.
 */
#ifndef PZ_BOSSES_H
#define PZ_BOSSES_H
#include "core/game.h"

typedef struct {
    const char *name, *title, *lore;
    float hp, radius, speed, damage;
    int codex;
    Vector3 color, glow;
    int phases;
} BossDef;

extern const BossDef BOSSES[BOSS_COUNT];

void Bosses_Clear(void);
void Bosses_Spawn(BossType type, Vector3 pos);
void Bosses_Update(float dt);
void Bosses_UpdateIntro(float dt);
void Bosses_TakeDamage(float dmg, int src, Vector3 at);
void Bosses_PartDestroyed(Agent *a);
void Bosses_UpdatePart(Agent *a, float dt);
bool Bosses_HitTest(Vector3 p, float r, Vector3 *hitPos);
bool Bosses_Collide(Vector3 *pos, Vector3 *vel, float radius);
const char *Bosses_PatternName(void);

#endif

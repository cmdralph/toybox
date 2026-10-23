/*
 * combat.h - damage, death, explosions and combat juice (hit-stop,
 * slow-motion, popups). Every source of harm funnels through here so
 * immune-memory tracking and feedback stay consistent.
 */
#ifndef PZ_COMBAT_H
#define PZ_COMBAT_H
#include "core/game.h"

typedef enum {
    DMG_TOXIN = 0, DMG_MINION, DMG_BURST, DMG_CONTACT, DMG_DASH, DMG_HAZARD, DMG_POISON, DMG_EXPLOSION, DMG_ENEMY
} DamageSource;

void Combat_DamageAgent(Agent *a, float dmg, DamageSource src, Vector3 dir);
void Combat_KillAgent(Agent *a, DamageSource src);
void Combat_DamagePlayer(float dmg, Vector3 from, DamageSource src);
void Combat_DamageBoss(float dmg, DamageSource src, Vector3 at);
void Combat_Explosion(Vector3 pos, float radius, float dmg, DamageSource src, bool fromPlayer);
void Combat_PlayerBurst(void);
void Combat_Popup(Vector3 pos, const char *text, Color c, float size);
void Combat_HitStop(float t);
void Combat_SlowMo(float t);
Vector3 Combat_KindColor(int kind);

#endif

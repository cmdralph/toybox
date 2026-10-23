/*
 * projectiles.h - toxin globules, perforin bolts, boss orbs and globs.
 */
#ifndef PZ_PROJECTILES_H
#define PZ_PROJECTILES_H
#include "core/game.h"

int  Proj_Spawn(ProjType type, Vector3 pos, Vector3 vel, float damage, bool fromPlayer);
void Proj_Clear(void);
void Proj_Update(float dt);
int  Proj_Count(void);

#endif

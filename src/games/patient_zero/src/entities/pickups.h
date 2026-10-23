/*
 * pickups.h - genetic material orbs, ATP and biomass globules, secret
 * caches and mutagenic pods.
 */
#ifndef PZ_PICKUPS_H
#define PZ_PICKUPS_H
#include "core/game.h"

int  Pickups_Spawn(PickupType type, Vector3 pos, float value);
void Pickups_Clear(void);
void Pickups_Update(float dt);
int  Pickups_Count(void);

#endif

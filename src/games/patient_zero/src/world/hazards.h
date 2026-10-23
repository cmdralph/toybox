/*
 * hazards.h - area effects: acid pools, mucus, cilia currents, complement
 * rings, antibiotic strikes, electrical storms, clots, toxin clouds and
 * boss vortices/shockwaves. All telegraph before they hurt.
 */
#ifndef PZ_HAZARDS_H
#define PZ_HAZARDS_H
#include "core/game.h"

void Hazards_Clear(void);
int  Hazards_Spawn(HazardType type, Vector3 pos, float radius, float duration, float strength);
void Hazards_Update(float dt);
bool Hazards_Collide(Vector3 *pos, Vector3 *vel, float radius);   /* solid clots */
float Hazards_SlowAt(Vector3 pos);
int  Hazards_Count(void);

#endif

/*
 * hostcells.h - wall-anchored host cells: infection targets and, once
 * infected, virion factories.
 */
#ifndef PZ_HOSTCELLS_H
#define PZ_HOSTCELLS_H
#include "core/game.h"

int  Hosts_Spawn(Vector3 pos, Vector3 normal, float radius);
void Hosts_Update(Agent *a, float dt);

#endif

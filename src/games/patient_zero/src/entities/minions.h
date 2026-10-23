/*
 * minions.h - the player's virion swarm and decoys.
 */
#ifndef PZ_MINIONS_H
#define PZ_MINIONS_H
#include "core/game.h"

#define MINION_BOSS_TARGET (-2)

int  Minions_Spawn(Vector3 pos, AgentRef home, bool burst);
int  Minions_SpawnDecoy(Vector3 pos, Vector3 vel);
void Minions_Update(Agent *a, float dt);
void Minions_UpdateDecoy(Agent *a, float dt);
void Minions_OnDeath(Agent *a);
void Minions_Command(void);
int  Minions_Count(void);

#endif

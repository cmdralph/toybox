/*
 * agents.h - the shared agent pool update loop and ambient life
 * (drifting blood cells, platelets, gut flora).
 */
#ifndef PZ_AGENTS_H
#define PZ_AGENTS_H
#include "core/game.h"

int  Agents_Spawn(AgentKind kind, Vector3 pos);
void Agents_Clear(void);
void Agents_Update(float dt);
const char *Agents_KindName(int kind);

#endif

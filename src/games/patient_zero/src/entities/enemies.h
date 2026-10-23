/*
 * enemies.h - immune cell roster, per-kind behaviour state machines and the
 * reinforcement / outbreak director.
 */
#ifndef PZ_ENEMIES_H
#define PZ_ENEMIES_H
#include "core/game.h"

typedef struct {
    const char *name;
    float hp, speed, radius, damage, vision, ep;
    int gm;
    int codex;
    Vector3 color;
} EnemyDef;

const EnemyDef *Enemies_Def(int kind);
int  Enemies_Spawn(AgentKind kind, Vector3 pos, bool patrol);
void Enemies_Update(Agent *a, float dt);
void Enemies_OnDeath(Agent *a);
void Enemies_Reset(void);
void Enemies_Director(float dt);
bool Enemies_Detected(void);          /* any immune cell currently sees the player */
int  Enemies_Active(void);            /* immune cells alive                        */
int  Enemies_WaveRemaining(void);

#endif

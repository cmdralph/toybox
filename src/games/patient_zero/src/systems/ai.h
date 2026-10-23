/*
 * ai.h - steering behaviours and perception shared by every AI agent:
 * seek/arrive/flee, flocking (separation/alignment/cohesion), wall avoidance
 * against the cave field, and vision-cone + line-of-sight + "scent".
 */
#ifndef PZ_AI_H
#define PZ_AI_H
#include "core/game.h"

Vector3 AI_Seek(const Agent *a, Vector3 target, float speed);
Vector3 AI_Arrive(const Agent *a, Vector3 target, float speed, float slowRadius);
Vector3 AI_Flee(const Agent *a, Vector3 from, float speed);
Vector3 AI_Flock(const Agent *a, uint32_t mask, float radius, float sepW, float aliW, float cohW);
Vector3 AI_AvoidWalls(const Agent *a, float lookAhead);
void    AI_Integrate(Agent *a, Vector3 desired, float accel, float flowFactor, float dt);
bool    AI_CanSeePlayer(Agent *a);
void    AI_AlertGroup(Vector3 pos, float radius, Vector3 target);
Vector3 AI_WanderGoal(Agent *a, float range);

#endif

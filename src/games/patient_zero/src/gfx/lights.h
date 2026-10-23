/*
 * lights.h - dynamic point lights. Gameplay code submits candidates every
 * frame (infected cells, toxins, bosses, explosions); the brightest ones
 * relative to the camera are uploaded to the lit shaders.
 */
#ifndef PZ_LIGHTS_H
#define PZ_LIGHTS_H
#include "core/game.h"

void Lights_Begin(void);
void Lights_Add(Vector3 pos, Vector3 color, float radius);
void Lights_AddTemp(Vector3 pos, Vector3 color, float radius, float life);
void Lights_UpdateTemp(float dt);
void Lights_ClearTemp(void);
void Lights_Commit(Vector3 camPos);
int  Lights_Count(void);
int  Lights_Candidates(void);

#endif

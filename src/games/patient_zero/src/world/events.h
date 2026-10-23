/*
 * events.h - dynamic region events: fever, clotting, antibiotic strikes and
 * organ-specific ones (peristalsis, cough, electrical storm, swarm surge).
 */
#ifndef PZ_EVENTS_H
#define PZ_EVENTS_H
#include "core/game.h"

void Events_Reset(void);
void Events_Update(float dt);
void Events_Trigger(EventType e);
const char *Events_Name(EventType e);
float Events_EnemySpeedMul(void);

#endif

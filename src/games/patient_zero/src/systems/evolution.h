/*
 * evolution.h - the five evolutionary stages and the metamorphosis event
 * that moves the pathogen between them.
 */
#ifndef PZ_EVOLUTION_H
#define PZ_EVOLUTION_H
#include "core/game.h"

typedef struct {
    const char *name, *title, *desc;
    float epThreshold, radius, camDist;
} StageDef;

extern const StageDef STAGES[STAGE_COUNT];

bool  Evolution_Ready(void);
void  Evolution_Begin(void);
void  Evolution_Update(float dt);   /* runs while state == GS_EVOLVE */
float Evolution_Progress(void);     /* EP progress toward next stage 0..1 */
float Evolution_Duration(void);

#endif

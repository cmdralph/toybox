/*
 * flow_field.h - coarse vector field of fluid currents (bloodstream flow,
 * peristaltic waves, coughs). Sampled with trilinear interpolation by the
 * player, drifters and particles.
 */
#ifndef PZ_FLOW_FIELD_H
#define PZ_FLOW_FIELD_H
#include "core/game.h"

bool    Flow_Init(void);
void    Flow_Shutdown(void);
void    Flow_Build(const RegionState *r, float strength);
Vector3 Flow_Sample(Vector3 p);
float   Flow_PathS(Vector3 p);
void    Flow_SetModifiers(float mul, float waveS, float waveAmp, float reverse);

#endif

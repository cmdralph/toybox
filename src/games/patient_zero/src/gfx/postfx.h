/*
 * postfx.h - HDR scene target and the post chain:
 * bright pass -> two-level gaussian bloom, half-res DOF blur, composite with
 * chromatic aberration, heartbeat lens distortion, vignette, grain, ACES and
 * per-region grading.
 */
#ifndef PZ_POSTFX_H
#define PZ_POSTFX_H
#include "core/game.h"

typedef struct {
    float bloom, dof, focus, chroma, vignette, grain, pulse, flash, damage, letterbox, exposure, shimmer, haze;
    Vector3 flashColor, lift, gain, tint, hazeColor;
    float saturation, contrast;
} PostParams;

bool PostFx_Init(int w, int h);
void PostFx_Shutdown(void);
void PostFx_Resize(int w, int h);
void PostFx_Begin(Color clear);
void PostFx_End(const PostParams *p);
PostParams PostFx_Defaults(void);
bool PostFx_Ready(void);

#endif

/*
 * shaders.h - every GLSL program, embedded as C strings (GLSL 330 for the
 * macOS OpenGL 4.1 core profile), plus cached uniform locations.
 */
#ifndef PZ_SHADERS_H
#define PZ_SHADERS_H
#include "core/game.h"

typedef enum {
    SHD_LIT = 0,      /* membrane / organism shader (DrawMesh)              */
    SHD_INST,         /* instanced cells                                    */
    SHD_CAVE,         /* tissue walls                                       */
    SHD_PARTICLE,     /* instanced soft billboards                          */
    SHD_BRIGHT,       /* bloom threshold                                    */
    SHD_BLUR,         /* separable gaussian                                 */
    SHD_COMPOSITE,    /* final post chain                                   */
    SHD_COUNT
} ShaderId;

/* Per-draw material parameters shared by LIT / INST / CAVE */
typedef struct {
    Vector3 rimColor;
    float rimPower, rimStrength, rimAlpha;
    Vector3 emissive;
    float wobble, wobbleFreq, pulse, pulseSpeed;
    float alpha, sss, spec;
    float time;
} MaterialParams;

typedef struct {
    int mvp, viewPos, time, fogColor, fogDensity, sunDir, sunColor, ambient;
    int lightCount, plPos, plCol, plRad;
    int rimColor, rimPower, rimStrength, rimAlpha, emissive, wobble, wobbleFreq, pulse, pulseSpeed, alpha, sss, spec;
    int instance;     /* attribute location of instanceTransform            */
    int camRight, camUp, camPos;
    /* cave */
    int wallPulse, heart, detailColor, veinColor, veinGlow, causticColor;
    /* post */
    int threshold, knee, texel, dir;
    int bloomTex, blurTex, depthTex, bloomStrength, dofStrength, focus, nearFar, chroma, vignette, grain,
        pulseFx, lift, gain, saturation, contrast, tint, haze, hazeColor, flash, flashColor, damage, letterbox,
        exposure, shimmer, resolution, dofEnabled;
} ShaderLocs;

bool          Shaders_Init(void);
void          Shaders_Unload(void);
Shader        Shaders_Get(ShaderId id);
const ShaderLocs *Shaders_Locs(ShaderId id);
MaterialParams Shaders_DefaultMaterial(void);
void          Shaders_SetMaterial(ShaderId id, const MaterialParams *m);
void          Shaders_SetFrame(Vector3 viewPos, float time, Vector3 fogColor, float fogDensity,
                               Vector3 sunDir, Vector3 sunColor, Vector3 ambient);
void          Shaders_SetLights(const Vector3 *pos, const Vector3 *col, const float *rad, int count);

#endif

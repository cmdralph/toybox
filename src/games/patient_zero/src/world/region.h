/*
 * region.h - the five body regions: their look, content tables and
 * procedural generation (path graph -> SDF stamps -> mesh -> population).
 */
#ifndef PZ_REGION_H
#define PZ_REGION_H
#include "core/game.h"
#include "world/sdf_cave.h"

typedef struct {
    const char *name, *subtitle, *intro;
    Vector3 fogColor;
    float fogDensity;
    Vector3 ambient, sunColor, sunDir;
    CaveLook look;
    float flow;
    int drifters, platelets, hosts, bacteria;
    float hostRadiusMin, hostRadiusMax;
    Vector3 hostColor;
    int enemyBase;
    float weights[5];        /* neutrophil, macrophage, NK, T cell, B cell */
    int acidPools, mucus, cilia;
    EventType events[3];
    BossType boss;
    Vector3 lift, gain, tint;
    float saturation, contrast;
    Vector3 hazeColor;
    float haze;
    Vector3 moteA, moteB;
    float tubeRadius, tubeVar;
    float noiseAmp, noiseFreq, noiseRidged;
    int codexHost, codexDrifter;
    float bossRadius;
    float musicRoot;
    int musicTempo, musicMode;
    float drifterRadius;
    Vector3 drifterColor;
} RegionDef;

extern const RegionDef REGIONS[REGION_COUNT];

const RegionDef *Region_Def(void);
void  Region_Generate(int index, uint32_t seed, bool menuOnly);
void  Region_Update(float dt);
float Region_EnemyHpMul(void);
float Region_EnemyDmgMul(void);
bool  Region_ObjectivePos(Vector3 *out);
void  Region_ObjectiveText(char *buf, int n);
Vector3 Region_PathPoint(float s, Vector3 *tangent, float *radius);
int   Region_SecretCount(void);

#endif

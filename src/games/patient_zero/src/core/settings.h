/*
 * settings.h - default settings, resolution presets, difficulty tables and
 * quality presets (the runtime half of config.h).
 */
#ifndef PZ_SETTINGS_H
#define PZ_SETTINGS_H
#include "core/game.h"

#define RES_PRESET_COUNT 5
extern const int RES_PRESETS[RES_PRESET_COUNT][2];

typedef struct {
    const char *name;
    float enemyDamage, enemyHp, alertGain, adaptRate, spawnMul, playerRegen;
} DifficultyDef;
extern const DifficultyDef DIFFICULTIES[DIFF_COUNT];

void  Settings_Default(Settings *s);
void  Settings_ApplyQuality(Settings *s, int quality);
float Settings_DrifterDensity(void);
int   Settings_LightBudget(void);
int   Settings_ParticleBudget(void);
const char *Settings_KeyName(int key);
const char *Settings_ActionName(int action);

#endif

/*
 * save.h - profile + run persistence in the platform data directory
 * (~/Library/Application Support/PatientZero on macOS).
 */
#ifndef PZ_SAVE_H
#define PZ_SAVE_H
#include "core/game.h"

void Save_Init(void);
bool Save_LoadProfile(void);
bool Save_WriteProfile(void);
void Save_StoreRun(void);           /* copy current run state into profile    */
bool Save_RestoreRun(void);         /* copy profile run into live state        */
void Save_ClearRun(void);
const char *Save_Dir(void);

#endif

/*
 * bestiary_ui.h - the Codex: every cell type you meet, with real biology
 * notes and a rotating 3D specimen viewer.
 */
#ifndef PZ_BESTIARY_UI_H
#define PZ_BESTIARY_UI_H
#include "core/game.h"

typedef struct { const char *name, *category, *text; } CodexDef;
extern const CodexDef CODEX[CX_COUNT];

void Codex_Unlock(int id);
int  Codex_Count(void);
void Bestiary_Draw(void);
void Bestiary_Shutdown(void);

#endif

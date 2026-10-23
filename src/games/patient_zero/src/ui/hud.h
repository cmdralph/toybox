/*
 * hud.h - in-game heads-up display plus every transient message channel:
 * banners, toasts, hints, achievement popups, screen flashes, float text.
 */
#ifndef PZ_HUD_H
#define PZ_HUD_H
#include "core/game.h"

typedef struct { const char *name, *desc; } AchievementDef;
extern const AchievementDef ACHIEVEMENTS[ACH_COUNT];

void HUD_Reset(void);
void HUD_Update(float dt);
void HUD_Draw(void);
void HUD_DrawMessages(void);          /* banners/toasts/hints: drawn in every state */
void HUD_Banner(const char *title, const char *sub, Color c);
void HUD_Toast(const char *title, const char *text, Color c);
void HUD_Hint(TutorialId id, const char *text);
void HUD_Achievement(AchId id);
void HUD_Flash(Color c, float amount);
void HUD_FloatText(const char *text, Color c);
float HUD_FlashAmount(Color *c);
void HUD_RegionTitle(void);

#endif

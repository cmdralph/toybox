/*
 * menu.h - main menu, new-game setup, pause, loading, region clear,
 * game over, victory and credits screens.
 */
#ifndef PZ_MENU_H
#define PZ_MENU_H
#include "core/game.h"

void Menu_DrawMain(void);
void Menu_DrawNewGame(void);
void Menu_DrawPause(void);
void Menu_DrawLoading(void);
void Menu_DrawRegionClear(void);
void Menu_DrawGameOver(void);
void Menu_DrawVictory(void);
void Menu_DrawCredits(void);
void Menu_DrawBossIntro(void);
void Menu_DrawEvolve(void);
void Menu_DrawPhoto(void);

#endif

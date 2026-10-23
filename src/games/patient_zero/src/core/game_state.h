/*
 * game_state.h - the top-level state machine and run lifecycle.
 */
#ifndef PZ_GAME_STATE_H
#define PZ_GAME_STATE_H
#include "core/game.h"

void Game_Init(void);
void Game_Shutdown(void);
void Game_Frame(void);
void Game_SetState(GameState s);
void Game_StartRun(RunMode mode, Difficulty diff, uint32_t seed, bool daily, int ngPlus);
void Game_ContinueRun(void);
void Game_RetryRegion(void);
void Game_AbandonRun(void);
void Game_NextRegion(void);
void Game_ReturnToMenu(void);
void Game_OpenOverlay(GameState s);   /* settings / codex with return state */
void Game_CloseOverlay(void);

#endif

/*
 * input.h - keyboard/mouse/gamepad mapping into InputState.
 */
#ifndef PZ_INPUT_H
#define PZ_INPUT_H
#include "core/game.h"

void Input_Poll(InputState *in, bool gameplay);
void Input_ClearEdges(InputState *in);
bool Input_Back(void);        /* Esc / gamepad B / Start in menus */
bool Input_Confirm(void);
int  Input_AnyKeyPressed(void);

#endif

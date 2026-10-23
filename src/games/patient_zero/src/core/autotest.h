/*
 * autotest.h - scripted smoke test (`patient_zero --autotest`). Drives the
 * game through menus, play with an autopilot, the genome editor, evolution,
 * a boss fight, death/retry and every region, saving screenshots and
 * printing frame statistics. Used for CI-style verification.
 */
#ifndef PZ_AUTOTEST_H
#define PZ_AUTOTEST_H
#include "core/game.h"

void Autotest_Init(void);
void Autotest_Frame(float dt);
void Autotest_Input(InputState *in);

#endif

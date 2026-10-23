/*
 * infection.h - latching onto host cells and the rhythmic injection
 * mini-game. Hold RMB to inject; tap LMB on the pulse for bonus progress.
 */
#ifndef PZ_INFECTION_H
#define PZ_INFECTION_H
#include "core/game.h"

AgentRef Infection_FindTarget(void);
bool     Infection_TryAttach(void);
void     Infection_Update(float dt);
void     Infection_Complete(Agent *host);
void     Infection_Cleanse(Agent *host);
bool     Infection_InSweetSpot(void);

#endif

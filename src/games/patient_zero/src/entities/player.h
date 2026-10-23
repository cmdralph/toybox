/*
 * player.h - the pathogen: 6DOF swimming, abilities, aiming, resources.
 */
#ifndef PZ_PLAYER_H
#define PZ_PLAYER_H
#include "core/game.h"

void Player_Reset(void);
void Player_RecomputeStats(void);
void Player_Update(float dt);
void Player_UpdateAim(void);
void Player_Die(void);
void Player_GainEP(float ep);
void Player_GainGM(int gm, Vector3 at);
void Player_Heal(float amount);
float Player_DesiredCamDist(void);

#endif

/*
 * music.h - adaptive procedural score streamed in real time. Layers fade by
 * game state: an organic drone bed, a heartbeat locked to Alert, a pulsing
 * bassline, percussion when threatened, a melodic motif that grows with the
 * evolution stage, and a distorted boss layer. Fluid ambience rides on top.
 */
#ifndef PZ_MUSIC_H
#define PZ_MUSIC_H
#include "core/game.h"

void Music_Init(void);
void Music_Shutdown(void);
void Music_Update(float dt);
void Music_Stinger(int kind);       /* 0 discover, 1 evolve, 2 boss, 3 victory, 4 death */
float Music_Heartbeat(void);         /* 0..1 envelope of the current heartbeat     */

#endif

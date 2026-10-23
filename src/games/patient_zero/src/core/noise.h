/*
 * noise.h - seeded 3D value noise, fBm and ridged noise for cave generation,
 * mesh displacement and procedural textures.
 */
#ifndef PZ_NOISE_H
#define PZ_NOISE_H
#include "core/game.h"

void  Noise_SetSeed(uint32_t seed);
float Noise_Value3(float x, float y, float z);          /* [-1,1]            */
float Noise_Fbm3(float x, float y, float z, int oct);    /* [-1,1]            */
float Noise_Ridged3(float x, float y, float z, int oct); /* [0,1]             */
float Noise_Hash3(int x, int y, int z);                  /* [0,1)             */

#endif

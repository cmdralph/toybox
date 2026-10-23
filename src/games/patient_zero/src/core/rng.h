/*
 * rng.h - PCG32 random number generator.
 * Seeded per run and per region so worlds are reproducible ("daily seed").
 */
#ifndef PZ_RNG_H
#define PZ_RNG_H
#include "core/game.h"

void     Rng_Seed(Rng *r, uint64_t seed);
uint32_t Rng_U32(Rng *r);
float    Rng_Float(Rng *r);                       /* [0,1)                  */
float    Rng_Range(Rng *r, float a, float b);
int      Rng_Int(Rng *r, int lo, int hi);         /* inclusive              */
bool     Rng_Chance(Rng *r, float p);
Vector3  Rng_UnitVec(Rng *r);
Vector3  Rng_InSphere(Rng *r);
uint32_t Rng_DailySeed(void);
uint32_t Rng_Hash32(uint32_t x);

/* Convenience wrappers on the global gameplay stream (G.rng). */
float    Rng_FloatG(void);
float    Rng_RangeG(float a, float b);
int      Rng_IntG(int lo, int hi);
Vector3  Rng_UnitVecG(void);
Vector3  Rng_InSphereG(void);

#endif

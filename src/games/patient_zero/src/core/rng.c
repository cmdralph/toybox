/*
 * rng.c - PCG32 (O'Neill). Small, fast, statistically solid and, unlike
 * rand(), identical on every platform, which keeps daily seeds fair.
 */
#include "core/rng.h"
#include <time.h>

void Rng_Seed(Rng *r, uint64_t seed)
{
    r->state = 0u;
    r->inc = (seed << 1u) | 1u;
    Rng_U32(r);
    r->state += seed ^ 0x853c49e6748fea9bULL;
    Rng_U32(r);
}

uint32_t Rng_U32(Rng *r)
{
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

float Rng_Float(Rng *r) { return (float)(Rng_U32(r) >> 8) * (1.0f / 16777216.0f); }
float Rng_Range(Rng *r, float a, float b) { return a + (b - a) * Rng_Float(r); }

int Rng_Int(Rng *r, int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)(Rng_U32(r) % (uint32_t)(hi - lo + 1));
}

bool Rng_Chance(Rng *r, float p) { return Rng_Float(r) < p; }

Vector3 Rng_UnitVec(Rng *r)
{
    float z = Rng_Range(r, -1.0f, 1.0f);
    float a = Rng_Range(r, 0.0f, 2.0f * PI);
    float s = sqrtf(fmaxf(0.0f, 1.0f - z * z));
    return (Vector3){ s * cosf(a), z, s * sinf(a) };
}

Vector3 Rng_InSphere(Rng *r)
{
    float d = cbrtf(Rng_Float(r));
    return Vector3Scale(Rng_UnitVec(r), d);
}

uint32_t Rng_Hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

uint32_t Rng_DailySeed(void)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    uint32_t ymd = (uint32_t)((lt->tm_year + 1900) * 10000 + (lt->tm_mon + 1) * 100 + lt->tm_mday);
    return Rng_Hash32(ymd ^ 0xC0FFEEu) % 99999989u + 1u;
}

float   Rng_FloatG(void) { return Rng_Float(&G.rng); }
float   Rng_RangeG(float a, float b) { return Rng_Range(&G.rng, a, b); }
int     Rng_IntG(int lo, int hi) { return Rng_Int(&G.rng, lo, hi); }
Vector3 Rng_UnitVecG(void) { return Rng_UnitVec(&G.rng); }
Vector3 Rng_InSphereG(void) { return Rng_InSphere(&G.rng); }

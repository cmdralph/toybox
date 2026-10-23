/*
 * noise.c - value noise with quintic interpolation. Value noise is cheaper
 * than simplex and, layered as fBm, indistinguishable for organic tissue.
 */
#include "core/noise.h"

static uint32_t sSeed = 1337u;

void Noise_SetSeed(uint32_t seed) { sSeed = seed * 0x9E3779B9u + 0x7F4A7C15u; }

static inline uint32_t H3(int x, int y, int z)
{
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu ^ sSeed;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

float Noise_Hash3(int x, int y, int z) { return (float)(H3(x, y, z) & 0xFFFFFF) / 16777216.0f; }

static inline float Q(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float Noise_Value3(float x, float y, float z)
{
    int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
    float fx = x - (float)xi, fy = y - (float)yi, fz = z - (float)zi;
    float u = Q(fx), v = Q(fy), w = Q(fz);
    float c000 = Noise_Hash3(xi, yi, zi),         c100 = Noise_Hash3(xi + 1, yi, zi);
    float c010 = Noise_Hash3(xi, yi + 1, zi),     c110 = Noise_Hash3(xi + 1, yi + 1, zi);
    float c001 = Noise_Hash3(xi, yi, zi + 1),     c101 = Noise_Hash3(xi + 1, yi, zi + 1);
    float c011 = Noise_Hash3(xi, yi + 1, zi + 1), c111 = Noise_Hash3(xi + 1, yi + 1, zi + 1);
    float x00 = c000 + (c100 - c000) * u, x10 = c010 + (c110 - c010) * u;
    float x01 = c001 + (c101 - c001) * u, x11 = c011 + (c111 - c011) * u;
    float y0 = x00 + (x10 - x00) * v, y1 = x01 + (x11 - x01) * v;
    return (y0 + (y1 - y0) * w) * 2.0f - 1.0f;
}

float Noise_Fbm3(float x, float y, float z, int oct)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int i = 0; i < oct; i++) {
        sum += amp * Noise_Value3(x, y, z);
        norm += amp;
        amp *= 0.5f;
        x = x * 2.03f + 19.1f; y = y * 2.03f - 7.3f; z = z * 2.03f + 3.7f;
    }
    return sum / norm;
}

float Noise_Ridged3(float x, float y, float z, int oct)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int i = 0; i < oct; i++) {
        float n = 1.0f - fabsf(Noise_Value3(x, y, z));
        sum += amp * n * n;
        norm += amp;
        amp *= 0.5f;
        x = x * 2.1f + 5.2f; y = y * 2.1f + 1.3f; z = z * 2.1f - 8.7f;
    }
    return sum / norm;
}

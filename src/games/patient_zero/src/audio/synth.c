/*
 * synth.c - DSP primitives. Everything is float, mono, 44.1 kHz.
 */
#include "audio/synth.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define TAU 6.28318530718f

float Syn_Sine(float ph) { return sinf(ph * TAU); }
float Syn_Saw(float ph) { ph -= floorf(ph); return 2.0f * ph - 1.0f; }
float Syn_Square(float ph, float pw) { ph -= floorf(ph); return ph < pw ? 1.0f : -1.0f; }
float Syn_Tri(float ph) { ph -= floorf(ph); return 4.0f * fabsf(ph - 0.5f) - 1.0f; }

float Syn_Noise(uint32_t *s)
{
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(*s & 0xFFFFFF) / 8388607.5f - 1.0f;
}

float Syn_Env(float t, float a, float d, float s, float r, float len)
{
    if (t < 0.0f) return 0.0f;
    if (t < a) return t / a;
    if (t < a + d) return 1.0f - (1.0f - s) * (t - a) / d;
    if (t < len) return s;
    if (t < len + r) return s * (1.0f - (t - len) / r);
    return 0.0f;
}

float Syn_Exp(float t, float tau) { return t < 0.0f ? 0.0f : expf(-t / tau); }

static float SvfStep(Svf *f, float in, float cutoff, float q, int mode)
{
    float fc = cutoff / (float)SYNTH_RATE;
    if (fc > 0.24f) fc = 0.24f;
    if (fc < 0.0001f) fc = 0.0001f;
    float g = 2.0f * sinf(3.14159265f * fc);
    float damp = 1.0f / (q < 0.5f ? 0.5f : q);
    float high = in - f->low - damp * f->band;
    f->band += g * high;
    f->low += g * f->band;
    if (!isfinite(f->low) || !isfinite(f->band)) { f->low = 0.0f; f->band = 0.0f; }
    switch (mode) {
        case 0: return f->low;
        case 1: return f->band;
        default: return high;
    }
}

float Syn_SvfLP(Svf *f, float in, float cutoff, float q) { return SvfStep(f, in, cutoff, q, 0); }
float Syn_SvfBP(Svf *f, float in, float cutoff, float q) { return SvfStep(f, in, cutoff, q, 1); }
float Syn_SvfHP(Svf *f, float in, float cutoff, float q) { return SvfStep(f, in, cutoff, q, 2); }

float Syn_OnePoleLP(OnePole *f, float in, float cutoff)
{
    float a = 1.0f - expf(-TAU * cutoff / (float)SYNTH_RATE);
    f->z += a * (in - f->z);
    return f->z;
}

float Syn_Drive(float x, float amt) { return tanhf(x * amt) / tanhf(amt > 0.01f ? amt : 0.01f); }
float Syn_MidiHz(float note) { return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f); }

/* Schroeder/Freeverb-style: 4 damped combs in parallel -> 2 allpasses */
void Syn_Reverb(float *buf, int n, float room, float damp, float wet)
{
    static const int combLen[4] = { 1557, 1617, 1491, 1422 };
    static const int apLen[2] = { 556, 441 };
    float *cb[4], *ab[2];
    int ci[4] = { 0 }, ai[2] = { 0 };
    float cf[4] = { 0 };
    for (int k = 0; k < 4; k++) cb[k] = (float *)calloc((size_t)combLen[k], sizeof(float));
    for (int k = 0; k < 2; k++) ab[k] = (float *)calloc((size_t)apLen[k], sizeof(float));
    float fb = 0.70f + 0.28f * room;
    for (int i = 0; i < n; i++) {
        float in = buf[i] * 0.25f;
        float out = 0.0f;
        for (int k = 0; k < 4; k++) {
            float y = cb[k][ci[k]];
            cf[k] = y * (1.0f - damp) + cf[k] * damp;
            cb[k][ci[k]] = in + cf[k] * fb;
            ci[k] = (ci[k] + 1) % combLen[k];
            out += y;
        }
        for (int k = 0; k < 2; k++) {
            float bv = ab[k][ai[k]];
            float y = -out + bv;
            ab[k][ai[k]] = out + bv * 0.5f;
            ai[k] = (ai[k] + 1) % apLen[k];
            out = y;
        }
        buf[i] = buf[i] * (1.0f - wet * 0.5f) + out * wet;
    }
    for (int k = 0; k < 4; k++) free(cb[k]);
    for (int k = 0; k < 2; k++) free(ab[k]);
}

void Syn_Normalize(float *buf, int n, float peak)
{
    float m = 0.0f;
    for (int i = 0; i < n; i++) if (fabsf(buf[i]) > m) m = fabsf(buf[i]);
    if (m < 1e-6f) return;
    float g = peak / m;
    for (int i = 0; i < n; i++) buf[i] *= g;
}

void Syn_FadeEdges(float *buf, int n, int fadeIn, int fadeOut)
{
    for (int i = 0; i < fadeIn && i < n; i++) buf[i] *= (float)i / (float)fadeIn;
    for (int i = 0; i < fadeOut && i < n; i++) buf[n - 1 - i] *= (float)i / (float)fadeOut;
}

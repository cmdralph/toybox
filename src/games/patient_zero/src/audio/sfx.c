/*
 * sfx.c - every sound effect is synthesised at startup from a recipe:
 * wet membrane pops, squelches, chitinous clicks, swells and roars.
 * Each sound gets 4 aliased voices so rapid repeats overlap naturally.
 */
#include "audio/sfx.h"
#include "audio/synth.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define VOICES 4
#define SR SYNTH_RATE

static Sound sSound[SFX_COUNT][VOICES];
static int sNext[SFX_COUNT];
static float sLast[SFX_COUNT];
static bool sReady = false;
static uint32_t sSeed = 0x1234567u;

static float Rand01(void) { return Syn_Noise(&sSeed) * 0.5f + 0.5f; }

typedef struct { float *b; int n; } Buf;

static Buf NewBuf(float seconds)
{
    Buf b;
    b.n = (int)(seconds * (float)SR);
    b.b = (float *)calloc((size_t)b.n, sizeof(float));
    return b;
}

/* --- building blocks (each adds into the buffer) ------------------------- */

static void AddSweep(Buf *b, float t0, float dur, float f0, float f1, float amp, float tau, int wave)
{
    int s0 = (int)(t0 * SR), n = (int)(dur * SR);
    float ph = 0.0f;
    for (int i = 0; i < n && s0 + i < b->n; i++) {
        float t = (float)i / SR;
        float k = t / dur;
        float f = f0 * powf(f1 / f0, k);
        ph += f / SR;
        float o = wave == 0 ? Syn_Sine(ph) : wave == 1 ? Syn_Saw(ph) : wave == 2 ? Syn_Square(ph, 0.5f) : Syn_Tri(ph);
        float env = (tau > 0.0f ? Syn_Exp(t, tau) : 1.0f) * fminf(1.0f, t * 400.0f) * fminf(1.0f, (dur - t) * 200.0f);
        b->b[s0 + i] += o * amp * env;
    }
}

static void AddNoise(Buf *b, float t0, float dur, float c0, float c1, float q, float amp, float tau, int mode)
{
    int s0 = (int)(t0 * SR), n = (int)(dur * SR);
    Svf f = { 0 };
    for (int i = 0; i < n && s0 + i < b->n; i++) {
        float t = (float)i / SR;
        float k = t / dur;
        float c = c0 * powf(c1 / c0, k);
        float x = Syn_Noise(&sSeed);
        float y = mode == 0 ? Syn_SvfLP(&f, x, c, q) : mode == 1 ? Syn_SvfBP(&f, x, c, q) : Syn_SvfHP(&f, x, c, q);
        float env = (tau > 0.0f ? Syn_Exp(t, tau) : 1.0f) * fminf(1.0f, t * 300.0f) * fminf(1.0f, (dur - t) * 150.0f);
        b->b[s0 + i] += y * amp * env;
    }
}

static void AddBubbles(Buf *b, float t0, float dur, int count, float fLo, float fHi, float amp)
{
    for (int i = 0; i < count; i++) {
        float t = t0 + Rand01() * dur;
        float f = fLo + Rand01() * (fHi - fLo);
        AddSweep(b, t, 0.05f, f, f * 1.8f, amp * (0.5f + 0.5f * Rand01()), 0.015f, 0);
    }
}

static void AddChord(Buf *b, float t0, float dur, const float *notes, int count, float amp, float attack, float tau, int wave)
{
    int s0 = (int)(t0 * SR), n = (int)(dur * SR);
    for (int k = 0; k < count; k++) {
        float f = Syn_MidiHz(notes[k]);
        float ph = Rand01(), ph2 = Rand01();
        for (int i = 0; i < n && s0 + i < b->n; i++) {
            float t = (float)i / SR;
            ph += f / SR;
            ph2 += f * 1.004f / SR;
            float o = wave == 0 ? (Syn_Sine(ph) + 0.5f * Syn_Sine(ph2 * 2.0f)) : (Syn_Saw(ph) + Syn_Saw(ph2)) * 0.5f;
            float env = fminf(1.0f, t / attack) * (tau > 0.0f ? Syn_Exp(t, tau) : 1.0f) * fminf(1.0f, (dur - t) * 30.0f);
            b->b[s0 + i] += o * amp * env / (float)count;
        }
    }
}

static void LowPass(Buf *b, float cutoff)
{
    Svf f = { 0 };
    for (int i = 0; i < b->n; i++) b->b[i] = Syn_SvfLP(&f, b->b[i], cutoff, 0.7f);
}

static void Drive(Buf *b, float amt) { for (int i = 0; i < b->n; i++) b->b[i] = Syn_Drive(b->b[i], amt); }

/* --- recipes ------------------------------------------------------------- */

static Buf Make(SfxId id)
{
    Buf b;
    switch (id) {
        case SFX_UI_HOVER: b = NewBuf(0.06f); AddSweep(&b, 0, 0.06f, 900, 1150, 0.4f, 0.02f, 0); break;
        case SFX_UI_CLICK: b = NewBuf(0.14f); AddSweep(&b, 0, 0.07f, 420, 950, 0.6f, 0.03f, 0); AddSweep(&b, 0.05f, 0.08f, 700, 1300, 0.4f, 0.03f, 0); AddNoise(&b, 0, 0.02f, 3000, 3000, 1, 0.2f, 0.005f, 1); break;
        case SFX_UI_BACK: b = NewBuf(0.12f); AddSweep(&b, 0, 0.12f, 700, 330, 0.55f, 0.05f, 0); break;
        case SFX_UI_DENY: b = NewBuf(0.18f); AddSweep(&b, 0, 0.18f, 150, 130, 0.5f, 0.08f, 2); LowPass(&b, 900); break;
        case SFX_SPIT: b = NewBuf(0.16f); AddNoise(&b, 0, 0.12f, 1600, 500, 1.5f, 0.9f, 0.03f, 1); AddSweep(&b, 0, 0.1f, 340, 110, 0.6f, 0.03f, 0); break;
        case SFX_HIT: b = NewBuf(0.12f); AddNoise(&b, 0, 0.1f, 900, 300, 2.0f, 0.8f, 0.025f, 1); AddSweep(&b, 0, 0.08f, 200, 80, 0.6f, 0.03f, 0); break;
        case SFX_HIT_SHIELD: b = NewBuf(0.3f); { float n[] = { 82, 88.5f, 94.2f }; AddChord(&b, 0, 0.3f, n, 3, 0.7f, 0.001f, 0.06f, 0); } AddNoise(&b, 0, 0.03f, 5000, 5000, 1, 0.3f, 0.008f, 2); break;
        case SFX_KILL: b = NewBuf(0.45f); AddNoise(&b, 0, 0.35f, 3200, 250, 0.8f, 0.9f, 0.09f, 0); AddSweep(&b, 0, 0.25f, 230, 55, 0.8f, 0.07f, 0); AddBubbles(&b, 0.05f, 0.3f, 7, 300, 900, 0.25f); break;
        case SFX_KILL_BIG: b = NewBuf(0.9f); AddNoise(&b, 0, 0.8f, 2200, 120, 0.8f, 1.0f, 0.22f, 0); AddSweep(&b, 0, 0.6f, 140, 35, 1.0f, 0.18f, 0); AddBubbles(&b, 0.05f, 0.6f, 14, 200, 700, 0.3f); Syn_Reverb(b.b, b.n, 0.6f, 0.4f, 0.3f); break;
        case SFX_POP: b = NewBuf(0.08f); AddSweep(&b, 0, 0.06f, 320, 980, 0.7f, 0.02f, 0); break;
        case SFX_SPLAT: b = NewBuf(0.3f); AddNoise(&b, 0, 0.28f, 1800, 180, 1.0f, 1.0f, 0.07f, 0); AddSweep(&b, 0, 0.12f, 110, 50, 0.7f, 0.04f, 0); break;
        case SFX_DASH: b = NewBuf(0.32f); AddNoise(&b, 0, 0.3f, 350, 1900, 2.5f, 0.9f, 0.12f, 1); AddNoise(&b, 0.12f, 0.18f, 1900, 500, 2.0f, 0.4f, 0.06f, 1); break;
        case SFX_BURST: b = NewBuf(1.0f); AddSweep(&b, 0, 0.8f, 95, 28, 1.0f, 0.25f, 0); AddNoise(&b, 0, 0.7f, 3000, 150, 0.7f, 0.9f, 0.2f, 0); AddBubbles(&b, 0.05f, 0.6f, 16, 300, 1200, 0.2f); Syn_Reverb(b.b, b.n, 0.7f, 0.3f, 0.35f); break;
        case SFX_HURT: b = NewBuf(0.3f); AddSweep(&b, 0, 0.25f, 130, 48, 0.9f, 0.08f, 0); AddNoise(&b, 0, 0.18f, 700, 200, 2.0f, 0.7f, 0.05f, 1); Drive(&b, 2.0f); break;
        case SFX_DEATH: b = NewBuf(2.4f); AddSweep(&b, 0, 2.2f, 220, 40, 0.6f, 0.9f, 1); AddSweep(&b, 0, 2.2f, 233, 41, 0.5f, 0.9f, 1); AddNoise(&b, 0, 1.6f, 1500, 100, 0.8f, 0.7f, 0.5f, 0); LowPass(&b, 1800); Syn_Reverb(b.b, b.n, 0.9f, 0.3f, 0.5f); break;
        case SFX_LATCH: b = NewBuf(0.2f); AddNoise(&b, 0, 0.16f, 200, 1500, 3.0f, 0.8f, 0.0f, 1); AddSweep(&b, 0.13f, 0.05f, 1200, 600, 0.5f, 0.02f, 0); break;
        case SFX_DETACH: b = NewBuf(0.18f); AddNoise(&b, 0, 0.16f, 1400, 300, 3.0f, 0.6f, 0.0f, 1); break;
        case SFX_TICK: b = NewBuf(0.05f); AddSweep(&b, 0, 0.04f, 620, 560, 0.5f, 0.012f, 0); break;
        case SFX_BEAT_GOOD: b = NewBuf(0.45f); { float n[] = { 81, 88, 93 }; AddChord(&b, 0, 0.45f, n, 3, 0.8f, 0.002f, 0.12f, 0); } Syn_Reverb(b.b, b.n, 0.5f, 0.5f, 0.25f); break;
        case SFX_BEAT_BAD: b = NewBuf(0.2f); AddSweep(&b, 0, 0.18f, 200, 190, 0.4f, 0.07f, 2); AddSweep(&b, 0, 0.18f, 213, 201, 0.4f, 0.07f, 2); LowPass(&b, 1100); break;
        case SFX_INFECT: b = NewBuf(1.2f); { float n1[] = { 69 }, n2[] = { 73 }, n3[] = { 76 }, n4[] = { 81 };
            AddChord(&b, 0.0f, 0.9f, n1, 1, 0.5f, 0.003f, 0.3f, 0); AddChord(&b, 0.07f, 0.9f, n2, 1, 0.5f, 0.003f, 0.3f, 0);
            AddChord(&b, 0.14f, 0.9f, n3, 1, 0.5f, 0.003f, 0.3f, 0); AddChord(&b, 0.21f, 0.9f, n4, 1, 0.5f, 0.003f, 0.35f, 0); }
            AddNoise(&b, 0, 0.3f, 300, 2500, 1.5f, 0.4f, 0.1f, 1); AddBubbles(&b, 0.1f, 0.5f, 10, 500, 1500, 0.2f); Syn_Reverb(b.b, b.n, 0.7f, 0.4f, 0.4f); break;
        case SFX_CLEANSE: b = NewBuf(0.8f); { float n1[] = { 76 }, n2[] = { 72 }, n3[] = { 69 };
            AddChord(&b, 0.0f, 0.6f, n1, 1, 0.5f, 0.003f, 0.2f, 1); AddChord(&b, 0.1f, 0.6f, n2, 1, 0.5f, 0.003f, 0.2f, 1); AddChord(&b, 0.2f, 0.6f, n3, 1, 0.5f, 0.003f, 0.25f, 1); }
            AddNoise(&b, 0, 0.6f, 5000, 1500, 1.0f, 0.3f, 0.2f, 2); LowPass(&b, 2500); Syn_Reverb(b.b, b.n, 0.6f, 0.4f, 0.3f); break;
        case SFX_PICKUP: b = NewBuf(0.09f); AddSweep(&b, 0, 0.08f, 1500, 2400, 0.4f, 0.025f, 0); break;
        case SFX_CACHE: b = NewBuf(1.4f); { float n[] = { 72, 76, 79, 84, 88 }; for (int i = 0; i < 5; i++) AddChord(&b, 0.08f * (float)i, 1.0f, &n[i], 1, 0.45f, 0.002f, 0.35f, 0); }
            Syn_Reverb(b.b, b.n, 0.8f, 0.4f, 0.4f); break;
        case SFX_MUTATE: b = NewBuf(1.0f); for (int i = 0; i < 12; i++) AddSweep(&b, 0.04f * (float)i, 0.05f, 300.0f + 80.0f * (float)i, 600.0f + 120.0f * (float)i, 0.35f, 0.02f, 3);
            { float n[] = { 60, 67, 72, 76 }; AddChord(&b, 0.3f, 0.7f, n, 4, 0.6f, 0.05f, 0.3f, 1); } LowPass(&b, 3500); Syn_Reverb(b.b, b.n, 0.7f, 0.4f, 0.35f); break;
        case SFX_RESPEC: b = NewBuf(0.9f); for (int i = 0; i < 10; i++) AddSweep(&b, 0.05f * (float)i, 0.05f, 1200.0f - 90.0f * (float)i, 600.0f - 40.0f * (float)i, 0.35f, 0.02f, 3); Syn_Reverb(b.b, b.n, 0.6f, 0.4f, 0.3f); break;
        case SFX_EVOLVE: b = NewBuf(3.2f); { float n[] = { 48, 55, 60, 64, 67, 72 }; AddChord(&b, 0, 3.0f, n, 6, 0.9f, 1.6f, 0.0f, 1); }
            AddNoise(&b, 0, 2.0f, 200, 6000, 1.2f, 0.4f, 0.0f, 1); AddSweep(&b, 1.95f, 1.0f, 90, 30, 1.0f, 0.3f, 0); LowPass(&b, 4000); Syn_Reverb(b.b, b.n, 0.9f, 0.3f, 0.5f); break;
        case SFX_ADAPT: b = NewBuf(1.5f); { float n[] = { 38, 45, 39 }; AddChord(&b, 0, 1.4f, n, 3, 0.9f, 0.2f, 0.6f, 1); }
            for (int i = 0; i < b.n; i++) b.b[i] *= 0.6f + 0.4f * sinf((float)i / SR * 6.28f * 7.0f);
            { float m[] = { 79, 86.3f }; AddChord(&b, 0, 0.5f, m, 2, 0.4f, 0.001f, 0.1f, 0); } LowPass(&b, 2200); Syn_Reverb(b.b, b.n, 0.8f, 0.3f, 0.4f); break;
        case SFX_WARNING: b = NewBuf(1.0f); for (int i = 0; i < 3; i++) { AddSweep(&b, 0.3f * (float)i, 0.14f, 440, 430, 0.5f, 0.0f, 3); AddSweep(&b, 0.3f * (float)i + 0.14f, 0.14f, 330, 325, 0.5f, 0.0f, 3); }
            LowPass(&b, 2000); break;
        case SFX_RUMBLE: b = NewBuf(1.6f); AddNoise(&b, 0, 1.6f, 120, 60, 1.0f, 1.6f, 0.0f, 0); for (int i = 0; i < b.n; i++) b.b[i] *= sinf((float)i / (float)b.n * 3.14159f); break;
        case SFX_ZAP: b = NewBuf(0.35f); AddNoise(&b, 0, 0.3f, 4000, 1500, 0.7f, 0.8f, 0.08f, 2);
            for (int k = 0; k < 6; k++) { AddSweep(&b, 0.04f * (float)k, 0.05f, 1500.0f + Rand01() * 2500.0f, 600.0f, 0.3f, 0.02f, 1); }
            Drive(&b, 2.5f); break;
        case SFX_EXPLODE: b = NewBuf(0.9f); AddNoise(&b, 0, 0.85f, 2200, 90, 0.7f, 1.0f, 0.25f, 0); AddSweep(&b, 0, 0.5f, 90, 35, 1.0f, 0.15f, 0); Syn_Reverb(b.b, b.n, 0.6f, 0.4f, 0.3f); break;
        case SFX_SWARM: b = NewBuf(0.6f); for (int k = 0; k < 6; k++) AddSweep(&b, 0.02f * (float)k, 0.5f, 190.0f + 12.0f * (float)k, 260.0f + 10.0f * (float)k, 0.25f, 0.25f, 1);
            for (int i = 0; i < b.n; i++) { b.b[i] *= 0.6f + 0.4f * sinf((float)i / SR * 6.28f * 22.0f); }
            LowPass(&b, 2200); break;
        case SFX_CLOAK: b = NewBuf(0.7f); AddNoise(&b, 0, 0.7f, 6000, 400, 2.0f, 0.5f, 0.3f, 1); AddSweep(&b, 0, 0.6f, 1400, 300, 0.3f, 0.3f, 0); AddSweep(&b, 0, 0.6f, 1480, 320, 0.3f, 0.3f, 0); Syn_Reverb(b.b, b.n, 0.6f, 0.2f, 0.4f); break;
        case SFX_DECLOAK: b = NewBuf(0.5f); AddNoise(&b, 0, 0.5f, 400, 5000, 2.0f, 0.4f, 0.2f, 1); AddSweep(&b, 0, 0.4f, 300, 1200, 0.3f, 0.2f, 0); break;
        case SFX_SPAWN_VIRION: b = NewBuf(0.12f); AddSweep(&b, 0, 0.1f, 500, 1400, 0.5f, 0.03f, 0); AddBubbles(&b, 0, 0.08f, 3, 800, 1600, 0.2f); break;
        case SFX_ENGULF: b = NewBuf(0.7f); AddSweep(&b, 0, 0.6f, 160, 55, 1.0f, 0.25f, 0); AddNoise(&b, 0, 0.6f, 600, 150, 1.2f, 0.8f, 0.2f, 0); AddBubbles(&b, 0.1f, 0.5f, 8, 150, 400, 0.3f); break;
        case SFX_ESCAPE: b = NewBuf(0.45f); AddNoise(&b, 0, 0.3f, 300, 3000, 2.0f, 0.9f, 0.1f, 1); AddSweep(&b, 0.25f, 0.1f, 400, 1200, 0.6f, 0.04f, 0); break;
        case SFX_NK_CHARGE: b = NewBuf(0.65f); AddSweep(&b, 0, 0.6f, 200, 900, 0.4f, 0.0f, 1); AddSweep(&b, 0, 0.6f, 205, 930, 0.4f, 0.0f, 1); LowPass(&b, 3000); break;
        case SFX_PERFORIN: b = NewBuf(0.15f); AddNoise(&b, 0, 0.1f, 6000, 3000, 1.0f, 0.6f, 0.025f, 2); AddSweep(&b, 0, 0.1f, 1000, 500, 0.4f, 0.03f, 0); break;
        case SFX_ANTIBODY: b = NewBuf(0.2f); for (int k = 0; k < 4; k++) AddSweep(&b, 0.035f * (float)k, 0.03f, 2200.0f + 300.0f * (float)k, 2000.0f, 0.3f, 0.01f, 0); break;
        case SFX_BOSS_ROAR: b = NewBuf(2.2f); {
            int n = b.n; float ph = 0, ph2 = 0; Svf f = { 0 };
            for (int i = 0; i < n; i++) { float t = (float)i / SR; float fr = 52.0f + 6.0f * sinf(t * 7.0f); ph += fr / SR; ph2 += fr * 1.51f / SR;
                float x = Syn_Saw(ph) + 0.6f * Syn_Saw(ph2) + 0.4f * Syn_Noise(&sSeed);
                float formant = 400.0f + 500.0f * sinf(t * 1.7f) * sinf(t * 1.7f);
                b.b[i] = Syn_SvfBP(&f, x, formant, 3.0f) * Syn_Env(t, 0.25f, 0.3f, 0.8f, 0.9f, 1.2f); } }
            Drive(&b, 3.0f); Syn_Reverb(b.b, b.n, 0.9f, 0.3f, 0.45f); break;
        case SFX_BOSS_CHARGE: b = NewBuf(1.0f); AddNoise(&b, 0, 1.0f, 80, 400, 1.5f, 1.2f, 0.0f, 0); AddSweep(&b, 0, 1.0f, 50, 140, 0.6f, 0.0f, 1); LowPass(&b, 900); Drive(&b, 1.5f); break;
        case SFX_BOSS_SHOOT: b = NewBuf(0.35f); AddSweep(&b, 0, 0.3f, 130, 50, 1.0f, 0.1f, 0); AddNoise(&b, 0, 0.2f, 1200, 300, 1.2f, 0.5f, 0.06f, 1); break;
        case SFX_BOSS_DEATH: b = NewBuf(3.5f); AddNoise(&b, 0, 3.2f, 3000, 60, 0.6f, 1.0f, 1.0f, 0); AddSweep(&b, 0, 2.5f, 400, 40, 0.5f, 1.0f, 1); AddSweep(&b, 0, 1.0f, 70, 25, 1.0f, 0.4f, 0);
            AddBubbles(&b, 0.1f, 2.5f, 40, 150, 900, 0.3f); Syn_Reverb(b.b, b.n, 0.95f, 0.3f, 0.5f); break;
        case SFX_PORTAL: b = NewBuf(1.8f); { float n[] = { 60, 64, 67, 71, 74 }; AddChord(&b, 0, 1.7f, n, 5, 0.8f, 0.5f, 0.0f, 0); }
            for (int i = 0; i < b.n; i++) { b.b[i] *= 0.7f + 0.3f * sinf((float)i / SR * 6.28f * 9.0f); }
            Syn_Reverb(b.b, b.n, 0.9f, 0.3f, 0.5f); break;
        case SFX_ACHIEVE: b = NewBuf(1.3f); { float n[] = { 72, 76, 79, 83, 84 }; for (int i = 0; i < 5; i++) AddChord(&b, 0.06f * (float)i, 1.0f, &n[i], 1, 0.4f, 0.002f, 0.4f, 0); } Syn_Reverb(b.b, b.n, 0.8f, 0.4f, 0.4f); break;
        case SFX_DETECTED: b = NewBuf(0.5f); { float n1[] = { 74 }, n2[] = { 75 }; AddChord(&b, 0, 0.2f, n1, 1, 0.5f, 0.002f, 0.08f, 1); AddChord(&b, 0.12f, 0.35f, n2, 1, 0.5f, 0.002f, 0.12f, 1); } LowPass(&b, 2500); break;
        default: b = NewBuf(0.05f); break;
    }
    Syn_FadeEdges(b.b, b.n, 32, 256);
    /* loudness-normalise: target RMS with a peak ceiling, so a hiss and a
       sine blip of equal "volume" actually sound equally loud */
    float peak = 0.0f, sum = 0.0f;
    for (int i = 0; i < b.n; i++) { float a = fabsf(b.b[i]); if (a > peak) peak = a; sum += b.b[i] * b.b[i]; }
    float rms = sqrtf(sum / (float)(b.n > 0 ? b.n : 1));
    if (peak > 1e-6f && rms > 1e-6f) {
        float g = fminf(0.92f / peak, 0.19f / rms);
        for (int i = 0; i < b.n; i++) b.b[i] *= g;
    }
    return b;
}

void Sfx_Init(void)
{
    if (!IsAudioDeviceReady()) return;
    for (int i = 0; i < SFX_COUNT; i++) {
        Buf b = Make((SfxId)i);
        short *pcm = (short *)MemAlloc((unsigned int)(b.n * (int)sizeof(short)));
        for (int k = 0; k < b.n; k++) {
            float v = b.b[k];
            v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            pcm[k] = (short)(v * 32000.0f);
        }
        free(b.b);
        Wave w = { (unsigned int)b.n, SR, 16, 1, pcm };
        if (G.debug.autotest) ExportWave(w, TextFormat("autotest_sfx_%02d.wav", i));
        sSound[i][0] = LoadSoundFromWave(w);
        UnloadWave(w);
        for (int v = 1; v < VOICES; v++) sSound[i][v] = LoadSoundAlias(sSound[i][0]);
        sNext[i] = 0;
        sLast[i] = -100.0f;
    }
    sReady = true;
}

void Sfx_Shutdown(void)
{
    if (!sReady) return;
    for (int i = 0; i < SFX_COUNT; i++) {
        for (int v = 1; v < VOICES; v++) UnloadSoundAlias(sSound[i][v]);
        UnloadSound(sSound[i][0]);
    }
    sReady = false;
}

bool Sfx_Ready(void) { return sReady; }

static void PlayPan(SfxId id, float vol, float pitch, float pan /* -1 left .. 1 right */)
{
    if (!sReady || id < 0 || id >= SFX_COUNT) return;
    float v = vol * G.settings.sfx;
    if (v <= 0.001f) return;
    int k = sNext[id];
    sNext[id] = (k + 1) % VOICES;
    Sound s = sSound[id][k];
    SetSoundVolume(s, v > 1.0f ? 1.0f : v);
    SetSoundPitch(s, pitch);
#if RAYLIB_VERSION_MAJOR >= 6
    SetSoundPan(s, pan);
#else
    SetSoundPan(s, 0.5f - 0.5f * pan);
#endif
    PlaySound(s);
    sLast[id] = (float)GetTime();
}

void Sfx_Play(SfxId id, float vol, float pitch) { PlayPan(id, vol, pitch, 0.0f); }

void Sfx_Play3D(SfxId id, Vector3 pos, float vol)
{
    Camera3D c = G.rig.cam;
    Vector3 to = Vector3Subtract(pos, c.position);
    float d = Vector3Length(to);
    if (d > 140.0f) return;
    float att = 1.0f / (1.0f + (d / 22.0f) * (d / 22.0f));
    Vector3 fwd = Vector3Normalize(Vector3Subtract(c.target, c.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, c.up));
    float pan = d > 0.01f ? Vector3DotProduct(Vector3Scale(to, 1.0f / d), right) * 0.8f : 0.0f;
    float pitch = 1.0f + (Rand01() - 0.5f) * 0.08f;
    PlayPan(id, vol * att, pitch, pan);
}

void Sfx_PlayThrottled(SfxId id, float vol, float minInterval)
{
    if (id < 0 || id >= SFX_COUNT) return;
    if ((float)GetTime() - sLast[id] < minInterval) return;
    PlayPan(id, vol, 1.0f + (Rand01() - 0.5f) * 0.1f, 0.0f);
}

void Sfx_Update(float dt)
{
    (void)dt;
    if (IsAudioDeviceReady()) SetMasterVolume(G.settings.master);
}

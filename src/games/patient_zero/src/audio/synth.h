/*
 * synth.h - tiny DSP toolkit used to build every sound procedurally:
 * oscillators, noise, ADSR, one-pole/state-variable filters, a waveshaper
 * and a Freeverb-style comb/allpass reverb for the "wet, inside a body" tail.
 */
#ifndef PZ_SYNTH_H
#define PZ_SYNTH_H
#include <stdint.h>
#include <stdbool.h>

#define SYNTH_RATE 44100

typedef struct { float low, band; } Svf;               /* state-variable filter */
typedef struct { float z; } OnePole;

float Syn_Sine(float ph);                               /* ph in cycles          */
float Syn_Saw(float ph);
float Syn_Square(float ph, float pw);
float Syn_Tri(float ph);
float Syn_Noise(uint32_t *state);                       /* [-1,1]                */
float Syn_Env(float t, float a, float d, float s, float r, float len);
float Syn_Exp(float t, float tau);                      /* exp decay             */
float Syn_SvfLP(Svf *f, float in, float cutoff, float q);
float Syn_SvfBP(Svf *f, float in, float cutoff, float q);
float Syn_SvfHP(Svf *f, float in, float cutoff, float q);
float Syn_OnePoleLP(OnePole *f, float in, float cutoff);
float Syn_Drive(float x, float amt);
float Syn_MidiHz(float note);

/* Simple mono reverb applied in place to a buffer. */
void  Syn_Reverb(float *buf, int n, float room, float damp, float wet);
void  Syn_Normalize(float *buf, int n, float peak);
void  Syn_FadeEdges(float *buf, int n, int fadeIn, int fadeOut);

#endif

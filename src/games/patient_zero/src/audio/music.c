/*
 * music.c - the adaptive score, synthesised sample by sample into a raylib
 * AudioStream. Musical material is derived from the region (root, tempo,
 * mode); layer gains chase targets derived from game state so transitions
 * are always smooth crossfades.
 *
 *   L0 drone pad     - detuned saw chord through a breathing low-pass
 *   L1 heartbeat     - "lub-dub", tempo locked to Alert (also drives visuals)
 *   L2 bass pulse    - eighth-note root/fifth plucks when threatened
 *   L3 percussion    - kick / tom / shaker groove in combat and boss fights
 *   L4 motif         - pentatonic bell arpeggios, richer with each stage
 *   L5 boss          - distorted syncopated stabs + low choir
 *   L6 fluid bed     - brown-noise wash + bubbles, follows swim speed
 * A stereo feedback delay gives everything the wet, enclosed space of a body.
 */
#include "audio/music.h"
#include "audio/synth.h"
#include "world/region.h"
#include "entities/enemies.h"
#include <string.h>
#include <stdlib.h>

#define MSR 44100
#define FRAMES 2048
#define DELAY_MAX (MSR * 2)

static AudioStream sStream;
static bool sReady = false;
static short sOut[FRAMES * 2];

typedef struct {
    double t;                 /* seconds since start              */
    float gain[7], target[7];
    float beatPhase;          /* in beats                         */
    int lastStep;
    float root;               /* Hz                               */
    float tempo;
    int mode;
    int chord;                /* index into progression           */
    /* voices */
    float padPh[6];
    Svf padF[2];
    float hbPhase, hbEnv, hbEnv2, hbPitchPh, hbBpm;
    float bassPh, bassEnv, bassFreq;
    Svf bassF;
    float kickEnv, kickPh, tomEnv, tomPh, tomFreq, hatEnv;
    Svf hatF;
    float bellPh[4], bellEnv[4], bellFreq[4];
    int bellNext;
    float bossPh, bossEnv, bossFreq;
    Svf bossF, choirF;
    float choirPh[3];
    float brown;
    Svf fluidF;
    float bubbleEnv, bubblePh, bubbleFreq;
    /* stinger */
    float stEnv, stT;
    int stKind;
    float stPh[4];
    /* delay */
    float *dl, *dr;
    int dw;
    OnePole dlp, drp;
    /* master */
    Svf mlpL, mlpR;
    float muffle;
    uint32_t seed;
    float heartOut;
    float speedSmooth;
} MusicState;

static MusicState M;

/* autotest: capture the first seconds of the score for offline analysis */
#define CAPTURE_FRAMES (MSR * 24)
static short *sCapture = NULL;
static int sCaptured = 0;

static const int SCALES[4][7] = {
    { 0, 2, 3, 5, 7, 8, 10 },   /* aeolian    - blood      */
    { 0, 2, 3, 5, 7, 9, 10 },   /* dorian     - lungs/lymph */
    { 0, 1, 3, 5, 7, 8, 10 },   /* phrygian   - gut        */
    { 0, 1, 3, 5, 6, 8, 10 },   /* locrian    - brain      */
};
static const int PROG[4] = { 0, 5, 3, 4 };   /* i - VI - iv - v (scale degrees) */

static float DegreeHz(int degree, int octave)
{
    int oct = degree / 7 + octave;
    int d = degree % 7;
    if (d < 0) { d += 7; oct--; }
    float semis = (float)SCALES[M.mode & 3][d] + 12.0f * (float)oct;
    return M.root * powf(2.0f, semis / 12.0f);
}

static float Rnd(void) { return Syn_Noise(&M.seed) * 0.5f + 0.5f; }

void Music_Init(void)
{
    if (!IsAudioDeviceReady()) return;
    memset(&M, 0, sizeof(M));
    M.seed = 0xC0FFEEu;
    M.root = 110.0f;
    M.tempo = 84.0f;
    M.hbBpm = 60.0f;
    M.dl = (float *)calloc(DELAY_MAX, sizeof(float));
    M.dr = (float *)calloc(DELAY_MAX, sizeof(float));
    SetAudioStreamBufferSizeDefault(FRAMES);
    sStream = LoadAudioStream(MSR, 16, 2);
    SetAudioStreamBufferSizeDefault(0);
    memset(sOut, 0, sizeof(sOut));
    UpdateAudioStream(sStream, sOut, FRAMES);
    UpdateAudioStream(sStream, sOut, FRAMES);
    PlayAudioStream(sStream);
    sReady = true;
}

void Music_Shutdown(void)
{
    if (!sReady) return;
    StopAudioStream(sStream);
    UnloadAudioStream(sStream);
    free(M.dl);
    free(M.dr);
    free(sCapture);
    sCapture = NULL;
    sReady = false;
}

void Music_Stinger(int kind)
{
    M.stKind = kind;
    M.stT = 0.0f;
    M.stEnv = 1.0f;
    for (int i = 0; i < 4; i++) M.stPh[i] = Rnd();
}

float Music_Heartbeat(void) { return M.heartOut; }

/* ------------------------------------------------------------------------- */

static void UpdateTargets(void)
{
    GameState s = G.state;
    bool menu = !G.run.active || s == GS_MENU || s == GS_NEWGAME || s == GS_CREDITS || s == GS_LOADING;
    bool boss = G.boss.active && !G.boss.dying;
    float alert = G.region.alert / ALERT_MAX;
    float threat = Sat(alert * 1.2f + (Enemies_Detected() ? 0.25f : 0.0f));
    if (boss) threat = 1.0f;
    for (int i = 0; i < 7; i++) M.target[i] = 0.0f;
    M.target[0] = menu ? 0.55f : 0.42f;
    M.target[6] = menu ? 0.4f : 0.6f;
    if (!menu) {
        M.target[1] = 0.35f + 0.65f * Sat(alert * 1.4f);
        M.target[2] = SmoothStepF(0.15f, 0.45f, threat);
        M.target[3] = SmoothStepF(0.45f, 0.8f, threat);
        M.target[4] = 0.35f + 0.1f * (float)G.player.stage - 0.3f * threat;
        M.target[5] = boss ? 1.0f : 0.0f;
        if (G.player.dead) { for (int i = 1; i < 6; i++) M.target[i] = 0.0f; M.target[0] = 0.6f; }
    } else {
        M.target[4] = 0.45f;
    }
    if (G.state == GS_VICTORY) { M.target[2] = 0; M.target[3] = 0; M.target[5] = 0; M.target[4] = 0.7f; }

    const RegionDef *d = Region_Def();
    float wantRoot = d->musicRoot;
    M.root += (wantRoot - M.root) * 0.02f;
    M.tempo = (float)d->musicTempo + (boss ? 18.0f : 0.0f) + threat * 10.0f;
    M.mode = d->musicMode;
    M.hbBpm = 58.0f + 92.0f * Sat(alert) + (boss ? 20.0f : 0.0f);
    bool muffled = (s == GS_PAUSE || s == GS_MUTATION || s == GS_SETTINGS || s == GS_BESTIARY) && G.run.active;
    float wantMuffle = muffled ? 1.0f : 0.0f;
    M.muffle += (wantMuffle - M.muffle) * 0.1f;
    float sp = G.run.active ? Vector3Length(G.player.vel) : 3.0f;
    M.speedSmooth += (sp - M.speedSmooth) * 0.1f;
}

static void OnStep(int step)
{
    /* 16th-note sequencer */
    int s16 = step & 15;
    int bar = step / 16;
    if (s16 == 0) M.chord = PROG[(bar / 2) & 3];
    float threat = M.gain[2];
    /* bass: 8ths, root with fifth on off-beats */
    if ((s16 & 1) == 0) {
        int deg = M.chord + ((s16 & 4) ? 4 : 0);
        M.bassFreq = DegreeHz(deg, -1);
        M.bassEnv = 1.0f;
    }
    /* percussion */
    if (s16 == 0 || s16 == 8 || (s16 == 10 && threat > 0.6f)) { M.kickEnv = 1.0f; M.kickPh = 0.0f; }
    if (s16 == 6 || s16 == 14 || (s16 == 15 && (bar & 1))) { M.tomEnv = 1.0f; M.tomFreq = 140.0f + 40.0f * (float)(s16 & 3); }
    if ((s16 & 1) == 1 || Rnd() < 0.2f) M.hatEnv = 0.6f + 0.4f * Rnd();
    /* motif: pentatonic arpeggio, sparser when calm */
    float density = 0.25f + 0.08f * (float)G.player.stage;
    if ((s16 % 2 == 0 && Rnd() < density) || s16 == 0) {
        static const int PENTA[5] = { 0, 2, 4, 7, 9 };
        int k = M.bellNext;
        M.bellNext = (k + 1) & 3;
        int idx = (int)(Rnd() * 5.0f) % 5;
        float semis = (float)PENTA[idx] + 12.0f * (float)((int)(Rnd() * 2.0f));
        float chordOff = (float)SCALES[M.mode & 3][M.chord % 7];
        M.bellFreq[k] = M.root * 4.0f * powf(2.0f, (semis + chordOff) / 12.0f);
        M.bellEnv[k] = 0.6f + 0.4f * Rnd();
    }
    /* boss stabs: syncopated */
    static const int BOSSPAT[16] = { 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0 };
    if (BOSSPAT[s16]) { M.bossEnv = 1.0f; M.bossFreq = DegreeHz(M.chord, -2) * ((s16 == 12) ? 1.5f : 1.0f); }
    /* random bubbles in the fluid bed */
    if (Rnd() < 0.25f) { M.bubbleEnv = 1.0f; M.bubbleFreq = 250.0f + Rnd() * 700.0f; M.bubblePh = 0.0f; }
}

static void Render(short *out, int frames)
{
    float dt = 1.0f / (float)MSR;
    int delayLen = (int)(60.0f / M.tempo * 0.75f * (float)MSR);
    if (delayLen >= DELAY_MAX) delayLen = DELAY_MAX - 1;
    if (delayLen < 100) delayLen = 100;
    float musicVol = G.settings.music;
    float ambVol = G.settings.sfx;
    for (int i = 0; i < frames; i++) {
        for (int k = 0; k < 7; k++) M.gain[k] += (M.target[k] - M.gain[k]) * 0.00004f;
        M.t += dt;
        float L = 0.0f, R = 0.0f, send = 0.0f;

        /* sequencer clock */
        M.beatPhase += dt * M.tempo / 60.0f;
        int step = (int)(M.beatPhase * 4.0f);
        if (step != M.lastStep) { M.lastStep = step; OnStep(step); }

        /* L0 pad */
        if (M.gain[0] > 0.001f) {
            float f0 = DegreeHz(M.chord, -1), f1 = DegreeHz(M.chord + 2, -1), f2 = DegreeHz(M.chord + 4, -1);
            float fr[6] = { f0, f0 * 1.006f, f1, f1 * 0.995f, f2, f2 * 1.004f };
            float pl = 0.0f, pr = 0.0f;
            for (int v = 0; v < 6; v++) {
                M.padPh[v] += fr[v] * dt;
                if (M.padPh[v] > 1.0f) M.padPh[v] -= 1.0f;
                float o = Syn_Saw(M.padPh[v]);
                if (v & 1) pr += o; else pl += o;
            }
            float cut = 380.0f + 520.0f * (0.5f + 0.5f * sinf((float)M.t * 0.21f)) + 400.0f * M.gain[2];
            pl = Syn_SvfLP(&M.padF[0], pl, cut, 0.9f);
            pr = Syn_SvfLP(&M.padF[1], pr, cut * 1.03f, 0.9f);
            float g = M.gain[0] * 0.09f * musicVol;
            L += pl * g; R += pr * g;
            send += (pl + pr) * g * 0.3f;
        }

        /* L1 heartbeat: lub (strong) + dub (weaker, 0.28 beat later) */
        {
            float prev = M.hbPhase;
            M.hbPhase += dt * M.hbBpm / 60.0f;
            if (M.hbPhase >= 1.0f) { M.hbPhase -= 1.0f; M.hbEnv = 1.0f; M.hbPitchPh = 0.0f; }
            if (prev < 0.28f && M.hbPhase >= 0.28f) M.hbEnv2 = 0.7f;
            M.hbEnv *= expf(-dt * 14.0f);
            M.hbEnv2 *= expf(-dt * 16.0f);
            float e = M.hbEnv + M.hbEnv2;
            M.hbPitchPh += (42.0f + 40.0f * e) * dt;
            float o = Syn_Sine(M.hbPitchPh) * e;
            float g = M.gain[1] * 0.55f * musicVol;
            L += o * g; R += o * g;
            M.heartOut = Sat(e) * (0.3f + 0.7f * M.gain[1]);
        }

        /* L2 bass */
        if (M.gain[2] > 0.001f) {
            M.bassEnv *= expf(-dt * 7.0f);
            M.bassPh += M.bassFreq * dt;
            float o = Syn_Saw(M.bassPh) * 0.6f + Syn_Square(M.bassPh * 0.5f, 0.5f) * 0.4f;
            o = Syn_SvfLP(&M.bassF, o, 180.0f + 1400.0f * M.bassEnv * M.bassEnv, 1.6f) * M.bassEnv;
            float g = M.gain[2] * 0.35f * musicVol;
            L += o * g; R += o * g;
        }

        /* L3 percussion */
        if (M.gain[3] > 0.001f) {
            M.kickEnv *= expf(-dt * 9.0f);
            M.kickPh += (45.0f + 120.0f * M.kickEnv * M.kickEnv) * dt;
            float kick = Syn_Sine(M.kickPh) * M.kickEnv;
            M.tomEnv *= expf(-dt * 10.0f);
            M.tomPh += M.tomFreq * (0.8f + 0.4f * M.tomEnv) * dt;
            float tom = Syn_Sine(M.tomPh) * M.tomEnv * 0.6f;
            M.hatEnv *= expf(-dt * 40.0f);
            float hat = Syn_SvfHP(&M.hatF, Syn_Noise(&M.seed), 6000.0f, 0.8f) * M.hatEnv * 0.25f;
            float g = M.gain[3] * 0.6f * musicVol;
            L += (kick + tom * 0.8f + hat * 0.7f) * g;
            R += (kick + tom * 1.0f + hat * 1.2f) * g;
        }

        /* L4 bells */
        if (M.gain[4] > 0.001f) {
            float bl = 0.0f, br = 0.0f;
            for (int k = 0; k < 4; k++) {
                if (M.bellEnv[k] < 0.0005f) continue;
                M.bellEnv[k] *= expf(-dt * 2.6f);
                M.bellPh[k] += M.bellFreq[k] * dt;
                float o = (Syn_Sine(M.bellPh[k]) + 0.3f * Syn_Sine(M.bellPh[k] * 2.76f) + 0.12f * Syn_Sine(M.bellPh[k] * 5.4f)) * M.bellEnv[k];
                if (k & 1) br += o; else bl += o;
            }
            float g = M.gain[4] * 0.08f * musicVol;
            L += bl * g + br * g * 0.4f;
            R += br * g + bl * g * 0.4f;
            send += (bl + br) * g * 0.9f;
        }

        /* L5 boss */
        if (M.gain[5] > 0.001f) {
            M.bossEnv *= expf(-dt * 6.0f);
            M.bossPh += M.bossFreq * dt;
            float o = Syn_Drive((Syn_Saw(M.bossPh) + Syn_Saw(M.bossPh * 1.01f)) * 0.5f, 3.0f);
            o = Syn_SvfLP(&M.bossF, o, 300.0f + 2000.0f * M.bossEnv, 1.2f) * M.bossEnv;
            float ch = 0.0f;
            float base = DegreeHz(M.chord, 0);
            float cf[3] = { base, base * 1.19f, base * 1.5f };
            for (int v = 0; v < 3; v++) { M.choirPh[v] += cf[v] * dt; ch += Syn_Saw(M.choirPh[v]); }
            ch = Syn_SvfBP(&M.choirF, ch, 700.0f + 200.0f * sinf((float)M.t * 0.7f), 4.0f);
            float g = M.gain[5] * musicVol;
            L += (o * 0.35f + ch * 0.06f) * g;
            R += (o * 0.35f + ch * 0.06f) * g;
            send += ch * 0.05f * g;
        }

        /* L6 fluid bed */
        if (M.gain[6] > 0.001f) {
            float w = Syn_Noise(&M.seed);
            M.brown = M.brown * 0.995f + w * 0.05f;
            float cut = 160.0f + M.speedSmooth * 18.0f;
            float o = Syn_SvfLP(&M.fluidF, M.brown, cut, 0.9f) * 2.2f;
            M.bubbleEnv *= expf(-dt * 30.0f);
            M.bubbleFreq *= 1.0f + dt * 3.0f;
            M.bubblePh += M.bubbleFreq * dt;
            float bub = Syn_Sine(M.bubblePh) * M.bubbleEnv * 0.15f;
            float g = M.gain[6] * ambVol * (0.6f + 0.02f * M.speedSmooth);
            L += (o + bub) * g;
            R += (o * 0.9f + bub * 0.6f) * g;
        }

        /* stingers */
        if (M.stEnv > 0.001f) {
            M.stT += dt;
            float sl = 0.0f;
            static const float ST[5][4] = { { 0, 7, 12, 16 }, { 0, 4, 7, 12 }, { 0, 1, 6, 7 }, { 0, 4, 7, 11 }, { 0, 3, 6, 10 } };
            int kind = M.stKind < 0 ? 0 : (M.stKind > 4 ? 4 : M.stKind);
            for (int v = 0; v < 4; v++) {
                float f = M.root * 2.0f * powf(2.0f, ST[kind][v] / 12.0f);
                if (kind == 4) f *= powf(0.5f, M.stT * 0.4f);
                float onset = (kind == 3 || kind == 0) ? 0.09f * (float)v : 0.0f;
                if (M.stT < onset) continue;
                M.stPh[v] += f * dt;
                sl += (Syn_Saw(M.stPh[v]) * 0.4f + Syn_Sine(M.stPh[v]) * 0.6f);
            }
            float att = fminf(1.0f, M.stT / (kind == 2 ? 0.02f : 0.15f));
            M.stEnv = (M.stT < 0.2f) ? 1.0f : expf(-(M.stT - 0.2f) * (kind == 1 ? 0.7f : 1.2f));
            float o = sl * att * M.stEnv * 0.07f * musicVol;
            L += o; R += o;
            send += o * 0.8f;
        }

        /* stereo feedback delay (ping-pong, damped) */
        int rd = M.dw - delayLen;
        if (rd < 0) rd += DELAY_MAX;
        float yl = M.dl[rd], yr = M.dr[rd];
        M.dl[M.dw] = send + Syn_OnePoleLP(&M.drp, yr, 2500.0f) * 0.45f;
        M.dr[M.dw] = send * 0.6f + Syn_OnePoleLP(&M.dlp, yl, 2500.0f) * 0.45f;
        M.dw = (M.dw + 1) % DELAY_MAX;
        L += yl * 0.5f;
        R += yr * 0.5f;

        /* muffle when the game is paused / genome editor open */
        if (M.muffle > 0.01f) {
            float c = 18000.0f * (1.0f - M.muffle) + 500.0f * M.muffle;
            L = Syn_SvfLP(&M.mlpL, L, c, 0.7f);
            R = Syn_SvfLP(&M.mlpR, R, c, 0.7f);
        }
        L = tanhf(L * 0.9f);
        R = tanhf(R * 0.9f);
        out[i * 2] = (short)(L * 30000.0f);
        out[i * 2 + 1] = (short)(R * 30000.0f);
    }
}

void Music_Update(float dt)
{
    (void)dt;
    if (!sReady) return;
    UpdateTargets();
    int guard = 0;
    while (IsAudioStreamProcessed(sStream) && guard++ < 4) {
        Render(sOut, FRAMES);
        UpdateAudioStream(sStream, sOut, FRAMES);
        if (G.debug.autotest) {
            if (!sCapture) sCapture = (short *)calloc((size_t)CAPTURE_FRAMES * 2, sizeof(short));
            if (sCapture && sCaptured + FRAMES <= CAPTURE_FRAMES) {
                memcpy(sCapture + sCaptured * 2, sOut, sizeof(sOut));
                sCaptured += FRAMES;
                if (sCaptured + FRAMES > CAPTURE_FRAMES) {
                    Wave w = { (unsigned int)sCaptured, MSR, 16, 2, sCapture };
                    ExportWave(w, "autotest_music.wav");
                }
            }
        }
    }
}

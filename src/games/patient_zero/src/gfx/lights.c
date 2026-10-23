/*
 * lights.c - selects up to 16 point lights per frame by perceptual weight
 * (intensity * radius / distance) so distant fireworks never steal a slot
 * from the glow right next to the player.
 */
#include "gfx/lights.h"
#include "gfx/shaders.h"
#include "core/settings.h"

typedef struct { Vector3 pos, col; float radius, life, maxLife; } TempLight;

static PointLight sCand[MAX_LIGHT_CANDS];
static int sCandCount = 0;
static TempLight sTemp[64];
static int sCommitted = 0;

void Lights_Begin(void) { sCandCount = 0; }

void Lights_Add(Vector3 pos, Vector3 color, float radius)
{
    if (sCandCount >= MAX_LIGHT_CANDS) return;
    sCand[sCandCount++] = (PointLight){ pos, color, radius };
}

void Lights_AddTemp(Vector3 pos, Vector3 color, float radius, float life)
{
    int best = 0;
    for (int i = 0; i < 64; i++) {
        if (sTemp[i].life <= 0.0f) { best = i; break; }
        if (sTemp[i].life < sTemp[best].life) best = i;
    }
    sTemp[best] = (TempLight){ pos, color, radius, life, life };
}

void Lights_UpdateTemp(float dt)
{
    for (int i = 0; i < 64; i++) if (sTemp[i].life > 0.0f) sTemp[i].life -= dt;
}

void Lights_ClearTemp(void)
{
    for (int i = 0; i < 64; i++) sTemp[i].life = 0.0f;
}

void Lights_Commit(Vector3 camPos)
{
    for (int i = 0; i < 64; i++) {
        if (sTemp[i].life <= 0.0f) continue;
        float k = sTemp[i].life / sTemp[i].maxLife;
        Lights_Add(sTemp[i].pos, Vector3Scale(sTemp[i].col, k * k), sTemp[i].radius * (0.6f + 0.4f * k));
    }
    int budget = Settings_LightBudget();
    float score[MAX_LIGHT_CANDS];
    for (int i = 0; i < sCandCount; i++) {
        float d = Vector3Distance(sCand[i].pos, camPos);
        float lum = sCand[i].color.x * 0.3f + sCand[i].color.y * 0.6f + sCand[i].color.z * 0.1f;
        score[i] = lum * sCand[i].radius / (d + sCand[i].radius * 0.5f + 1.0f);
        if (d - sCand[i].radius > 140.0f) score[i] = -1.0f;
    }
    Vector3 pos[MAX_LIGHTS], col[MAX_LIGHTS];
    float rad[MAX_LIGHTS];
    int n = 0;
    while (n < budget) {
        int best = -1;
        for (int i = 0; i < sCandCount; i++) if (score[i] > 0.0f && (best < 0 || score[i] > score[best])) best = i;
        if (best < 0) break;
        pos[n] = sCand[best].pos; col[n] = sCand[best].color; rad[n] = sCand[best].radius;
        score[best] = -1.0f;
        n++;
    }
    sCommitted = n;
    Shaders_SetLights(pos, col, rad, n);
}

int Lights_Count(void) { return sCommitted; }
int Lights_Candidates(void) { return sCandCount; }

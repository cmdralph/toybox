/*
 * immune_memory.c - resistance grows with use (diminishing as it nears 1),
 * decays when a tactic is abandoned, and partially carries between regions
 * the way real immunological memory persists after an infection.
 */
#include "systems/immune_memory.h"
#include "core/settings.h"
#include "ui/hud.h"
#include "audio/sfx.h"
#include <stdio.h>
#include <string.h>

static const char *NAMES[IM_COUNT] = { "Toxins", "Dashing", "Swarming", "Stealth", "Lysis bursts" };
static const char *COUNTERS[IM_COUNT] = {
    "Immune cells are growing toxin-resistant shells",
    "Enemies move faster and predict your dashes",
    "More macrophages are hunting your virions",
    "Immune cells see further and sniff you out",
    "Immune cells toughen up and spread out against bursts",
};

const char *Immune_CatName(ImmuneCat c) { return NAMES[c]; }
const char *Immune_CounterText(ImmuneCat c) { return COUNTERS[c]; }

void Immune_Reset(bool carry)
{
    ImmuneMemory *m = &G.immune;
    int adaptations = m->adaptations;
    memset(m, 0, sizeof(*m));
    m->adaptations = adaptations;
    for (int i = 0; i < IM_COUNT; i++) {
        m->resist[i] = carry ? G.run.immuneCarry[i] * IMMUNE_CARRY : 0.0f;
        m->announced[i] = (m->resist[i] >= IMMUNE_ANNOUNCE_2) ? 2 : (m->resist[i] >= IMMUNE_ANNOUNCE_1 ? 1 : 0);
    }
}

void Immune_StoreCarry(void)
{
    for (int i = 0; i < IM_COUNT; i++) G.run.immuneCarry[i] = G.immune.resist[i];
}

float Immune_Resist(ImmuneCat c) { return G.immune.resist[c]; }

void Immune_Use(ImmuneCat c, float amount)
{
    ImmuneMemory *m = &G.immune;
    float rate = DIFFICULTIES[G.run.difficulty].adaptRate * G.stats.adaptMul;
    m->usage[c] += amount;
    m->resist[c] += amount * 0.010f * rate * (1.0f - m->resist[c]);
    m->resist[c] = Sat(m->resist[c]);
    m->lastUse[c] = G.region.time;
    float thr[3] = { IMMUNE_ANNOUNCE_1, IMMUNE_ANNOUNCE_2, IMMUNE_ANNOUNCE_3 };
    if (m->announced[c] < 3 && m->resist[c] >= thr[m->announced[c]]) {
        m->announced[c]++;
        m->adaptations++;
        char title[64];
        snprintf(title, sizeof(title), "THE BODY ADAPTS: %s  (%d%%)", NAMES[c], (int)(m->resist[c] * 100.0f));
        HUD_Toast(title, COUNTERS[c], (Color){ 255, 120, 90, 255 });
        Sfx_Play(SFX_ADAPT, 1.0f, 1.0f);
        HUD_Hint(TUT_ADAPT, "The immune system learns your tactics. Switch abilities to stay ahead of it (see the IMMUNE MEMORY panel).");
    }
}

void Immune_Update(float dt)
{
    ImmuneMemory *m = &G.immune;
    for (int i = 0; i < IM_COUNT; i++) {
        if (G.region.time - m->lastUse[i] > 12.0f) m->resist[i] = fmaxf(0.0f, m->resist[i] - IMMUNE_DECAY * G.stats.adaptDecayMul * dt);
        /* allow re-announcing after the meter falls back */
        float thr[3] = { IMMUNE_ANNOUNCE_1, IMMUNE_ANNOUNCE_2, IMMUNE_ANNOUNCE_3 };
        while (m->announced[i] > 0 && m->resist[i] < thr[m->announced[i] - 1] - 0.1f) m->announced[i]--;
    }
}

void Immune_ApplyToSpawn(Agent *a)
{
    ImmuneMemory *m = &G.immune;
    if (a->team != TEAM_IMMUNE || a->kind == AK_ANTIBODY) return;
    float tox = m->resist[IM_TOXIN];
    if (tox > 0.15f && GetRandomValue(0, 100) < (int)(tox * 100.0f)) {
        a->flags |= AF_SHIELDED;
        a->shield = 0.25f + 0.45f * tox;
    }
    a->speedMul *= 1.0f + 0.45f * m->resist[IM_DASH];
    a->vision *= 1.0f + 0.5f * m->resist[IM_STEALTH];
    float hpb = 1.0f + 0.4f * m->resist[IM_BURST];
    a->hp *= hpb;
    a->maxHp *= hpb;
}

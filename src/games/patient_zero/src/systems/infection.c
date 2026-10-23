/*
 * infection.c - the core verb. A viral capsid docks on a receptor, then
 * injects its genome; here that becomes a short rhythm interaction: a pulse
 * ring contracts toward the membrane and tapping on the beat accelerates
 * injection. Missing the beat makes noise (Alert) and loses progress.
 */
#include "systems/infection.h"
#include "systems/spatial_hash.h"
#include "core/pool.h"
#include "systems/combat.h"
#include "entities/player.h"
#include "entities/minions.h"
#include "gfx/particles.h"
#include "gfx/lights.h"
#include "gfx/camera.h"
#include "audio/sfx.h"
#include "ui/hud.h"
#include "ui/bestiary_ui.h"
#include "core/rng.h"
#include "world/region.h"
#include <stdio.h>

AgentRef Infection_FindTarget(void)
{
    Player *p = &G.player;
    int ids[32];
    int n = SH_Query(p->pos, p->radius + 4.0f + INFECT_RANGE, ids, 32, KMASK(AK_HOST));
    Vector3 fwd = G.rig.cam.target.x == 0 && G.rig.cam.target.y == 0 ? p->forward : Vector3Normalize(Vector3Subtract(G.rig.cam.target, G.rig.cam.position));
    float best = 1e9f;
    int bi = -1;
    for (int k = 0; k < n; k++) {
        Agent *h = &G.agents[ids[k]];
        if (h->flags & AF_INFECTED) continue;
        Vector3 to = Vector3Subtract(h->pos, p->pos);
        float d = Vector3Length(to) - h->radius - p->radius;
        if (d > INFECT_RANGE) continue;
        float align = (Vector3Length(to) > 0.01f) ? Vector3DotProduct(Vector3Normalize(to), fwd) : 1.0f;
        float score = d - align * 2.0f;
        if (score < best) { best = score; bi = ids[k]; }
    }
    return Agent_Ref(bi);
}

bool Infection_TryAttach(void)
{
    AgentRef r = Infection_FindTarget();
    Agent *h = Agent_Get(r);
    if (!h) return false;
    G.player.attach = r;
    G.player.rhythmPhase = 0.0f;
    G.player.rhythmCombo = 0;
    Sfx_Play(SFX_LATCH, 1.0f, 1.0f);
    Codex_Unlock(Region_Def()->codexHost);
    HUD_Hint(TUT_RHYTHM, "Keep holding RMB to inject. Tap LMB when the pulse ring meets the membrane for a big bonus.");
    return true;
}

bool Infection_InSweetSpot(void)
{
    float ph = G.player.rhythmPhase;
    return ph > 1.0f - RHYTHM_WINDOW || ph < RHYTHM_WINDOW * 0.35f;
}

void Infection_Update(float dt)
{
    Player *p = &G.player;
    Agent *h = Agent_Get(p->attach);
    if (!h || (h->flags & AF_INFECTED)) { p->attach = REF_NONE; return; }
    if (!G.in.infect) {
        p->attach = REF_NONE;
        Sfx_Play(SFX_DETACH, 0.6f, 1.0f);
        return;
    }
    /* dock on the membrane facing the player */
    Vector3 out = Vector3Subtract(p->pos, h->pos);
    if (Vector3Length(out) < 0.01f) out = h->anchor;
    out = Vector3Normalize(out);
    Vector3 dock = Vector3Add(h->pos, Vector3Scale(out, h->radius + p->radius * 0.85f));
    p->vel = Vector3Scale(Vector3Subtract(dock, p->pos), 8.0f);
    p->forward = Vector3Negate(out);

    float prevPhase = p->rhythmPhase;
    p->rhythmPhase += dt * RHYTHM_HZ;
    if (p->rhythmPhase >= 1.0f) { p->rhythmPhase -= 1.0f; Sfx_Play(SFX_TICK, 0.35f, 1.0f + 0.05f * (float)p->rhythmCombo); }
    (void)prevPhase;

    float rate = G.stats.infectSpeed / INJECT_BASE_TIME;
    h->progress += rate * dt;
    G.region.alert = fminf(ALERT_MAX, G.region.alert + 1.0f * G.stats.injectAlertMul * G.stats.alertMul * dt);
    G.region.alertDelay = fmaxf(G.region.alertDelay, 0.5f);

    if (G.in.firePressed) {
        if (Infection_InSweetSpot()) {
            float bonus = G.stats.perfectRhythm ? RHYTHM_GOOD_BONUS * 2.0f : RHYTHM_GOOD_BONUS;
            h->progress += bonus;
            p->rhythmCombo++;
            if (p->rhythmCombo > p->bestCombo) p->bestCombo = p->rhythmCombo;
            if (p->rhythmCombo > G.run.stats.bestCombo) G.run.stats.bestCombo = p->rhythmCombo;
            p->rhythmFlash = 1.0f;
            Sfx_Play(SFX_BEAT_GOOD, 0.9f, 1.0f + 0.07f * (float)(p->rhythmCombo > 8 ? 8 : p->rhythmCombo));
            Particles_Burst(dock, 14, 6.0f, V4(0.6f, 2.6f, 1.4f, 1.0f), V4(0.2f, 0.8f, 0.4f, 0.0f), 0.18f, 0.4f, PF_ADD);
            if (p->rhythmCombo >= 5) HUD_Achievement(ACH_RHYTHM);
        } else {
            h->progress = fmaxf(0.0f, h->progress - RHYTHM_BAD_PENALTY);
            p->rhythmCombo = 0;
            p->rhythmFlash = -1.0f;
            G.region.alert = fminf(ALERT_MAX, G.region.alert + 2.0f * G.stats.injectAlertMul * G.stats.alertMul);
            Sfx_Play(SFX_BEAT_BAD, 0.7f, 1.0f);
        }
    }
    /* genome stream from capsid into the cell */
    if (GetRandomValue(0, 100) < 60) {
        Vector3 from = Vector3Add(p->pos, Vector3Scale(Rng_UnitVecG(), p->radius * 0.4f));
        Particles_Emit(from, Vector3Scale(Vector3Subtract(h->pos, from), 2.2f), V4(0.4f, 2.0f, 1.0f, 1.0f), V4(0.2f, 1.2f, 0.6f, 0.0f), 0.12f, 0.04f, 0.45f, 0.5f, PF_ADD);
    }
    h->flash = fmaxf(h->flash, 0.3f * sinf(G.realTime * 12.0f) + 0.3f);
    if (h->progress >= 1.0f) Infection_Complete(h);
}

void Infection_Complete(Agent *h)
{
    Player *p = &G.player;
    h->flags |= AF_INFECTED;
    h->progress = 0.0f;
    h->produce = 0.3f;
    G.region.hostInfected++;
    G.run.stats.infections++;
    G.profile.totalInfections++;
    int combo = p->rhythmCombo;
    int gm = HOST_GM_REWARD + combo * 2;
    Player_GainGM(gm, h->pos);
    Player_GainEP(((float)HOST_EP_REWARD + 0.5f * (float)combo));
    G.region.alert = fminf(ALERT_MAX, G.region.alert + ALERT_INFECT * G.stats.injectAlertMul * G.stats.alertMul);
    G.region.alertDelay = ALERT_DECAY_DELAY;
    for (int i = 0; i < G.stats.burstReplicate && G.player.stage >= 1; i++) Minions_Spawn(Vector3Add(h->pos, Vector3Scale(h->anchor, h->radius)), Agent_Ref((int)(h - G.agents)), true);

    Vector3 c = { 0.4f, 2.6f, 1.3f };
    Particles_Burst(h->pos, 70, 14.0f, V4(c.x, c.y, c.z, 1.0f), V4(0.1f, 0.6f, 0.3f, 0.0f), 0.3f, 0.9f, PF_ADD);
    Particles_Ring(h->pos, h->anchor, 48, 16.0f, V4(0.8f, 3.0f, 1.6f, 1.0f), V4(0.1f, 0.5f, 0.3f, 0.0f), 0.35f, 0.6f);
    Lights_AddTemp(h->pos, (Vector3){ 1.0f, 4.0f, 2.0f }, 22.0f, 0.7f);
    Cam_AddTrauma(0.2f);
    Sfx_Play3D(SFX_INFECT, h->pos, 1.0f);
    char t[32];
    snprintf(t, sizeof(t), combo > 0 ? "INFECTED x%d" : "INFECTED", combo);
    Combat_Popup(Vector3Add(h->pos, Vector3Scale(h->anchor, h->radius + 1.0f)), t, (Color){ 120, 255, 170, 255 }, 28.0f);
    p->attach = REF_NONE;
    p->vel = Vector3Add(p->vel, Vector3Scale(h->anchor, 6.0f));
    HUD_Achievement(ACH_FIRST_INFECTION);
    HUD_Hint(TUT_MUTATE, "Infection grants Genetic Material (GM). Press TAB to open the mutation tree and evolve your genome.");
    if (G.region.hostTotal > 0 && G.region.hostInfected >= G.region.hostTotal && !G.region.fullInfectionAwarded) {
        G.region.fullInfectionAwarded = true;
        HUD_Achievement(ACH_SATURATION);
    }
}

void Infection_Cleanse(Agent *h)
{
    if (!(h->flags & AF_INFECTED)) return;
    h->flags &= (uint16_t)~AF_INFECTED;
    h->progress = 0.0f;
    if (G.region.hostInfected > 0) G.region.hostInfected--;
    Particles_Burst(h->pos, 40, 9.0f, V4(2.0f, 2.0f, 2.2f, 1.0f), V4(0.5f, 0.5f, 0.6f, 0.0f), 0.25f, 0.7f, PF_ADD);
    Sfx_Play3D(SFX_CLEANSE, h->pos, 1.0f);
    HUD_FloatText("An NK cell cleansed one of your cells!", (Color){ 255, 120, 120, 255 });
}

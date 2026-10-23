/*
 * player.c - the pathogen.
 *
 * Swimming at cellular scale is all viscosity (very low Reynolds number):
 * there is no coasting, velocity relaxes quickly toward what the flagella
 * push for, and the surrounding current drags small bodies along. Bigger
 * stages resist the current more. A dash is a violent flagellar kick that
 * briefly overpowers viscosity.
 */
#include "entities/player.h"
#include "entities/minions.h"
#include "entities/projectiles.h"
#include "entities/bosses.h"
#include "systems/combat.h"
#include "systems/infection.h"
#include "systems/immune_memory.h"
#include "systems/mutation.h"
#include "systems/evolution.h"
#include "systems/spatial_hash.h"
#include "world/sdf_cave.h"
#include "world/flow_field.h"
#include "world/hazards.h"
#include "world/region.h"
#include "gfx/camera.h"
#include "gfx/particles.h"
#include "gfx/lights.h"
#include "gfx/render.h"
#include "audio/sfx.h"
#include "audio/music.h"
#include "ui/hud.h"
#include "core/math_utils.h"
#include "core/rng.h"
#include "core/pool.h"
#include <stdio.h>
#include <string.h>

static int sDashHits[24];
static int sDashHitCount = 0;
static float sDenyT = 0.0f;

void Player_RecomputeStats(void)
{
    Player *p = &G.player;
    Mutation_ComputeStats(&G.stats, p->stage);
    p->radius = STAGES[p->stage].radius * G.stats.sizeMul;
    if (p->biomass > G.stats.maxBiomass) p->biomass = G.stats.maxBiomass;
    if (p->atp > G.stats.maxAtp) p->atp = G.stats.maxAtp;
}

void Player_Reset(void)
{
    Player *p = &G.player;
    int stage = p->stage, gm = p->gm;
    float ep = p->ep;
    int best = p->bestCombo;
    memset(p, 0, sizeof(*p));
    p->stage = stage; p->gm = gm; p->ep = ep; p->bestCombo = best;
    Player_RecomputeStats();
    p->pos = p->prev = G.region.startPos;
    p->biomass = G.stats.maxBiomass;
    p->atp = G.stats.maxAtp;
    p->attach = REF_NONE;
    p->engulfedBy = REF_NONE;
    p->aimAgent = REF_NONE;
    Vector3 t;
    Region_PathPoint(12.0f, &t, NULL);
    p->forward = t;
    G.rig.yaw = atan2f(t.x, t.z);
    G.rig.pitch = -0.12f;
    p->invuln = 2.0f;
}

float Player_DesiredCamDist(void)
{
    Player *p = &G.player;
    float base = STAGES[p->stage].camDist * (0.75f + 0.25f * G.stats.sizeMul);
    float sp = Vector3Length(p->vel);
    return base + Sat(sp / 30.0f) * 1.5f + (p->attach.idx >= 0 ? -1.0f : 0.0f);
}

void Player_GainEP(float ep)
{
    G.player.ep += ep * G.stats.epMul;
}

void Player_GainGM(int gm, Vector3 at)
{
    int n = (int)((float)gm * G.stats.gmMul + 0.5f);
    if (n <= 0) return;
    G.player.gm += n;
    G.run.stats.gmEarned += n;
    char t[16];
    snprintf(t, sizeof(t), "+%d GM", n);
    Combat_Popup(Vector3Add(at, (Vector3){ 0, 1.0f, 0 }), t, (Color){ 120, 230, 255, 255 }, 14.0f + fminf(10.0f, (float)n * 0.3f));
}

void Player_Heal(float amount)
{
    G.player.biomass = fminf(G.stats.maxBiomass, G.player.biomass + amount);
}

static void Deny(const char *why)
{
    if (sDenyT > 0.0f) return;
    sDenyT = 0.8f;
    HUD_FloatText(why, (Color){ 255, 200, 120, 255 });
    Sfx_Play(SFX_UI_DENY, 0.6f, 1.0f);
}

static void Uncloak(void)
{
    Player *p = &G.player;
    if (!p->cloaked) return;
    p->cloaked = false;
    p->cloakCd = CLOAK_COOLDOWN;
    Sfx_Play(SFX_DECLOAK, 0.7f, 1.0f);
}

void Player_Die(void)
{
    Player *p = &G.player;
    if (p->dead) return;
    p->dead = true;
    p->deathT = 0.0f;
    p->biomass = 0.0f;
    p->attach = REF_NONE;
    p->engulfedBy = REF_NONE;
    p->cloaked = false;
    G.run.stats.deaths++;
    Particles_Burst(p->pos, 120, 18.0f, V4(0.6f, 2.4f, 1.2f, 1.0f), V4(0.1f, 0.3f, 0.2f, 0.0f), 0.4f, 1.6f, PF_ADD);
    Particles_Burst(p->pos, 60, 8.0f, V4(0.4f, 0.8f, 0.5f, 0.8f), V4(0.1f, 0.2f, 0.1f, 0.0f), 0.8f, 2.4f, PF_FLOW);
    Lights_AddTemp(p->pos, (Vector3){ 2.0f, 5.0f, 3.0f }, 30.0f, 1.5f);
    Combat_SlowMo(1.6f);
    Cam_AddTrauma(0.8f);
    Sfx_Play(SFX_DEATH, 1.0f, 1.0f);
    Music_Stinger(4);
}

/* ------------------------------------------------------------------------- */
/* Aiming: ray from the camera through the crosshair                          */
/* ------------------------------------------------------------------------- */

void Player_UpdateAim(void)
{
    Player *p = &G.player;
    Camera3D c = G.rig.cam;
    Vector3 ro = c.position;
    Vector3 rd = Vector3Normalize(Vector3Subtract(c.target, c.position));
    float maxD = 110.0f;
    float wall;
    if (Cave_Raycast(ro, rd, maxD, 0.2f, &wall)) maxD = wall;
    float minT = Vector3Distance(ro, p->pos) + p->radius * 0.5f;
    float best = maxD;
    int bestIdx = -1;
    bool boss = false;
    uint32_t mask = AGENT_MASK_IMMUNE | KMASK(AK_BACTERIA);
    int ids[48];
    for (float t = minT; t < maxD + 8.0f; t += 12.0f) {
        Vector3 q = Vector3Add(ro, Vector3Scale(rd, t));
        int n = SH_Query(q, 9.0f, ids, 48, mask);
        for (int k = 0; k < n; k++) {
            Agent *a = &G.agents[ids[k]];
            if (a->flags & AF_ALLY) continue;
            float hit;
            float assist = 0.6f + 0.012f * Vector3Distance(ro, a->pos);
            if (MU_RaySphere(ro, rd, a->pos, a->radius + assist, &hit) && hit > minT && hit < best) {
                best = hit;
                bestIdx = ids[k];
            }
        }
    }
    if (G.boss.active && !G.boss.dying) {
        float hit;
        if (MU_RaySphere(ro, rd, G.boss.pos, G.boss.radius, &hit) && hit > minT && hit < best) { best = hit; bestIdx = -1; boss = true; }
        for (int s = 0; s < G.boss.segCount; s++) {
            if (MU_RaySphere(ro, rd, G.boss.seg[s], G.boss.radius * 0.8f, &hit) && hit > minT && hit < best) { best = hit; bestIdx = -1; boss = true; }
        }
    }
    p->aimPoint = Vector3Add(ro, Vector3Scale(rd, best));
    p->aimAgent = Agent_Ref(bestIdx);
    p->aimBoss = boss;
}

static void FireToxin(void)
{
    Player *p = &G.player;
    PlayerStats *s = &G.stats;
    Vector3 muzzle = Vector3Add(p->pos, Vector3Scale(p->forward, p->radius * 0.9f));
    Vector3 dir = Vector3Normalize(Vector3Subtract(p->aimPoint, muzzle));
    if (Vector3DotProduct(dir, Cam_Forward()) < 0.2f) dir = Cam_Forward();
    float dmg = s->toxinDamage;
    if (p->cloaked && p->ambushReady) { dmg *= 2.0f; p->ambushReady = false; }
    Vector3 right = Vector3Normalize(Vector3CrossProduct(dir, Cam_Up()));
    Vector3 up = Vector3CrossProduct(right, dir);
    int n = s->toxinCount;
    for (int i = 0; i < n; i++) {
        float off = (n > 1) ? ((float)i / (float)(n - 1) - 0.5f) * 2.0f : 0.0f;
        Vector3 d = Vector3Add(dir, Vector3Scale(right, off * s->toxinSpread));
        d = Vector3Add(d, Vector3Scale(Vector3Add(Vector3Scale(right, Rng_RangeG(-1, 1)), Vector3Scale(up, Rng_RangeG(-1, 1))), 0.012f));
        d = Vector3Normalize(d);
        int pi = Proj_Spawn(s->shieldShred ? PT_ACID : PT_TOXIN, muzzle, Vector3Add(Vector3Scale(d, s->toxinSpeed), Vector3Scale(p->vel, 0.4f)), dmg, true);
        if (pi >= 0 && p->aimAgent.idx >= 0 && n == 1) {
            G.proj[pi].target = p->aimAgent;
            G.proj[pi].homing = 2.2f;
        }
    }
    p->toxinCd = s->toxinInterval;
    p->atp -= s->toxinCost;
    p->squash = fmaxf(p->squash - 0.06f, -0.15f);
    p->lastAttackT = G.simTime;
    Particles_Burst(muzzle, 5, 4.0f, V4(0.5f, 2.4f, 0.8f, 1.0f), V4(0.1f, 0.5f, 0.2f, 0.0f), 0.15f, 0.2f, PF_ADD);
    Lights_AddTemp(muzzle, (Vector3){ 0.4f, 1.6f, 0.6f }, 6.0f, 0.08f);
    Sfx_PlayThrottled(SFX_SPIT, 0.55f, 0.06f);
    Cam_Kick(0.4f);
    if (p->cloaked) Uncloak();
}

static void TryDash(Vector3 wish)
{
    Player *p = &G.player;
    PlayerStats *s = &G.stats;
    if (p->dashCd > 0.0f || p->attach.idx >= 0) return;
    if (p->atp < s->dashCost) { Deny("Not enough ATP"); return; }
    Vector3 dir = Vector3Length(wish) > 0.1f ? Vector3Normalize(wish) : Cam_Forward();
    p->vel = Vector3Add(Vector3Scale(dir, s->dashPower), Vector3Scale(p->vel, 0.25f));
    p->dashT = PLAYER_DASH_TIME;
    p->invuln = fmaxf(p->invuln, PLAYER_DASH_IFRAMES);
    p->dashCd = s->dashCd;
    p->atp -= s->dashCost;
    p->squash = 0.35f;
    sDashHitCount = 0;
    Immune_Use(IM_DASH, 1.0f);
    for (int i = 0; i < 24; i++) {
        Vector3 v = Vector3Add(Vector3Scale(dir, -Rng_RangeG(6.0f, 16.0f)), Vector3Scale(Rng_UnitVecG(), 3.0f));
        Particles_EmitStretch(Vector3Add(p->pos, Vector3Scale(Rng_UnitVecG(), p->radius * 0.6f)), v, V4(0.6f, 2.0f, 1.4f, 0.9f), V4(0.1f, 0.4f, 0.3f, 0.0f), 0.12f, 0.4f, 0.06f, PF_ADD);
    }
    Particles_Ring(Vector3Subtract(p->pos, Vector3Scale(dir, p->radius)), dir, 24, 7.0f, V4(0.6f, 1.8f, 1.6f, 0.8f), V4(0.1f, 0.3f, 0.3f, 0.0f), 0.2f, 0.35f);
    Cam_Kick(7.0f);
    Cam_AddTrauma(0.08f);
    Sfx_Play(SFX_DASH, 0.8f, 1.0f + Rng_RangeG(-0.05f, 0.05f));
    if (s->decoys) Minions_SpawnDecoy(p->pos, Vector3Scale(dir, -6.0f));
}

static void UpdateEngulfed(float dt, Agent *m)
{
    Player *p = &G.player;
    p->engulfT += dt;
    Vector3 wob = { sinf(p->animT * 3.1f), cosf(p->animT * 2.3f), sinf(p->animT * 2.7f + 1.0f) };
    Vector3 target = Vector3Add(m->pos, Vector3Scale(wob, m->radius * 0.25f));
    p->pos = V3Damp(p->pos, target, 8.0f, dt);
    p->vel = m->vel;
    if (G.in.dash) {
        p->struggle++;
        p->squash = 0.35f;
        m->flash = 1.0f;
        m->vel = Vector3Add(m->vel, Vector3Scale(Rng_UnitVecG(), 4.0f));
        Cam_AddTrauma(0.12f);
        Particles_Burst(p->pos, 10, 6.0f, V4(0.6f, 2.0f, 1.2f, 1.0f), V4(0.2f, 0.4f, 0.3f, 0.0f), 0.2f, 0.3f, PF_ADD);
        Sfx_Play(SFX_HIT, 0.6f, 0.7f + 0.05f * (float)p->struggle);
    }
    if (p->struggle >= 6) {
        Vector3 dir = Cam_Forward();
        p->engulfedBy = REF_NONE;
        p->vel = Vector3Scale(dir, 26.0f);
        p->pos = Vector3Add(m->pos, Vector3Scale(dir, m->radius + p->radius + 0.3f));
        p->invuln = 0.9f;
        Combat_DamageAgent(m, 20.0f + G.stats.dashDamage, DMG_DASH, dir);
        Particles_Burst(p->pos, 40, 12.0f, V4(1.4f, 1.2f, 0.7f, 1.0f), V4(0.3f, 0.2f, 0.1f, 0.0f), 0.3f, 0.6f, PF_ADD);
        Sfx_Play(SFX_ESCAPE, 1.0f, 1.0f);
        Cam_AddTrauma(0.3f);
        Combat_HitStop(0.06f);
    }
}

/* ------------------------------------------------------------------------- */
/* Update                                                                     */
/* ------------------------------------------------------------------------- */

void Player_Update(float dt)
{
    Player *p = &G.player;
    PlayerStats *s = &G.stats;
    InputState *in = &G.in;
    p->prev = p->pos;
    p->animT += dt;
    sDenyT -= dt;

    if (p->dead) {
        p->deathT += dt;
        p->vel = V3Damp(p->vel, Flow_Sample(p->pos), 1.0f, dt);
        p->pos = Vector3Add(p->pos, Vector3Scale(p->vel, dt));
        return;
    }

    p->dashT = fmaxf(0.0f, p->dashT - dt);
    p->dashCd = fmaxf(0.0f, p->dashCd - dt);
    p->invuln = fmaxf(0.0f, p->invuln - dt);
    p->toxinCd = fmaxf(0.0f, p->toxinCd - dt);
    p->burstCd = fmaxf(0.0f, p->burstCd - dt);
    p->swarmCd = fmaxf(0.0f, p->swarmCd - dt);
    p->cloakCd = fmaxf(0.0f, p->cloakCd - dt);
    p->regenDelay = fmaxf(0.0f, p->regenDelay - dt);
    p->hurtFlash = fmaxf(0.0f, p->hurtFlash - dt * 1.6f);
    p->rhythmFlash = FDamp(p->rhythmFlash, 0.0f, 5.0f, dt);
    p->squash = FDamp(p->squash, 0.0f, 7.0f, dt);

    Player_UpdateAim();

    /* engulfed by a macrophage: only struggling matters */
    Agent *eng = Agent_Get(p->engulfedBy);
    if (eng) { UpdateEngulfed(dt, eng); return; }
    p->engulfedBy = REF_NONE;

    /* cloak upkeep */
    if (p->cloaked) {
        p->cloakT -= dt;
        p->atp -= CLOAK_ATP_DRAIN * dt;
        Immune_Use(IM_STEALTH, 0.25f * dt);
        if (p->cloakT <= 0.0f || p->atp <= 0.0f) Uncloak();
    }

    /* --- swimming --- */
    Vector3 f = Cam_Forward(), r = Cam_Right();
    Vector3 wish = Vector3Add(Vector3Add(Vector3Scale(f, in->moveY), Vector3Scale(r, in->moveX)), (Vector3){ 0, in->moveZ, 0 });
    float wl = Vector3Length(wish);
    if (wl > 1.0f) { wish = Vector3Scale(wish, 1.0f / wl); wl = 1.0f; }
    float slow = (1.0f - ANTIBODY_SLOW * (float)p->antibodies) * (1.0f - Hazards_SlowAt(p->pos));
    float speed = s->speed * fmaxf(0.25f, slow);
    Vector3 flow = Flow_Sample(p->pos);
    float flowK = 0.65f / (1.0f + 0.45f * (float)p->stage);
    bool attached = p->attach.idx >= 0;

    if (!attached) {
        Vector3 target = Vector3Add(Vector3Scale(wish, speed), Vector3Scale(flow, flowK));
        float rate = (wl > 0.05f) ? s->accel / 14.0f : 1.6f;
        if (p->dashT > 0.0f) rate = 0.7f;
        p->vel = V3Damp(p->vel, target, rate, dt);
        if (in->dash) TryDash(wish);
    }

    /* dash strikes */
    if (p->dashT > 0.0f) {
        int ids[16];
        int n = SH_Query(p->pos, p->radius + 0.6f, ids, 16, AGENT_MASK_IMMUNE | KMASK(AK_BACTERIA));
        for (int k = 0; k < n; k++) {
            bool done = false;
            for (int q = 0; q < sDashHitCount; q++) if (sDashHits[q] == ids[k]) done = true;
            if (done || sDashHitCount >= 24) continue;
            Agent *a = &G.agents[ids[k]];
            if (a->flags & AF_ALLY) continue;
            sDashHits[sDashHitCount++] = ids[k];
            float dmg = s->dashDamage * ((p->cloaked && p->ambushReady) ? 2.0f : 1.0f);
            Combat_DamageAgent(a, dmg, DMG_DASH, p->vel);
            Combat_HitStop(0.035f);
            Cam_AddTrauma(0.1f);
        }
        Vector3 bh;
        bool bossDone = false;
        for (int q = 0; q < sDashHitCount; q++) if (sDashHits[q] == -5) bossDone = true;
        if (G.boss.active && !bossDone && sDashHitCount < 24 && Bosses_HitTest(p->pos, p->radius + 0.5f, &bh)) {
            sDashHits[sDashHitCount++] = -5;
            Combat_DamageBoss(s->dashDamage, DMG_DASH, bh);
        }
        if (n > 0 && p->cloaked) { p->ambushReady = false; Uncloak(); }
        if (GetRandomValue(0, 100) < 80) Particles_EmitStretch(p->pos, Vector3Scale(p->vel, -0.3f), V4(0.5f, 1.8f, 1.2f, 0.7f), V4(0.1f, 0.3f, 0.2f, 0.0f), p->radius * 0.5f, 0.3f, 0.04f, PF_ADD);
    }

    /* contact lysis (capsid spikes) */
    p->contactTick += dt;
    if (s->contactDps > 0.0f && p->contactTick >= 0.25f) {
        p->contactTick = 0.0f;
        int ids[16];
        int n = SH_Query(p->pos, p->radius + 0.7f, ids, 16, AGENT_MASK_IMMUNE);
        for (int k = 0; k < n; k++) {
            Agent *a = &G.agents[ids[k]];
            Combat_DamageAgent(a, s->contactDps * 0.25f, DMG_CONTACT, Vector3Subtract(a->pos, p->pos));
            if (a->alive && s->synNecrotic) { a->poisonDps = fmaxf(a->poisonDps, 8.0f); a->poisonT = fmaxf(a->poisonT, 2.0f); }
        }
        Vector3 bh;
        if (G.boss.active && Bosses_HitTest(p->pos, p->radius + 0.7f, &bh)) Combat_DamageBoss(s->contactDps * 0.25f, DMG_CONTACT, bh);
    }

    /* abilities */
    if (!attached && in->fire && p->toxinCd <= 0.0f) {
        if (p->atp >= s->toxinCost) FireToxin();
        else if (in->firePressed) Deny("Not enough ATP");
    }
    if (in->burst) {
        if (p->burstCd > 0.0f) Deny("Lysis burst recharging");
        else if (p->atp < s->burstCost) Deny("Not enough ATP");
        else {
            p->atp -= s->burstCost;
            p->burstCd = s->burstCd;
            Combat_PlayerBurst();
            if (p->cloaked) Uncloak();
            if (attached) p->attach = REF_NONE;
        }
    }
    if (in->swarm) {
        if (p->stage < 1) Deny("Evolve into a Replicator to command virions");
        else if (p->swarmCd > 0.0f) Deny("Hive call recharging");
        else { p->swarmCd = s->swarmCd; Minions_Command(); }
    }
    if (in->cloak) {
        if (!s->cloakUnlocked) Deny("Requires the Cloaking Membrane mutation");
        else if (p->cloaked) Uncloak();
        else if (p->cloakCd > 0.0f) Deny("Cloak recharging");
        else if (p->atp < 20.0f) Deny("Not enough ATP");
        else {
            p->cloaked = true;
            p->cloakT = s->cloakDuration;
            p->ambushReady = s->cloakAmbush > 0.0f;
            Immune_Use(IM_STEALTH, 2.5f);
            for (int i = 0; i < G.agentHigh; i++) {
                Agent *a = &G.agents[i];
                if (a->alive && a->team == TEAM_IMMUNE && a->targetPlayer) a->timer2 = 3.0f;
            }
            Particles_Burst(p->pos, 40, 6.0f, V4(1.2f, 1.0f, 2.4f, 0.8f), V4(0.2f, 0.2f, 0.6f, 0.0f), 0.3f, 0.6f, PF_ADD);
            Sfx_Play(SFX_CLOAK, 0.9f, 1.0f);
        }
    }

    /* infection */
    if (in->infectPressed && !attached) {
        if (!Infection_TryAttach()) {
            if (Infection_FindTarget().idx < 0) Deny("No host cell in reach - get close to a cell on the wall");
        } else {
            HUD_Hint(TUT_INFECT, "Injecting! Keep holding RMB. Tap LMB as the pulse ring closes on the membrane for bonus progress.");
        }
    }
    if (p->attach.idx >= 0) Infection_Update(dt);

    /* metabolism */
    if (p->regenDelay <= 0.0f) p->biomass = fminf(s->maxBiomass, p->biomass + s->regen * dt);
    float atpRegen = s->atpRegen * (p->cloaked ? 0.0f : 1.0f) * (in->fire ? 0.35f : 1.0f);
    p->atp = fminf(s->maxAtp, p->atp + atpRegen * dt);
    if (p->antibodies > 0) {
        p->antibodyDecay += dt;
        if (p->antibodyDecay > 5.0f) { p->antibodies--; p->antibodyDecay = 0.0f; }
    }

    /* integrate + collide */
    p->pos = Vector3Add(p->pos, Vector3Scale(p->vel, dt));
    if (Cave_Collide(&p->pos, &p->vel, p->radius, 0.25f) && p->dashT > 0.1f) {
        Cam_AddTrauma(0.15f);
        p->squash = -0.3f;
        p->dashT = 0.0f;
    }
    Hazards_Collide(&p->pos, &p->vel, p->radius);
    Bosses_Collide(&p->pos, &p->vel, p->radius);

    /* facing: toward the aim while attacking, otherwise along the swim */
    Vector3 face = p->forward;
    if (attached) {
        Agent *h = Agent_Get(p->attach);
        if (h) face = Vector3Normalize(Vector3Subtract(h->pos, p->pos));
    } else if (G.simTime - p->lastAttackT < 0.6f || in->fire) {
        face = Vector3Normalize(Vector3Subtract(p->aimPoint, p->pos));
    } else if (Vector3Length(p->vel) > 2.0f) {
        face = Vector3Normalize(p->vel);
    }
    p->forward = Vector3Normalize(Vector3Lerp(p->forward, face, 1.0f - expf(-8.0f * dt)));

    /* flagellar wake */
    float sp = Vector3Length(p->vel);
    if (sp > 4.0f && GetRandomValue(0, 100) < (int)(sp * 2.0f)) {
        Vector3 back = Vector3Subtract(p->pos, Vector3Scale(p->forward, p->radius * 1.3f));
        Particles_Emit(back, Vector3Add(Vector3Scale(p->vel, 0.2f), Vector3Scale(Rng_UnitVecG(), 0.6f)), V4(0.3f, 1.0f, 0.7f, 0.35f), V4(0.1f, 0.3f, 0.2f, 0.0f),
                       0.12f * p->radius, 0.5f * p->radius, 1.2f, 1.5f, PF_ADD | PF_FLOW);
    }

    if (p->biomass <= 0.0f) Player_Die();
}

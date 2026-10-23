/*
 * bosses.c - region guardians.
 *
 * Each boss has three phases (100-60%, 60-30%, 30-0%) and a per-phase
 * pattern pool. A scheduler picks the next pattern (never the same twice in
 * a row), every pattern telegraphs before it bites, and phase transitions
 * give a short invulnerable roar plus a shockwave so the fight has beats.
 *
 *   Sentinel Macrophage   CHARGE, VORTEX, SUMMON | +SHOCKWAVE
 *   Alveolar Warden       MUCUS, VOLLEY, SUMMON  | SPIRAL, GUST
 *   Peristaltic Leviathan CHARGE, ACID           | RING, SUMMON   (segmented worm)
 *   Germinal Citadel      SHIELD nodes, RING, VOLLEY | SPIRAL, SUMMON, re-SHIELD
 *   Microglial Overmind   TELEPORT, STORM, VOLLEY | SPIRAL, SUMMON, SHOCKWAVE | VORTEX
 */
#include "entities/bosses.h"
#include "entities/enemies.h"
#include "entities/projectiles.h"
#include "entities/pickups.h"
#include "entities/player.h"
#include "systems/combat.h"
#include "systems/immune_memory.h"
#include "world/region.h"
#include "world/hazards.h"
#include "world/sdf_cave.h"
#include "gfx/particles.h"
#include "gfx/lights.h"
#include "gfx/camera.h"
#include "audio/sfx.h"
#include "audio/music.h"
#include "ui/hud.h"
#include "ui/bestiary_ui.h"
#include "core/game_state.h"
#include "core/settings.h"
#include "core/pool.h"
#include "core/rng.h"
#include "core/math_utils.h"
#include <stdio.h>
#include <string.h>

const BossDef BOSSES[BOSS_COUNT] = {
    [BOSS_SENTINEL] = { "SENTINEL MACROPHAGE", "Guardian of the Vessel",
        "An ancient tissue-resident macrophage swollen to monstrous size on a lifetime of debris. It patrols the great vessel and swallows whatever it cannot recognise.",
        1500.0f, 7.5f, 7.0f, 18.0f, CX_BOSS_SENTINEL, { 1.0f, 0.85f, 0.65f }, { 2.4f, 1.4f, 0.6f }, 3 },
    [BOSS_WARDEN] = { "ALVEOLAR WARDEN", "Keeper of Breath",
        "A colossal alveolar macrophage fused with the surfactant lining of an air sac. It controls the mucus and the cough itself.",
        2000.0f, 8.0f, 6.0f, 16.0f, CX_BOSS_WARDEN, { 1.0f, 0.8f, 0.9f }, { 2.4f, 1.2f, 1.8f }, 3 },
    [BOSS_LEVIATHAN] = { "PERISTALTIC LEVIATHAN", "The Hungry Coil",
        "Gut-associated lymphoid tissue given hunger and a spine of smooth muscle. It rides the peristaltic wave and spits digestive acid.",
        2700.0f, 4.6f, 14.0f, 22.0f, CX_BOSS_LEVIATHAN, { 0.95f, 0.75f, 0.45f }, { 1.6f, 2.4f, 0.4f }, 3 },
    [BOSS_CITADEL] = { "GERMINAL CITADEL", "Heart of the Node",
        "A germinal centre that never stops dividing: follicular dendritic cells armoured in antibody, guarded by orbiting shield nodes.",
        3300.0f, 9.0f, 0.0f, 20.0f, CX_BOSS_CITADEL, { 0.85f, 1.0f, 0.7f }, { 1.2f, 2.6f, 1.0f }, 3 },
    [BOSS_OVERMIND] = { "MICROGLIAL OVERMIND", "Warden of Thought",
        "The brain's resident immune mind. It walks between synapses, calls lightning from the neurons and remembers everything you have ever done.",
        4300.0f, 8.5f, 9.0f, 24.0f, CX_BOSS_OVERMIND, { 0.85f, 0.75f, 1.0f }, { 1.6f, 1.0f, 3.0f }, 3 },
};

static const BossPattern SCHED[BOSS_COUNT][3][5] = {
    [BOSS_SENTINEL]  = { { BP_CHARGE, BP_VORTEX, BP_SUMMON, BP_NONE }, { BP_CHARGE, BP_SHOCKWAVE, BP_VORTEX, BP_SUMMON, BP_NONE }, { BP_CHARGE, BP_SHOCKWAVE, BP_VORTEX, BP_CHARGE, BP_NONE } },
    [BOSS_WARDEN]    = { { BP_MUCUS, BP_VOLLEY, BP_SUMMON, BP_NONE }, { BP_SPIRAL, BP_GUST, BP_MUCUS, BP_VOLLEY, BP_NONE }, { BP_SPIRAL, BP_GUST, BP_VOLLEY, BP_SUMMON, BP_NONE } },
    [BOSS_LEVIATHAN] = { { BP_CHARGE, BP_ACID, BP_NONE }, { BP_RING, BP_SUMMON, BP_CHARGE, BP_ACID, BP_NONE }, { BP_RING, BP_CHARGE, BP_ACID, BP_SUMMON, BP_NONE } },
    [BOSS_CITADEL]   = { { BP_RING, BP_VOLLEY, BP_NONE }, { BP_SPIRAL, BP_SUMMON, BP_RING, BP_NONE }, { BP_SPIRAL, BP_RING, BP_SUMMON, BP_VOLLEY, BP_NONE } },
    [BOSS_OVERMIND]  = { { BP_TELEPORT, BP_STORM, BP_VOLLEY, BP_NONE }, { BP_SPIRAL, BP_SUMMON, BP_SHOCKWAVE, BP_TELEPORT, BP_NONE }, { BP_VORTEX, BP_SPIRAL, BP_STORM, BP_TELEPORT, BP_NONE } },
};

static Vector3 sCenter;
static float sArena = 30.0f;
static float sContactCd = 0.0f;
static int sLastPattern = BP_NONE;
static float sShieldMsgT = 0.0f;
static int sChargeCount = 0;
static bool sEngaged = true;

const char *Bosses_PatternName(void)
{
    switch (G.boss.pattern) {
        case BP_CHARGE: return G.boss.patternStep == 0 ? "CHARGING..." : "CHARGE";
        case BP_VORTEX: return "PHAGOCYTIC VORTEX";
        case BP_SUMMON: return "SUMMONING";
        case BP_SHOCKWAVE: return "SHOCKWAVE";
        case BP_MUCUS: return "MUCUS BARRAGE";
        case BP_VOLLEY: return "VOLLEY";
        case BP_SPIRAL: return "SPIRAL";
        case BP_GUST: return G.boss.patternStep == 0 ? "INHALING..." : "COUGH";
        case BP_ACID: return "ACID SPIT";
        case BP_RING: return "RING BURST";
        case BP_SHIELD: return "SHIELD NODES";
        case BP_TELEPORT: return "PHASING";
        case BP_STORM: return "SYNAPTIC STORM";
        default: return "";
    }
}

void Bosses_Clear(void)
{
    memset(&G.boss, 0, sizeof(G.boss));
    sContactCd = 0.0f;
    sLastPattern = BP_NONE;
}

static float DmgMul(void) { return Region_EnemyDmgMul(); }

static Vector3 ToPlayer(Vector3 from)
{
    Vector3 d = Vector3Subtract(G.player.pos, from);
    float l = Vector3Length(d);
    return l > 0.01f ? Vector3Scale(d, 1.0f / l) : (Vector3){ 0, 0, 1 };
}

static void Shoot(ProjType t, Vector3 from, Vector3 dir, float speed, float dmg)
{
    Proj_Spawn(t, from, Vector3Scale(Vector3Normalize(dir), speed), dmg * DmgMul(), false);
}

static void SpawnShieldNodes(int n)
{
    Boss *b = &G.boss;
    for (int i = 0; i < n; i++) {
        int k = Agent_Alloc();
        if (k < 0) break;
        Agent *a = &G.agents[k];
        a->kind = AK_BOSS_PART;
        a->team = TEAM_IMMUNE;
        a->radius = 1.7f;
        a->hp = a->maxHp = (90.0f + 30.0f * (float)b->phase) * DIFFICULTIES[G.run.difficulty].enemyHp * (1.0f + 0.4f * (float)G.run.ngPlus);
        a->phase = (float)i / (float)n;
        a->pos = a->prev = b->pos;
        a->cooldown = 2.0f + (float)i * 0.6f;
        a->target = REF_NONE;
        a->owner = REF_NONE;
        G.agentCount[AK_BOSS_PART]++;
    }
    b->shieldNodes = n;
    b->shielded = true;
    HUD_Banner("SHIELD NODES ONLINE", "Destroy the orbiting nodes to expose the core", (Color){ 140, 220, 255, 255 });
    Sfx_Play(SFX_WARNING, 0.7f, 1.2f);
}

static void SummonAdds(void)
{
    Boss *b = &G.boss;
    int alive = 0;
    for (int i = 0; i < G.agentHigh; i++) if (G.agents[i].alive && G.agents[i].team == TEAM_IMMUNE && G.agents[i].group == 3 && G.agents[i].kind != AK_ANTIBODY) alive++;
    if (alive >= 12) return;
    AgentKind k = AK_NEUTROPHIL;
    int n = 4;
    switch (b->type) {
        case BOSS_WARDEN: k = AK_BCELL; n = 2; break;
        case BOSS_LEVIATHAN: k = AK_NK; n = 3; break;
        case BOSS_CITADEL: k = AK_TCELL; n = 3; break;
        case BOSS_OVERMIND: k = AK_NK; n = 3; break;
        default: break;
    }
    for (int i = 0; i < n; i++) {
        Vector3 p = Vector3Add(b->pos, Vector3Scale(Rng_UnitVecG(), b->radius + 3.0f));
        Vector3 q;
        if (!Cave_RandomOpenPoint(&G.rng, 2.0f, p, 4.0f, &q)) q = p;
        int e = Enemies_Spawn(k, q, false);
        if (e >= 0) {
            Agent *a = &G.agents[e];
            a->group = 3;
            a->state = AI_CHASE;
            a->targetPlayer = true;
            a->flags |= AF_ALERTED;
            a->vel = Vector3Scale(Vector3Normalize(Vector3Subtract(q, b->pos)), 12.0f);
            Particles_Burst(q, 16, 5.0f, V4(2.0f, 1.6f, 1.2f, 1.0f), V4(0.4f, 0.3f, 0.2f, 0.0f), 0.3f, 0.5f, PF_ADD);
        }
    }
    Sfx_Play3D(SFX_BOSS_ROAR, b->pos, 0.6f);
}

static void Ring(Vector3 from, Vector3 axis, int n, float speed, float dmg, float phase)
{
    Vector3 a = Vector3Normalize(axis);
    Vector3 u = MU_AnyPerp(a);
    Vector3 v = Vector3CrossProduct(a, u);
    for (int i = 0; i < n; i++) {
        float t = phase + (float)i / (float)n * 2.0f * PI;
        Vector3 d = Vector3Add(Vector3Scale(u, cosf(t)), Vector3Scale(v, sinf(t)));
        Shoot(PT_BOSS_ORB, Vector3Add(from, Vector3Scale(d, G.boss.radius * 0.9f)), d, speed, dmg);
    }
}

/* ------------------------------------------------------------------------- */
/* Spawn                                                                      */
/* ------------------------------------------------------------------------- */

void Bosses_Spawn(BossType type, Vector3 pos)
{
    Boss *b = &G.boss;
    const BossDef *d = &BOSSES[type];
    memset(b, 0, sizeof(*b));
    b->active = true;
    b->type = (uint8_t)type;
    b->pos = b->prev = pos;
    b->facing = ToPlayer(pos);
    b->radius = d->radius;
    float hpMul = DIFFICULTIES[G.run.difficulty].enemyHp * (1.0f + 0.5f * (float)G.run.ngPlus);
    if (G.run.mode == MODE_OUTBREAK) hpMul *= 0.7f + 0.08f * (float)G.run.wave;
    b->hp = b->maxHp = d->hp * hpMul;
    b->attackT = 2.0f;
    b->pattern = BP_NONE;
    sCenter = pos;
    sArena = (G.run.mode == MODE_CAMPAIGN) ? G.region.bossRadius * 0.55f : 26.0f;
    sContactCd = 0.0f;
    sLastPattern = BP_NONE;
    if (type == BOSS_LEVIATHAN) {
        b->segCount = 12;
        for (int i = 0; i < b->segCount; i++) b->seg[i] = Vector3Subtract(pos, Vector3Scale(b->facing, (float)(i + 1) * b->radius * 1.05f));
    }
    if (type == BOSS_CITADEL) SpawnShieldNodes(5);
    if (G.run.mode == MODE_CAMPAIGN) G.region.bossSpawned = true;
    Codex_Unlock(d->codex);
    G.ui.bannerT = 0.0f;
    HUD_Hint(TUT_BOSS, "Guardian fight! Watch for glowing telegraphs before each attack, dash (Shift) through danger for invulnerability frames, and send your swarm with E.");
    Music_Stinger(2);
    Sfx_Play(SFX_BOSS_ROAR, 1.0f, 1.0f);
    Cam_StartCinematic(pos, b->radius * 3.4f + 10.0f, b->radius * 0.7f, 4.0f);
    Game_SetState(GS_BOSS_INTRO);
}

/* ------------------------------------------------------------------------- */
/* Hit testing                                                                */
/* ------------------------------------------------------------------------- */

static float SegRadius(int i) { return G.boss.radius * (0.92f - 0.035f * (float)i); }

bool Bosses_HitTest(Vector3 p, float r, Vector3 *hitPos)
{
    Boss *b = &G.boss;
    if (!b->active || b->dying) return false;
    if (Vector3Distance(p, b->pos) < b->radius + r) { if (hitPos) *hitPos = p; return true; }
    for (int i = 0; i < b->segCount; i++) {
        if (Vector3Distance(p, b->seg[i]) < SegRadius(i) + r) { if (hitPos) *hitPos = p; return true; }
    }
    return false;
}

static bool PushOut(Vector3 *pos, Vector3 *vel, float radius, Vector3 c, float cr)
{
    Vector3 d = Vector3Subtract(*pos, c);
    float l = Vector3Length(d);
    float rr = cr + radius;
    if (l >= rr || l < 1e-4f) return false;
    Vector3 n = Vector3Scale(d, 1.0f / l);
    *pos = Vector3Add(c, Vector3Scale(n, rr));
    if (vel) {
        float vn = Vector3DotProduct(*vel, n);
        if (vn < 0.0f) *vel = Vector3Subtract(*vel, Vector3Scale(n, vn * 1.4f));
    }
    return true;
}

bool Bosses_Collide(Vector3 *pos, Vector3 *vel, float radius)
{
    Boss *b = &G.boss;
    if (!b->active || b->dying) return false;
    bool hit = PushOut(pos, vel, radius, b->pos, b->radius * 0.92f);
    for (int i = 0; i < b->segCount; i++) hit |= PushOut(pos, vel, radius, b->seg[i], SegRadius(i) * 0.9f);
    return hit;
}

/* ------------------------------------------------------------------------- */
/* Damage                                                                     */
/* ------------------------------------------------------------------------- */

static void EnterPhase(int phase)
{
    Boss *b = &G.boss;
    b->phase = phase;
    b->invuln = 1.6f;
    b->flash = 1.0f;
    b->pattern = BP_NONE;
    b->attackT = 1.8f;
    int h = Hazards_Spawn(HZ_SHOCKWAVE, b->pos, 40.0f, 1.6f, 14.0f);
    (void)h;
    Cam_AddTrauma(0.5f);
    Sfx_Play(SFX_BOSS_ROAR, 1.0f, 0.85f);
    Particles_Burst(b->pos, 120, 20.0f, V4(BOSSES[b->type].glow.x, BOSSES[b->type].glow.y, BOSSES[b->type].glow.z, 1.0f), V4(0.3f, 0.2f, 0.2f, 0.0f), 0.5f, 1.2f, PF_ADD);
    char t[64];
    snprintf(t, sizeof(t), "%s ENRAGES", BOSSES[b->type].name);
    HUD_Banner(t, phase == 1 ? "Phase II - new attacks unlocked" : "Final phase - it holds nothing back", (Color){ 255, 90, 70, 255 });
    if (b->type == BOSS_CITADEL && phase >= 1) SpawnShieldNodes(4 + phase);
}

void Bosses_TakeDamage(float dmg, int src, Vector3 at)
{
    Boss *b = &G.boss;
    if (!b->active || b->dying || !b->introDone || dmg <= 0.0f) return;
    if (b->invuln > 0.0f) {
        if (GetRandomValue(0, 100) < 30) Particles_Burst(at, 4, 4.0f, V4(2.0f, 2.0f, 2.0f, 1.0f), V4(0.4f, 0.4f, 0.4f, 0.0f), 0.12f, 0.2f, PF_ADD);
        return;
    }
    if (b->shielded) {
        dmg *= 0.08f;
        if (sShieldMsgT <= 0.0f) {
            sShieldMsgT = 2.0f;
            Combat_Popup(at, "SHIELDED", (Color){ 140, 210, 255, 255 }, 22.0f);
        }
        Particles_Burst(at, 5, 5.0f, V4(0.6f, 1.6f, 2.6f, 1.0f), V4(0.1f, 0.3f, 0.6f, 0.0f), 0.15f, 0.25f, PF_ADD);
        Sfx_PlayThrottled(SFX_HIT_SHIELD, 0.5f, 0.06f);
    } else {
        Sfx_PlayThrottled(SFX_HIT, 0.5f, 0.05f);
    }
    b->hp -= dmg;
    b->flash = fmaxf(b->flash, 0.6f);
    G.run.stats.damageDealt += dmg;
    switch (src) {
        case DMG_TOXIN: Immune_Use(IM_TOXIN, dmg * 0.03f); break;
        case DMG_MINION: Immune_Use(IM_SWARM, dmg * 0.04f); break;
        case DMG_DASH: case DMG_CONTACT: Immune_Use(IM_DASH, dmg * 0.02f); break;
        default: break;
    }
    if (dmg >= 1.0f && src != DMG_POISON && src != DMG_CONTACT) {
        char t[16];
        snprintf(t, sizeof(t), "%d", (int)(dmg + 0.5f));
        Combat_Popup(at, t, b->shielded ? (Color){ 120, 190, 255, 255 } : (Color){ 255, 220, 150, 255 }, 22.0f);
    }
    float frac = b->hp / b->maxHp;
    if (b->phase == 0 && frac < 0.6f) EnterPhase(1);
    else if (b->phase == 1 && frac < 0.3f) EnterPhase(2);
    if (b->hp <= 0.0f) {
        b->hp = 0.0f;
        b->dying = true;
        b->deathT = 0.0f;
        b->pattern = BP_NONE;
        Combat_SlowMo(2.2f);
        Combat_HitStop(0.12f);
        Cam_AddTrauma(0.7f);
        Sfx_Play(SFX_BOSS_DEATH, 1.0f, 1.0f);
        for (int i = 0; i < G.agentHigh; i++) if (G.agents[i].alive && G.agents[i].kind == AK_BOSS_PART) Agent_Free(i);
        b->shielded = false;
    }
}

void Bosses_PartDestroyed(Agent *a)
{
    Boss *b = &G.boss;
    (void)a;
    if (!b->active) return;
    if (b->shieldNodes > 0) b->shieldNodes--;
    Particles_Burst(a->pos, 40, 10.0f, V4(0.6f, 1.8f, 2.8f, 1.0f), V4(0.1f, 0.3f, 0.6f, 0.0f), 0.3f, 0.7f, PF_ADD);
    Sfx_Play3D(SFX_EXPLODE, a->pos, 0.8f);
    if (b->shieldNodes == 0 && b->shielded) {
        b->shielded = false;
        b->invuln = 0.0f;
        b->pattern = BP_NONE;
        b->attackT = 3.0f;        /* exposed and dazed */
        HUD_Banner("SHIELD DOWN", "The core is exposed!", (Color){ 140, 255, 200, 255 });
        Cam_AddTrauma(0.3f);
    }
}

void Bosses_UpdatePart(Agent *a, float dt)
{
    Boss *b = &G.boss;
    if (!b->active || b->dying) { Agent_Free((int)(a - G.agents)); return; }
    float R = b->radius * 2.1f;
    float ang = a->phase * 6.2831f + b->anim * (0.55f + 0.15f * (float)b->phase);
    float tilt = sinf(ang * 2.0f + a->phase * 3.0f) * R * 0.3f;
    a->pos = Vector3Add(b->pos, (Vector3){ cosf(ang) * R, tilt, sinf(ang) * R });
    a->vel = (Vector3){ 0 };
    a->spin += dt * 2.0f;
    a->cooldown -= dt;
    if (a->cooldown <= 0.0f && b->introDone && !G.player.dead) {
        a->cooldown = 3.5f - 0.6f * (float)b->phase + Rng_RangeG(0.0f, 1.0f);
        Shoot(PT_BOSS_ORB, a->pos, ToPlayer(a->pos), 18.0f, 9.0f);
    }
}

/* ------------------------------------------------------------------------- */
/* Patterns                                                                   */
/* ------------------------------------------------------------------------- */

static void ChoosePattern(void)
{
    Boss *b = &G.boss;
    const BossPattern *pool = SCHED[b->type][b->phase];
    int n = 0;
    while (n < 5 && pool[n] != BP_NONE) n++;
    if (n == 0) return;
    int pick = Rng_IntG(0, n - 1);
    if (n > 1 && (int)pool[pick] == sLastPattern) pick = (pick + 1) % n;
    b->pattern = pool[pick];
    if (b->pattern == BP_SHIELD && b->shielded) b->pattern = BP_RING;
    sLastPattern = b->pattern;
    b->patternT = 0.0f;
    b->patternStep = 0;
    b->timer = 0.0f;
    sChargeCount = 0;
}

static void EndPattern(void)
{
    Boss *b = &G.boss;
    b->pattern = BP_NONE;
    float gap = (2.4f - 0.55f * (float)b->phase) / sqrtf(DIFFICULTIES[G.run.difficulty].enemyDamage);
    b->attackT = fmaxf(0.7f, gap);
}

static void RunPattern(float dt)
{
    Boss *b = &G.boss;
    Player *p = &G.player;
    const BossDef *d = &BOSSES[b->type];
    b->patternT += dt;
    b->timer -= dt;
    float rate = 1.0f + 0.25f * (float)b->phase;
    switch ((BossPattern)b->pattern) {
        case BP_CHARGE:
            if (b->patternStep == 0) {
                Vector3 lead = Vector3Add(p->pos, Vector3Scale(p->vel, 0.35f));
                b->chargeDir = Vector3Normalize(Vector3Subtract(lead, b->pos));
                b->facing = b->chargeDir;
                b->vel = Vector3Scale(b->vel, expf(-4.0f * dt));
                if (GetRandomValue(0, 100) < 60) {
                    Vector3 from = Vector3Add(b->pos, Vector3Scale(Rng_UnitVecG(), b->radius * 2.2f));
                    Particles_Emit(from, Vector3Scale(Vector3Subtract(b->pos, from), 2.0f), V4(d->glow.x, d->glow.y, d->glow.z, 0.0f), V4(d->glow.x, d->glow.y, d->glow.z, 1.0f), 0.3f, 0.1f, 0.45f, 0.0f, PF_ADD);
                }
                if (b->patternT >= 1.1f / rate) {
                    b->patternStep = 1;
                    b->timer = 1.0f;
                    Sfx_Play3D(SFX_BOSS_CHARGE, b->pos, 1.0f);
                    Cam_AddTrauma(0.2f);
                }
            } else {
                float sp = (b->type == BOSS_LEVIATHAN ? 36.0f : 40.0f) * (0.9f + 0.1f * rate);
                b->vel = Vector3Scale(b->chargeDir, sp);
                if (GetRandomValue(0, 100) < 80) Particles_EmitStretch(b->pos, Vector3Scale(b->vel, -0.3f), V4(d->glow.x, d->glow.y, d->glow.z, 0.8f), V4(0.2f, 0.1f, 0.1f, 0.0f), b->radius * 0.3f, 0.4f, 0.05f, PF_ADD);
                float o = Cave_Sample(Vector3Add(b->pos, Vector3Scale(b->chargeDir, b->radius)));
                if (b->timer <= 0.0f || o < 0.5f) {
                    if (o < 0.5f) {
                        Cam_AddTrauma(0.45f);
                        Sfx_Play3D(SFX_EXPLODE, b->pos, 1.0f);
                        Particles_Burst(Vector3Add(b->pos, Vector3Scale(b->chargeDir, b->radius)), 50, 12.0f, V4(1.4f, 0.8f, 0.6f, 1.0f), V4(0.3f, 0.1f, 0.1f, 0.0f), 0.5f, 1.0f, 0);
                        b->vel = Vector3Scale(b->chargeDir, -8.0f);
                    }
                    sChargeCount++;
                    int maxCharges = ((b->type == BOSS_LEVIATHAN && b->phase >= 1) || (b->type == BOSS_SENTINEL && b->phase >= 2)) ? 2 : 1;
                    if (sChargeCount < maxCharges) { b->patternStep = 0; b->patternT = 0.35f; }
                    else EndPattern();
                }
            }
            break;
        case BP_VORTEX:
            if (b->patternStep == 0) {
                Hazards_Spawn(HZ_VORTEX, b->pos, 36.0f, 5.0f, 26.0f + 6.0f * (float)b->phase);
                b->patternStep = 1;
                Sfx_Play3D(SFX_RUMBLE, b->pos, 1.0f);
            }
            b->vel = Vector3Scale(b->vel, expf(-3.0f * dt));
            if (b->patternT > 5.0f) EndPattern();
            break;
        case BP_SUMMON:
            if (b->patternStep == 0 && b->patternT > 0.7f) { SummonAdds(); b->patternStep = 1; }
            if (b->patternT > 1.5f) EndPattern();
            break;
        case BP_SHOCKWAVE:
            if (b->timer <= 0.0f && b->patternStep < 2) {
                Hazards_Spawn(HZ_SHOCKWAVE, b->pos, 48.0f, 1.9f, 20.0f);
                b->patternStep++;
                b->timer = 1.3f;
                b->flash = 1.0f;
                Cam_AddTrauma(0.35f);
                Sfx_Play3D(SFX_EXPLODE, b->pos, 1.0f);
            }
            if (b->patternT > 3.2f) EndPattern();
            break;
        case BP_MUCUS:
            if (b->timer <= 0.0f && b->patternStep < 5 + b->phase) {
                b->timer = 0.4f / rate;
                b->patternStep++;
                Vector3 to = Vector3Subtract(Vector3Add(p->pos, Vector3Scale(Rng_UnitVecG(), 4.0f)), b->pos);
                float dist = Vector3Length(to);
                Vector3 v = Vector3Scale(Vector3Normalize(to), 20.0f);
                v.y += dist * 0.13f;
                Proj_Spawn(PT_MUCUS, Vector3Add(b->pos, Vector3Scale(Vector3Normalize(to), b->radius)), v, 12.0f * DmgMul(), false);
                Sfx_Play3D(SFX_BOSS_SHOOT, b->pos, 0.7f);
            }
            if (b->patternT > 3.0f) EndPattern();
            break;
        case BP_VOLLEY:
            if (b->timer <= 0.0f && b->patternStep < 3) {
                b->timer = 0.7f / rate;
                b->patternStep++;
                Vector3 dir = ToPlayer(b->pos);
                Vector3 right = Vector3Normalize(Vector3CrossProduct(dir, (Vector3){ 0, 1, 0 }));
                Vector3 up = Vector3CrossProduct(right, dir);
                if (b->type == BOSS_OVERMIND) {
                    for (int i = 0; i < 6; i++) {
                        Vector3 dd = Vector3Normalize(Vector3Add(dir, Vector3Add(Vector3Scale(right, Rng_RangeG(-0.8f, 0.8f)), Vector3Scale(up, Rng_RangeG(-0.6f, 0.8f)))));
                        int pi = Proj_Spawn(PT_BOLT, Vector3Add(b->pos, Vector3Scale(dd, b->radius)), Vector3Scale(dd, 22.0f), 10.0f * DmgMul(), false);
                        if (pi >= 0) { G.proj[pi].homing = 1.4f; G.proj[pi].targetPlayer = true; }
                    }
                } else {
                    for (int i = -3; i <= 3; i++) {
                        Vector3 dd = Vector3Add(dir, Vector3Scale(right, (float)i * 0.12f));
                        dd = Vector3Add(dd, Vector3Scale(up, (b->patternStep % 2 ? 0.06f : -0.06f)));
                        Shoot(PT_BOSS_ORB, Vector3Add(b->pos, Vector3Scale(Vector3Normalize(dd), b->radius)), dd, 20.0f + 3.0f * (float)b->phase, 10.0f);
                    }
                }
                Sfx_Play3D(SFX_BOSS_SHOOT, b->pos, 0.9f);
                b->flash = 0.5f;
            }
            if (b->patternT > 2.6f) EndPattern();
            break;
        case BP_SPIRAL:
            b->vel = Vector3Scale(b->vel, expf(-2.0f * dt));
            if (b->timer <= 0.0f) {
                b->timer = 0.11f / rate;
                b->spiralAngle += 0.23f;
                int arms = 4 + b->phase;
                Vector3 axis = Vector3Normalize(Vector3Add((Vector3){ 0, 1, 0 }, Vector3Scale(ToPlayer(b->pos), 0.0f)));
                Vector3 u = MU_AnyPerp(axis), v = Vector3CrossProduct(axis, u);
                for (int k = 0; k < arms; k++) {
                    float t = b->spiralAngle + (float)k / (float)arms * 2.0f * PI;
                    Vector3 dd = Vector3Add(Vector3Scale(u, cosf(t)), Vector3Scale(v, sinf(t)));
                    dd.y += 0.15f * sinf(b->patternT * 2.0f);
                    Shoot(PT_BOSS_ORB, Vector3Add(b->pos, Vector3Scale(Vector3Normalize(dd), b->radius)), dd, 15.0f, 8.0f);
                }
                Sfx_PlayThrottled(SFX_BOSS_SHOOT, 0.35f, 0.2f);
            }
            if (b->patternT > 4.2f) EndPattern();
            break;
        case BP_GUST:
            if (b->patternStep == 0) {
                if (GetRandomValue(0, 100) < 80) {
                    Vector3 from = Vector3Add(b->pos, Vector3Scale(Rng_UnitVecG(), b->radius * 4.0f));
                    Particles_EmitStretch(from, Vector3Scale(Vector3Subtract(b->pos, from), 1.5f), V4(1.4f, 1.2f, 1.3f, 0.0f), V4(1.0f, 0.9f, 1.0f, 0.7f), 0.2f, 0.6f, 0.08f, PF_ADD);
                }
                if (b->patternT > 1.0f) {
                    b->patternStep = 1;
                    Sfx_Play(SFX_RUMBLE, 1.0f, 1.4f);
                    Cam_AddTrauma(0.4f);
                    Vector3 dir = ToPlayer(b->pos);
                    float dist = Vector3Distance(b->pos, p->pos);
                    if (dist < 60.0f && p->invuln <= 0.0f) {
                        p->vel = Vector3Add(p->vel, Vector3Scale(dir, 48.0f * (1.0f - dist / 70.0f)));
                        Combat_DamagePlayer(6.0f * DmgMul(), b->pos, DMG_ENEMY);
                    }
                    for (int i = 0; i < 80; i++) {
                        Vector3 dd = Vector3Normalize(Vector3Add(dir, Vector3Scale(Rng_UnitVecG(), 0.5f)));
                        Particles_EmitStretch(Vector3Add(b->pos, Vector3Scale(dd, b->radius)), Vector3Scale(dd, Rng_RangeG(30.0f, 60.0f)), V4(1.2f, 1.1f, 1.2f, 0.8f), V4(0.3f, 0.3f, 0.3f, 0.0f), 0.3f, 0.7f, 0.05f, PF_ADD);
                    }
                }
            }
            if (b->patternT > 1.8f) EndPattern();
            break;
        case BP_ACID:
            if (b->timer <= 0.0f && b->patternStep < 3 + b->phase) {
                b->timer = 0.5f / rate;
                b->patternStep++;
                float dist = Vector3Distance(p->pos, b->pos);
                Vector3 aim = Vector3Add(p->pos, Vector3Scale(p->vel, dist / 26.0f));
                Vector3 dd = Vector3Normalize(Vector3Subtract(aim, b->pos));
                Vector3 v = Vector3Scale(dd, 26.0f);
                v.y += dist * 0.11f;
                Proj_Spawn(PT_ACIDGLOB, Vector3Add(b->pos, Vector3Scale(dd, b->radius + 0.5f)), v, 14.0f * DmgMul(), false);
                Sfx_Play3D(SFX_BOSS_SHOOT, b->pos, 0.8f);
                b->flash = 0.4f;
            }
            if (b->patternT > 2.6f) EndPattern();
            break;
        case BP_RING:
            if (b->timer <= 0.0f && b->patternStep < 3) {
                b->timer = 0.9f / rate;
                b->patternStep++;
                if (b->type == BOSS_LEVIATHAN) {
                    for (int s = 0; s < b->segCount; s += 2) {
                        Vector3 prev = (s == 0) ? b->pos : b->seg[s - 1];
                        Ring(b->seg[s], Vector3Subtract(prev, b->seg[s]), 6, 13.0f, 8.0f, (float)b->patternStep * 0.5f);
                    }
                } else {
                    Vector3 ax = Vector3Normalize(Vector3Add(ToPlayer(b->pos), Vector3Scale(Rng_UnitVecG(), 0.4f)));
                    Ring(b->pos, ax, 18 + 4 * b->phase, 16.0f, 9.0f, (float)b->patternStep * 0.3f);
                }
                Sfx_Play3D(SFX_BOSS_SHOOT, b->pos, 1.0f);
                b->flash = 0.6f;
            }
            if (b->patternT > 3.0f) EndPattern();
            break;
        case BP_SHIELD:
            if (!b->shielded) SpawnShieldNodes(5);
            EndPattern();
            break;
        case BP_TELEPORT:
            if (b->patternStep == 0) {
                b->invuln = 0.3f;
                if (b->patternT > 0.6f) {
                    Particles_Burst(b->pos, 60, 12.0f, V4(1.4f, 1.0f, 2.8f, 1.0f), V4(0.2f, 0.1f, 0.6f, 0.0f), 0.4f, 0.6f, PF_ADD);
                    Vector3 q;
                    Vector3 around = Vector3Add(p->pos, Vector3Scale(Rng_UnitVecG(), 22.0f));
                    if (Cave_RandomOpenPoint(&G.rng, b->radius + 1.0f, around, 8.0f, &q) && Vector3Distance(q, sCenter) < sArena * 1.6f) b->pos = b->prev = q;
                    else b->pos = b->prev = Vector3Add(sCenter, Vector3Scale(Rng_UnitVecG(), sArena * 0.4f));
                    b->patternStep = 1;
                    Sfx_Play3D(SFX_ZAP, b->pos, 1.0f);
                    Particles_Burst(b->pos, 60, 12.0f, V4(1.4f, 1.0f, 2.8f, 1.0f), V4(0.2f, 0.1f, 0.6f, 0.0f), 0.4f, 0.6f, PF_ADD);
                }
            } else if (b->patternStep == 1 && b->patternT > 1.1f) {
                Ring(b->pos, ToPlayer(b->pos), 12 + 4 * b->phase, 15.0f, 8.0f, 0.0f);
                b->patternStep = 2;
            }
            if (b->patternT > 1.6f) EndPattern();
            break;
        case BP_STORM:
            if (b->timer <= 0.0f && b->patternStep < 6 + b->phase * 2) {
                b->timer = 0.45f / rate;
                b->patternStep++;
                Vector3 c = Vector3Add(p->pos, Vector3Scale(Rng_InSphereG(), 9.0f));
                if (b->patternStep % 3 == 1) c = Vector3Add(p->pos, Vector3Scale(p->vel, 1.0f));
                if (Cave_Sample(c) > 0.5f) Hazards_Spawn(HZ_STORM, c, 5.5f, 3.0f, 22.0f);
            }
            if (b->patternT > 4.0f) EndPattern();
            break;
        default:
            EndPattern();
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Movement                                                                   */
/* ------------------------------------------------------------------------- */

static void Move(float dt)
{
    Boss *b = &G.boss;
    Player *p = &G.player;
    const BossDef *d = &BOSSES[b->type];
    bool charging = (b->pattern == BP_CHARGE && b->patternStep == 1);
    Vector3 desired = { 0 };
    float t = b->anim;
    if (!sEngaged) {
        /* the guardian holds its arena until the pathogen arrives */
        desired = Vector3Scale(Vector3Subtract(Vector3Add(sCenter, (Vector3){ 0, sinf(t * 0.5f) * 2.0f, 0 }), b->pos), 0.6f);
        b->vel = V3Damp(b->vel, desired, 1.5f, dt);
        b->facing = Vector3Normalize(Vector3Lerp(b->facing, ToPlayer(b->pos), 1.0f - expf(-1.0f * dt)));
        b->prev = b->pos;
        b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
        for (int i = 0; i < b->segCount; i++) {
            Vector3 prev = (i == 0) ? b->pos : b->seg[i - 1];
            Vector3 dv = Vector3Subtract(b->seg[i], prev);
            float l = Vector3Length(dv);
            if (l > b->radius && l > 0.001f) b->seg[i] = Vector3Add(prev, Vector3Scale(dv, b->radius / l));
        }
        return;
    }
    switch (b->type) {
        case BOSS_SENTINEL: {
            Vector3 to = Vector3Subtract(p->pos, b->pos);
            float dist = Vector3Length(to);
            Vector3 dir = dist > 0.01f ? Vector3Scale(to, 1.0f / dist) : (Vector3){ 0 };
            float want = 17.0f;
            desired = Vector3Scale(dir, Clamp((dist - want) * 0.6f, -d->speed, d->speed));
            desired = Vector3Add(desired, Vector3Scale(Vector3CrossProduct(dir, (Vector3){ 0, 1, 0 }), d->speed * 0.4f * sinf(t * 0.3f)));
        } break;
        case BOSS_WARDEN: {
            Vector3 orbit = Vector3Add(sCenter, (Vector3){ cosf(t * 0.25f) * sArena * 0.3f, sinf(t * 0.4f) * 3.0f, sinf(t * 0.25f) * sArena * 0.3f });
            desired = Vector3Scale(Vector3Subtract(orbit, b->pos), 0.5f);
        } break;
        case BOSS_LEVIATHAN: {
            float R = sArena * 0.75f;
            Vector3 orbit = Vector3Add(sCenter, (Vector3){ cosf(t * 0.45f) * R, sinf(t * 0.9f) * R * 0.3f, sinf(t * 0.45f) * R });
            Vector3 aim = Vector3Lerp(orbit, p->pos, 0.25f + 0.1f * (float)b->phase);
            desired = Vector3Scale(Vector3Normalize(Vector3Subtract(aim, b->pos)), d->speed * (1.0f + 0.15f * (float)b->phase));
        } break;
        case BOSS_CITADEL:
            desired = Vector3Scale(Vector3Subtract(Vector3Add(sCenter, (Vector3){ 0, sinf(t * 0.5f) * 1.5f, 0 }), b->pos), 1.0f);
            break;
        default: {
            Vector3 hover = Vector3Add(sCenter, (Vector3){ sinf(t * 0.21f) * sArena * 0.35f, sinf(t * 0.5f) * 4.0f, cosf(t * 0.17f) * sArena * 0.35f });
            if (b->pattern == BP_TELEPORT && b->patternStep >= 1) hover = b->pos;
            desired = Vector3Scale(Vector3Subtract(hover, b->pos), 0.6f);
        } break;
    }
    if (!charging) {
        float rate = (b->pattern == BP_CHARGE) ? 4.0f : 1.5f;
        b->vel = V3Damp(b->vel, desired, rate, dt);
        Vector3 face = ToPlayer(b->pos);
        if (b->type == BOSS_LEVIATHAN && Vector3Length(b->vel) > 1.0f) face = Vector3Normalize(b->vel);
        b->facing = Vector3Normalize(Vector3Lerp(b->facing, face, 1.0f - expf(-2.5f * dt)));
    }
    b->prev = b->pos;
    b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
    Vector3 g;
    float o = Cave_SampleGrad(b->pos, &g);
    float clearance = b->radius * 0.85f;
    if (o < clearance) b->pos = Vector3Add(b->pos, Vector3Scale(g, (clearance - o) * 0.5f));

    /* worm body follows the head like a chain */
    for (int i = 0; i < b->segCount; i++) {
        Vector3 prev = (i == 0) ? b->pos : b->seg[i - 1];
        Vector3 dv = Vector3Subtract(b->seg[i], prev);
        float l = Vector3Length(dv);
        float spacing = b->radius * 1.0f;
        if (l > spacing && l > 0.001f) b->seg[i] = Vector3Add(prev, Vector3Scale(dv, spacing / l));
        float so = Cave_SampleGrad(b->seg[i], &g);
        float sr = SegRadius(i) * 0.8f;
        if (so < sr) b->seg[i] = Vector3Add(b->seg[i], Vector3Scale(g, (sr - so) * 0.5f));
    }
}

static void ContactDamage(float dt)
{
    Boss *b = &G.boss;
    Player *p = &G.player;
    sContactCd -= dt;
    if (p->dead || sContactCd > 0.0f) return;
    Vector3 hit;
    if (Bosses_HitTest(p->pos, p->radius + 0.3f, &hit)) {
        bool charging = (b->pattern == BP_CHARGE && b->patternStep == 1);
        float dmg = BOSSES[b->type].damage * DmgMul() * (charging ? 1.5f : 0.8f);
        if (p->invuln <= 0.0f) {
            Combat_DamagePlayer(dmg, b->pos, DMG_ENEMY);
            p->vel = Vector3Add(p->vel, Vector3Scale(ToPlayer(b->pos), charging ? 30.0f : 14.0f));
        }
        sContactCd = 0.9f;
    }
    /* virions touching the boss get crushed */
    (void)dt;
}

static void DeathSequence(float dt)
{
    Boss *b = &G.boss;
    const BossDef *d = &BOSSES[b->type];
    b->deathT += dt;
    b->vel = Vector3Scale(b->vel, expf(-2.0f * dt));
    b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
    b->flash = 0.5f + 0.5f * sinf(b->deathT * 30.0f);
    if (GetRandomValue(0, 100) < 25) {
        Vector3 e = Vector3Add(b->pos, Vector3Scale(Rng_UnitVecG(), b->radius * Rng_RangeG(0.3f, 1.0f)));
        if (b->segCount > 0 && Rng_FloatG() < 0.5f) e = b->seg[Rng_IntG(0, b->segCount - 1)];
        Particles_Burst(e, 30, 10.0f, V4(d->glow.x, d->glow.y, d->glow.z, 1.0f), V4(0.3f, 0.2f, 0.1f, 0.0f), 0.5f, 0.8f, PF_ADD);
        Lights_AddTemp(e, d->glow, 18.0f, 0.3f);
        Sfx_PlayThrottled(SFX_EXPLODE, 0.6f, 0.15f);
        Cam_AddTrauma(0.15f);
    }
    if (b->deathT >= 3.2f) {
        Vector3 at = b->pos;
        Particles_Burst(at, 220, 34.0f, V4(d->glow.x * 1.5f, d->glow.y * 1.5f, d->glow.z * 1.5f, 1.0f), V4(0.3f, 0.2f, 0.2f, 0.0f), 0.8f, 2.0f, PF_ADD);
        Particles_Burst(at, 120, 14.0f, V4(d->color.x, d->color.y, d->color.z, 0.9f), V4(0.2f, 0.1f, 0.1f, 0.0f), 1.6f, 3.0f, PF_FLOW);
        Particles_Ring(at, (Vector3){ 0, 1, 0 }, 120, 40.0f, V4(3.0f, 3.0f, 3.0f, 1.0f), V4(0.4f, 0.4f, 0.4f, 0.0f), 0.6f, 1.2f);
        Lights_AddTemp(at, (Vector3){ 6.0f, 6.0f, 6.0f }, 60.0f, 1.6f);
        HUD_Flash((Color){ 255, 255, 255, 255 }, 0.9f);
        Cam_AddTrauma(1.0f);
        Sfx_Play(SFX_BOSS_DEATH, 1.0f, 0.8f);
        int ri = G.region.index;
        int gm = 120 + 60 * ri;
        for (int i = 0; i < 16; i++) {
            int k = Pickups_Spawn(PK_GM, Vector3Add(at, Vector3Scale(Rng_UnitVecG(), b->radius * 0.6f)), (float)gm / 16.0f);
            if (k >= 0) G.pickups[k].vel = Vector3Scale(Rng_UnitVecG(), 18.0f);
        }
        for (int i = 0; i < 6; i++) Pickups_Spawn(PK_BIOMASS, Vector3Add(at, Vector3Scale(Rng_UnitVecG(), b->radius)), 25.0f);
        Player_GainEP(50.0f + 25.0f * (float)ri);
        G.run.stats.bossKills++;
        HUD_Achievement(ACH_GIANT_SLAYER);
        Music_Stinger(3);
        b->active = false;
        b->dying = false;
        b->segCount = 0;
        if (G.run.mode == MODE_CAMPAIGN) {
            G.region.bossDefeated = true;
            G.region.portalOpen = true;
            G.region.portalPos = sCenter;
            HUD_Banner("GUARDIAN DESTROYED", "A metastasis channel has opened at the heart of the arena", (Color){ 140, 255, 190, 255 });
            HUD_Hint(TUT_PORTAL, "Swim into the glowing metastasis channel to spread to the next region. Collect the scattered genetic material first!");
        } else {
            HUD_Banner("GUARDIAN DESTROYED", "The outbreak continues", (Color){ 140, 255, 190, 255 });
        }
    }
}

void Bosses_UpdateIntro(float dt)
{
    Boss *b = &G.boss;
    if (!b->active) return;
    b->anim += dt;
    b->flash = 0.3f + 0.3f * sinf(b->anim * 6.0f);
    const BossDef *d = &BOSSES[b->type];
    if (GetRandomValue(0, 100) < 70) {
        Vector3 from = Vector3Add(b->pos, Vector3Scale(Rng_UnitVecG(), b->radius * 3.0f));
        Particles_Emit(from, Vector3Scale(Vector3Subtract(b->pos, from), 1.2f), V4(d->glow.x, d->glow.y, d->glow.z, 0.0f), V4(d->glow.x, d->glow.y, d->glow.z, 1.0f), 0.35f, 0.1f, 0.8f, 0.0f, PF_ADD);
    }
    Move(dt * 0.3f);
}

void Bosses_Update(float dt)
{
    Boss *b = &G.boss;
    sShieldMsgT -= dt;
    if (!b->active) return;
    b->anim += dt;
    b->flash = fmaxf(0.0f, b->flash - dt * 3.0f);
    b->invuln = fmaxf(0.0f, b->invuln - dt);
    if (b->dying) { DeathSequence(dt); return; }
    if (!b->introDone) return;

    sEngaged = G.run.mode == MODE_OUTBREAK || Vector3Distance(G.player.pos, sCenter) < sArena * 2.0f + 30.0f || b->hp < b->maxHp;
    if (!sEngaged) { b->pattern = BP_NONE; b->attackT = fmaxf(b->attackT, 1.2f); }
    else if (b->pattern == BP_NONE) {
        b->attackT -= dt;
        if (b->attackT <= 0.0f && !G.player.dead) ChoosePattern();
    } else {
        RunPattern(dt);
    }
    Move(dt);
    ContactDamage(dt);

    /* ambient boss effects */
    const BossDef *d = &BOSSES[b->type];
    if (GetRandomValue(0, 100) < 30) {
        Vector3 s = Vector3Add(b->pos, Vector3Scale(Rng_UnitVecG(), b->radius * 1.05f));
        Particles_Emit(s, Vector3Scale(Vector3Normalize(Vector3Subtract(s, b->pos)), 1.5f), V4(d->glow.x * 0.6f, d->glow.y * 0.6f, d->glow.z * 0.6f, 0.0f),
                       V4(d->glow.x * 0.5f, d->glow.y * 0.5f, d->glow.z * 0.5f, 0.6f), 0.3f, 0.6f, 1.6f, 0.5f, PF_ADD | PF_FLOW);
    }
}

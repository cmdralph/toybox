/*
 * enemies.c - the innate and adaptive immune response.
 *
 * Every immune cell runs the same perception tick (vision cone + line of
 * sight through the cave SDF) feeding a small state machine:
 *
 *   PATROL -> (sees player)  -> CHASE -> WINDUP -> ATTACK -> CHASE ...
 *          -> (alerted)      -> INVESTIGATE -> PATROL
 *   CHASE  -> (lost 3.5 s)   -> INVESTIGATE
 *
 * Kind-specific behaviour layers on top:
 *   Neutrophil  fast swarmer, lunges; dies into a sticky NET (NETosis)
 *   Macrophage  slow tank, eats virions, engulfs small pathogens whole
 *   NK cell     hunts *infected cells* and cleanses them; telegraphed dash
 *   T cell      keeps range, fires perforin bolts with target leading
 *   B cell      hangs back and releases antibody swarms that opsonise you
 *   Antibody    homing Y-protein that sticks: slows you, amplifies damage
 *
 * The director adds reinforcements as Alert rises (weighted by the immune
 * system's memory of your tactics), triggers complement-cascade rings at
 * high Alert, and runs Outbreak mode waves.
 */
#include "entities/enemies.h"
#include "entities/projectiles.h"
#include "entities/minions.h"
#include "entities/bosses.h"
#include "entities/pickups.h"
#include "entities/player.h"
#include "systems/ai.h"
#include "systems/combat.h"
#include "systems/infection.h"
#include "systems/immune_memory.h"
#include "systems/spatial_hash.h"
#include "world/region.h"
#include "world/hazards.h"
#include "world/events.h"
#include "world/flow_field.h"
#include "world/sdf_cave.h"
#include "gfx/particles.h"
#include "gfx/camera.h"
#include "audio/sfx.h"
#include "ui/hud.h"
#include "ui/bestiary_ui.h"
#include "core/settings.h"
#include "core/pool.h"
#include "core/rng.h"
#include "core/math_utils.h"
#include <stdio.h>

static const EnemyDef DEFS[AK_COUNT] = {
    [AK_BACTERIA]   = { "Commensal Bacterium", 20.0f, 3.5f, 0.85f, 0.0f, 0.0f, 1.0f, 1, CX_BACTERIA, { 0.5f, 1.0f, 0.4f } },
    [AK_NEUTROPHIL] = { "Neutrophil", 22.0f, 13.0f, 0.95f, 6.0f, 26.0f, 2.0f, 3, CX_NEUTROPHIL, { 0.85f, 0.75f, 1.0f } },
    [AK_MACROPHAGE] = { "Macrophage", 140.0f, 6.0f, 2.6f, 10.0f, 22.0f, 8.0f, 12, CX_MACROPHAGE, { 1.0f, 0.85f, 0.6f } },
    [AK_NK]         = { "Natural Killer Cell", 60.0f, 10.0f, 1.2f, 14.0f, 30.0f, 5.0f, 7, CX_NK, { 1.0f, 0.35f, 0.35f } },
    [AK_TCELL]      = { "Cytotoxic T Cell", 45.0f, 9.0f, 1.0f, 8.0f, 40.0f, 4.0f, 6, CX_TCELL, { 0.5f, 0.8f, 1.0f } },
    [AK_BCELL]      = { "Plasma B Cell", 55.0f, 7.0f, 1.2f, 0.0f, 36.0f, 4.0f, 6, CX_BCELL, { 1.0f, 0.85f, 0.4f } },
    [AK_ANTIBODY]   = { "Antibody (IgG)", 4.0f, 18.0f, 0.35f, 0.0f, 60.0f, 0.0f, 0, CX_ANTIBODY, { 1.0f, 0.95f, 0.5f } },
};

const EnemyDef *Enemies_Def(int kind)
{
    if (kind < 0 || kind >= AK_COUNT || DEFS[kind].name == NULL) return NULL;
    return &DEFS[kind];
}

/* group ids: 0 patrol, 1 reinforcement, 2 outbreak wave, 3 boss summon */
#define GROUP_PATROL 0
#define GROUP_REINF  1
#define GROUP_WAVE   2
#define GROUP_SUMMON 3

static float sSeenT = 0.0f;
static float sCompT = 0.0f;
static bool  sWaveActive = false;
static int   sWaveRemaining = 0;
static bool  sHintNK = false;

void Enemies_Reset(void)
{
    sSeenT = 0.0f;
    sCompT = 14.0f;
    sWaveActive = false;
    sWaveRemaining = 0;
    sHintNK = false;
}

bool Enemies_Detected(void) { return sSeenT > 0.0f; }
int  Enemies_WaveRemaining(void) { return sWaveRemaining; }

int Enemies_Active(void)
{
    int n = 0;
    for (int k = AK_NEUTROPHIL; k <= AK_BCELL; k++) n += G.agentCount[k];
    return n;
}

int Enemies_Spawn(AgentKind kind, Vector3 pos, bool patrol)
{
    const EnemyDef *d = Enemies_Def(kind);
    if (!d) return -1;
    int i = Agent_Alloc();
    if (i < 0) return -1;
    Agent *a = &G.agents[i];
    a->kind = (uint8_t)kind;
    a->team = TEAM_IMMUNE;
    a->pos = a->prev = pos;
    a->anchor = pos;
    a->goal = pos;
    a->dir = Rng_UnitVecG();
    a->radius = d->radius * Rng_RangeG(0.92f, 1.08f);
    a->hp = a->maxHp = d->hp * Region_EnemyHpMul();
    a->vision = d->vision;
    a->speedMul = Rng_RangeG(0.92f, 1.08f);
    a->phase = Rng_FloatG();
    a->variant = (uint8_t)Rng_IntG(0, 3);
    a->spin = Rng_RangeG(-1.0f, 1.0f);
    a->state = patrol ? AI_PATROL : AI_INVESTIGATE;
    a->group = patrol ? GROUP_PATROL : GROUP_REINF;
    a->timer = patrol ? Rng_RangeG(0.0f, 4.0f) : 10.0f;
    a->perceptT = Rng_RangeG(0.0f, ENEMY_PERCEPT_TICK);
    if (!patrol && !G.player.dead) a->goal = G.player.pos;
    if (kind != AK_ANTIBODY && Rng_FloatG() < 0.035f + 0.025f * (float)G.region.index + 0.03f * (float)G.run.ngPlus) {
        a->flags |= AF_ELITE;
        a->hp *= 2.2f;
        a->maxHp *= 2.2f;
        a->radius *= 1.25f;
        a->vision *= 1.2f;
    }
    if (kind == AK_ANTIBODY) a->timer = 11.0f;
    Immune_ApplyToSpawn(a);
    G.agentCount[kind]++;
    return i;
}

void Enemies_OnDeath(Agent *a)
{
    Player *p = &G.player;
    if (p->engulfedBy.idx == (int)(a - G.agents)) {
        p->engulfedBy = REF_NONE;
        p->vel = Vector3Scale(Rng_UnitVecG(), 12.0f);
    }
    if (a->kind == AK_NEUTROPHIL && Rng_FloatG() < 0.18f) {
        /* NETosis: the dying neutrophil casts a sticky web of chromatin */
        Hazards_Spawn(HZ_MUCUS, a->pos, 4.5f, 9.0f, 0.5f);
        Particles_Burst(a->pos, 30, 5.0f, V4(0.9f, 0.85f, 1.0f, 0.8f), V4(0.6f, 0.5f, 0.8f, 0.0f), 0.25f, 1.6f, 0);
    }
}

/* ------------------------------------------------------------------------- */
/* Behaviour helpers                                                          */
/* ------------------------------------------------------------------------- */

static bool TargetInfo(Agent *a, Vector3 *pos, float *radius, Vector3 *vel)
{
    if (a->targetPlayer) {
        if (G.player.dead) return false;
        *pos = G.player.pos; *radius = G.player.radius; *vel = G.player.vel;
        return true;
    }
    Agent *t = Agent_Get(a->target);
    if (!t) return false;
    *pos = t->pos; *radius = t->radius; *vel = t->vel;
    return true;
}

static void Perceive(Agent *a)
{
    Player *p = &G.player;
    a->perceptT = ENEMY_PERCEPT_TICK + Rng_FloatG() * 0.06f;
    if (a->state == AI_STUNNED || a->state == AI_ENGULF) return;

    bool sees = AI_CanSeePlayer(a);
    if (sees) {
        sSeenT = 0.35f;
        a->timer2 = 0.0f;
        a->goal = p->pos;
        if (!(a->flags & AF_SEEN)) {
            a->flags |= AF_SEEN;
            const EnemyDef *d = Enemies_Def(a->kind);
            if (d) Codex_Unlock(d->codex);
        }
        if (a->kind == AK_ANTIBODY) return;
        if (!a->targetPlayer || (a->state != AI_CHASE && a->state != AI_ATTACK && a->state != AI_WINDUP)) {
            bool wasCalm = !(a->flags & AF_ALERTED);
            a->targetPlayer = true;
            a->target = REF_NONE;
            if (a->state != AI_ATTACK && a->state != AI_WINDUP) a->state = AI_CHASE;
            a->flags |= AF_ALERTED;
            if (wasCalm) {
                AI_AlertGroup(a->pos, ENEMY_GROUP_ALERT, p->pos);
                Sfx_PlayThrottled(SFX_DETECTED, 0.6f, 1.5f);
                HUD_Hint(TUT_ALERT, "You've been spotted! Being seen raises ALERT, which summons reinforcements. Break line of sight to lose them.");
            }
        }
    } else if (a->targetPlayer && (a->state == AI_CHASE)) {
        a->timer2 += ENEMY_PERCEPT_TICK;
        if (a->timer2 > 3.5f) {
            a->targetPlayer = false;
            a->state = AI_INVESTIGATE;
            a->timer = 7.0f;
        }
    }

    /* decoys lure everything that can see them */
    if (a->kind != AK_ANTIBODY && G.agentCount[AK_DECOY] > 0) {
        int dcy = SH_Nearest(a->pos, a->vision * 0.9f, KMASK(AK_DECOY), -1);
        if (dcy >= 0 && Cave_LineOfSight(a->pos, G.agents[dcy].pos)) {
            a->targetPlayer = false;
            a->target = Agent_Ref(dcy);
            if (a->state != AI_ATTACK && a->state != AI_WINDUP) a->state = AI_CHASE;
            return;
        }
    }

    if (!a->targetPlayer && a->kind != AK_ANTIBODY && a->kind != AK_BCELL) {
        Agent *cur = Agent_Get(a->target);
        if (!cur || cur->kind == AK_DECOY) {
            /* NK cells sense stressed (infected) cells by their missing MHC-I */
            if (a->kind == AK_NK) {
                int ids[48];
                int n = SH_Query(a->pos, a->vision * 1.5f, ids, 48, KMASK(AK_HOST));
                int best = -1;
                float bd = 1e9f;
                for (int k = 0; k < n; k++) {
                    Agent *h = &G.agents[ids[k]];
                    if (!(h->flags & AF_INFECTED)) continue;
                    float d = Vector3DistanceSqr(h->pos, a->pos);
                    if (d < bd) { bd = d; best = ids[k]; }
                }
                if (best >= 0) {
                    a->target = Agent_Ref(best);
                    a->state = AI_CHASE;
                    if (!sHintNK && Vector3Distance(a->pos, p->pos) < 60.0f) {
                        sHintNK = true;
                        HUD_Hint(TUT_NK, "NK cells hunt your infected cells and cleanse them. Defend your factories: hold LMB to spit toxin, Q for a lysis burst.");
                    }
                    return;
                }
            }
            /* swarm virions are fair game too */
            uint32_t mask = KMASK(AK_MINION);
            int m = SH_Nearest(a->pos, a->vision * 0.6f, mask, -1);
            if (m >= 0 && !(G.stats.synGhostSwarm && p->cloaked)) {
                a->target = Agent_Ref(m);
                a->state = AI_CHASE;
            }
        }
    }
}

static Vector3 Separation(Agent *a, float dt)
{
    (void)dt;
    float spread = 1.0f + 1.2f * Immune_Resist(IM_BURST);
    float ali = (a->kind == AK_NEUTROPHIL) ? 0.25f : 0.05f;
    float coh = (a->kind == AK_NEUTROPHIL) ? 0.06f : 0.0f;
    return AI_Flock(a, AGENT_MASK_IMMUNE_CELLS, (3.0f + a->radius * 2.0f) * spread, 1.0f * spread, ali, coh);
}

static void MeleeContact(Agent *a, Vector3 tpos, float tr, float dmg, float cd)
{
    float d = Vector3Distance(a->pos, tpos);
    if (d > a->radius + tr + 0.25f || a->cooldown > 0.0f) return;
    a->cooldown = cd;
    if (a->targetPlayer) {
        Combat_DamagePlayer(dmg, a->pos, DMG_ENEMY);
    } else {
        Agent *t = Agent_Get(a->target);
        if (t && t->kind != AK_HOST) Combat_DamageAgent(t, dmg, DMG_ENEMY, Vector3Subtract(t->pos, a->pos));
    }
    a->vel = Vector3Scale(a->vel, -0.3f);
}

/* ------------------------------------------------------------------------- */
/* Per-kind updates                                                           */
/* ------------------------------------------------------------------------- */

static void UpdateAntibody(Agent *a, float dt, float spd)
{
    Player *p = &G.player;
    a->timer -= dt;
    if (a->timer <= 0.0f) {
        Particles_Burst(a->pos, 6, 2.0f, V4(1.2f, 1.1f, 0.5f, 0.8f), V4(0.4f, 0.4f, 0.1f, 0.0f), 0.1f, 0.4f, PF_ADD);
        Agent_Free((int)(a - G.agents));
        return;
    }
    Vector3 tp, tv;
    float tr;
    if (!TargetInfo(a, &tp, &tr, &tv)) {
        a->targetPlayer = !p->dead;
        Vector3 desired = Vector3Scale(a->dir, spd * 0.5f);
        AI_Integrate(a, desired, 2.0f, 0.5f, dt);
        return;
    }
    Vector3 desired = AI_Seek(a, tp, spd);
    AI_Integrate(a, desired, 2.6f, 0.2f, dt);
    a->spin += dt * 4.0f;
    if (Vector3Distance(a->pos, tp) < a->radius + tr) {
        if (a->targetPlayer) {
            if (p->invuln <= 0.0f) {
                p->antibodies = (p->antibodies < ANTIBODY_MAX) ? p->antibodies + 1 : ANTIBODY_MAX;
                p->antibodyDecay = 0.0f;
                Codex_Unlock(CX_ANTIBODY);
                Sfx_PlayThrottled(SFX_ANTIBODY, 0.7f, 0.1f);
                Particles_Burst(a->pos, 8, 3.0f, V4(1.6f, 1.5f, 0.6f, 1.0f), V4(0.5f, 0.4f, 0.1f, 0.0f), 0.15f, 0.4f, PF_ADD);
                HUD_Hint(TUT_ANTIBODY, "Antibodies are sticking to you: they slow you and amplify damage. A lysis burst (Q) shakes them all off.");
                Agent_Free((int)(a - G.agents));
            }
        } else {
            Agent *t = Agent_Get(a->target);
            if (t) Combat_DamageAgent(t, 3.0f, DMG_ENEMY, a->vel);
            Agent_Free((int)(a - G.agents));
        }
    }
}

static void UpdateMacrophageEngulf(Agent *a, float dt)
{
    Player *p = &G.player;
    if (p->engulfedBy.idx != (int)(a - G.agents) || p->dead) {
        /* the player broke free (player.c clears the ref) */
        a->state = AI_STUNNED;
        a->timer = 1.6f;
        a->cooldown = 4.0f;
        return;
    }
    a->timer -= dt;
    float dm = Region_EnemyDmgMul() * ((a->flags & AF_ELITE) ? 1.4f : 1.0f);
    p->invuln = 0.0f;
    Combat_DamagePlayer(DEFS[AK_MACROPHAGE].damage * dm * dt, a->pos, DMG_ENEMY);
    p->hurtFlash = fminf(p->hurtFlash, 0.5f);
    a->vel = Vector3Scale(a->vel, expf(-3.0f * dt));
    a->pos = Vector3Add(a->pos, Vector3Scale(a->vel, dt));
    if (GetRandomValue(0, 100) < 30) Particles_Emit(Vector3Add(a->pos, Vector3Scale(Rng_UnitVecG(), a->radius * 0.6f)), (Vector3){ 0 },
                                                   V4(1.4f, 0.6f, 0.3f, 0.8f), V4(0.5f, 0.2f, 0.1f, 0.0f), 0.3f, 0.1f, 0.6f, 1.0f, PF_ADD);
    if (a->timer <= 0.0f && p->engulfedBy.idx >= 0) {
        /* spat out, partially digested */
        p->engulfedBy = REF_NONE;
        Combat_DamagePlayer(18.0f * dm, a->pos, DMG_ENEMY);
        p->vel = Vector3Scale(Rng_UnitVecG(), 16.0f);
        a->state = AI_STUNNED;
        a->timer = 1.2f;
        a->cooldown = 5.0f;
    }
}

void Enemies_Update(Agent *a, float dt)
{
    Player *p = &G.player;
    const EnemyDef *d = Enemies_Def(a->kind);
    float elite = (a->flags & AF_ELITE) ? 1.4f : 1.0f;
    float spd = d->speed * a->speedMul * Events_EnemySpeedMul() * (a->flags & AF_ELITE ? 1.1f : 1.0f);
    float dmg = d->damage * Region_EnemyDmgMul() * elite;
    a->cooldown -= dt;
    a->spin += dt;

    if (a->kind == AK_ANTIBODY) { UpdateAntibody(a, dt, spd); return; }

    a->perceptT -= dt;
    if (a->perceptT <= 0.0f) Perceive(a);

    /* reinforcements that wander far away quietly return to the lymph */
    if (a->group == GROUP_REINF && !(a->flags & AF_ALERTED) && Vector3Distance(a->pos, p->pos) > DESPAWN_DIST) {
        Agent_Free((int)(a - G.agents));
        return;
    }

    Vector3 desired = { 0 };
    float accel = 3.0f;
    Vector3 tp = { 0 }, tv = { 0 };
    float tr = 0.0f;
    bool hasTarget = (a->state == AI_CHASE || a->state == AI_ATTACK || a->state == AI_WINDUP) && TargetInfo(a, &tp, &tr, &tv);
    if ((a->state == AI_CHASE || a->state == AI_ATTACK || a->state == AI_WINDUP) && !hasTarget) {
        a->state = AI_INVESTIGATE;
        a->timer = 5.0f;
        a->target = REF_NONE;
        a->targetPlayer = false;
    }
    float dist = hasTarget ? Vector3Distance(a->pos, tp) : 1e9f;
    Agent *tAgent = a->targetPlayer ? NULL : Agent_Get(a->target);

    switch ((AiState)a->state) {
        case AI_IDLE:
        case AI_PATROL:
            a->timer -= dt;
            if (a->timer <= 0.0f || Vector3Distance(a->pos, a->goal) < 3.0f) {
                a->goal = AI_WanderGoal(a, a->group == GROUP_PATROL ? 26.0f : 40.0f);
                a->timer = Rng_RangeG(4.0f, 9.0f);
            }
            desired = AI_Arrive(a, a->goal, spd * 0.42f, 5.0f);
            accel = 2.0f;
            break;
        case AI_INVESTIGATE:
            a->timer -= dt;
            desired = AI_Arrive(a, a->goal, spd * 0.85f, 4.0f);
            if (a->timer <= 0.0f || Vector3Distance(a->pos, a->goal) < 3.5f) {
                a->state = AI_PATROL;
                a->anchor = a->pos;
                a->timer = Rng_RangeG(2.0f, 4.0f);
            }
            break;
        case AI_STUNNED:
            a->timer -= dt;
            desired = (Vector3){ 0 };
            accel = 0.8f;
            if (a->timer <= 0.0f) a->state = (a->flags & AF_ALERTED) ? AI_CHASE : AI_PATROL;
            break;
        case AI_ENGULF:
            UpdateMacrophageEngulf(a, dt);
            return;
        case AI_CHASE:
        case AI_WINDUP:
        case AI_ATTACK: {
            Vector3 lead = tp;
            if (a->targetPlayer) {
                /* adaptive: the more you dash, the more they predict you */
                float pred = 0.15f + 0.6f * Immune_Resist(IM_DASH);
                lead = Vector3Add(tp, Vector3Scale(tv, pred * Sat(dist / 20.0f)));
            }
            switch (a->kind) {
                case AK_NEUTROPHIL:
                    if (a->state == AI_CHASE) {
                        desired = AI_Seek(a, lead, spd);
                        accel = 3.5f;
                        if (dist < 9.0f + tr && a->cooldown <= 0.0f) { a->state = AI_WINDUP; a->timer = 0.28f; }
                    } else if (a->state == AI_WINDUP) {
                        a->timer -= dt;
                        desired = Vector3Scale(AI_Seek(a, tp, spd), -0.15f);
                        a->flash = fmaxf(a->flash, 0.35f);
                        if (a->timer <= 0.0f) {
                            a->state = AI_ATTACK;
                            a->timer = 0.42f;
                            a->vel = AI_Seek(a, lead, spd * 2.7f);
                        }
                    } else {
                        a->timer -= dt;
                        desired = a->vel;
                        accel = 0.5f;
                        if (a->timer <= 0.0f) { a->state = AI_CHASE; if (a->cooldown < 0.4f) a->cooldown = 0.7f; }
                    }
                    MeleeContact(a, tp, tr, dmg, 0.8f);
                    break;
                case AK_MACROPHAGE: {
                    desired = AI_Seek(a, lead, spd);
                    accel = 1.6f;
                    float touch = a->radius + tr + 0.3f;
                    if (dist < touch) {
                        if (a->targetPlayer) {
                            bool small = p->radius < a->radius * 0.78f;
                            if (small && p->invuln <= 0.0f && a->cooldown <= 0.0f && p->engulfedBy.idx < 0 && p->attach.idx < 0) {
                                p->engulfedBy = Agent_Ref((int)(a - G.agents));
                                p->engulfT = 0.0f;
                                p->struggle = 0;
                                a->state = AI_ENGULF;
                                a->timer = 3.4f;
                                Sfx_Play(SFX_ENGULF, 1.0f, 1.0f);
                                Cam_AddTrauma(0.3f);
                                HUD_FloatText("ENGULFED!  Mash DASH to break free", (Color){ 255, 160, 90, 255 });
                                HUD_Hint(TUT_ENGULF, "A macrophage swallowed you! Mash DASH to tear free before it digests you. Grow bigger and they can no longer engulf you.");
                                return;
                            }
                            MeleeContact(a, tp, tr, dmg, 1.0f);
                        } else if (tAgent && (tAgent->kind == AK_MINION || tAgent->kind == AK_DECOY)) {
                            /* phagocytosis */
                            Particles_Burst(tAgent->pos, 12, 3.0f, V4(1.4f, 1.2f, 0.7f, 1.0f), V4(0.4f, 0.3f, 0.2f, 0.0f), 0.2f, 0.5f, PF_ADD);
                            Combat_KillAgent(tAgent, DMG_ENEMY);
                            a->hp = fminf(a->maxHp, a->hp + 8.0f);
                            a->target = REF_NONE;
                            a->state = AI_PATROL;
                        }
                    }
                } break;
                case AK_NK:
                    if (tAgent && tAgent->kind == AK_HOST) {
                        /* cleansing an infected cell: perforin + granzyme induce apoptosis */
                        float touch = a->radius + tAgent->radius + 0.4f;
                        if (!(tAgent->flags & AF_INFECTED)) { a->target = REF_NONE; a->state = AI_PATROL; break; }
                        if (dist > touch) { desired = AI_Seek(a, tp, spd); accel = 3.0f; }
                        else {
                            desired = (Vector3){ 0 };
                            accel = 6.0f;
                            float cleanse = 4.5f * (G.stats.latentInfection ? 2.0f : 1.0f);
                            tAgent->progress += dt / cleanse;
                            tAgent->timer2 = 0.3f;
                            if (GetRandomValue(0, 100) < 35) {
                                Vector3 from = Vector3Add(a->pos, Vector3Scale(Vector3Normalize(Vector3Subtract(tp, a->pos)), a->radius));
                                Particles_Emit(from, Vector3Scale(Vector3Subtract(tp, from), 2.0f), V4(2.2f, 0.4f, 0.4f, 1.0f), V4(0.8f, 0.1f, 0.1f, 0.0f), 0.14f, 0.04f, 0.4f, 0.5f, PF_ADD);
                            }
                            if (tAgent->progress >= 1.0f) {
                                Infection_Cleanse(tAgent);
                                a->target = REF_NONE;
                                a->state = AI_PATROL;
                            }
                        }
                        break;
                    }
                    if (a->state == AI_CHASE) {
                        desired = AI_Seek(a, lead, spd);
                        if (dist < 17.0f && a->cooldown <= 0.0f && Cave_LineOfSight(a->pos, tp)) {
                            a->state = AI_WINDUP;
                            a->timer = 0.6f;
                            Sfx_Play3D(SFX_NK_CHARGE, a->pos, 0.8f);
                        }
                    } else if (a->state == AI_WINDUP) {
                        a->timer -= dt;
                        desired = (Vector3){ 0 };
                        accel = 6.0f;
                        a->flash = 0.4f + 0.4f * sinf(a->timer * 40.0f);
                        a->dir = Vector3Normalize(Vector3Subtract(lead, a->pos));
                        if (GetRandomValue(0, 100) < 50) Particles_Emit(Vector3Add(a->pos, Vector3Scale(Rng_UnitVecG(), a->radius * 1.6f)), Vector3Scale(Rng_UnitVecG(), -2.0f),
                                                                       V4(2.5f, 0.3f, 0.3f, 0.0f), V4(2.0f, 0.2f, 0.2f, 1.0f), 0.2f, 0.05f, 0.3f, 0.0f, PF_ADD);
                        if (a->timer <= 0.0f) {
                            a->state = AI_ATTACK;
                            a->timer = 0.45f;
                            a->vel = Vector3Scale(a->dir, 42.0f * a->speedMul);
                        }
                    } else {
                        a->timer -= dt;
                        desired = a->vel;
                        accel = 0.3f;
                        if (GetRandomValue(0, 100) < 70) Particles_EmitStretch(a->pos, Vector3Scale(a->vel, -0.2f), V4(2.0f, 0.4f, 0.4f, 0.8f), V4(0.5f, 0.1f, 0.1f, 0.0f), 0.4f, 0.3f, 0.05f, PF_ADD);
                        MeleeContact(a, tp, tr, dmg, 0.5f);
                        if (a->timer <= 0.0f) { a->state = AI_CHASE; a->cooldown = 2.8f; }
                    }
                    break;
                case AK_TCELL: {
                    float want = 16.0f;
                    if (dist > want + 6.0f) desired = AI_Seek(a, lead, spd);
                    else if (dist < want - 5.0f) desired = AI_Flee(a, tp, spd * 0.9f);
                    else {
                        Vector3 to = Vector3Normalize(Vector3Subtract(tp, a->pos));
                        Vector3 side = Vector3Normalize(Vector3CrossProduct(to, (Vector3){ 0, 1, 0 }));
                        desired = Vector3Scale(side, spd * 0.5f * (a->variant & 1 ? 1.0f : -1.0f));
                    }
                    if (a->cooldown <= 0.0f && dist < 34.0f && Cave_LineOfSight(a->pos, tp)) {
                        a->cooldown = 1.7f + Rng_FloatG() * 0.5f;
                        float sp = 30.0f;
                        float t = dist / sp;
                        Vector3 aim = Vector3Add(tp, Vector3Scale(tv, t * 0.75f));
                        Vector3 dir = Vector3Normalize(Vector3Subtract(aim, a->pos));
                        Proj_Spawn(PT_PERFORIN, Vector3Add(a->pos, Vector3Scale(dir, a->radius + 0.4f)), Vector3Scale(dir, sp), dmg, false);
                        Sfx_Play3D(SFX_PERFORIN, a->pos, 0.6f);
                        a->flash = 0.5f;
                        a->vel = Vector3Subtract(a->vel, Vector3Scale(dir, 3.0f));
                    }
                } break;
                case AK_BCELL: {
                    if (!a->targetPlayer) { a->state = AI_PATROL; break; }
                    if (dist < 15.0f) desired = AI_Flee(a, tp, spd);
                    else if (dist > 30.0f) desired = AI_Seek(a, tp, spd * 0.7f);
                    else desired = Vector3Scale(a->vel, 0.5f);
                    if (a->cooldown <= 0.0f && dist < 45.0f) {
                        a->cooldown = 3.6f;
                        int n = 2 + (G.region.alert > 60.0f ? 1 : 0) + (a->flags & AF_ELITE ? 2 : 0);
                        for (int k = 0; k < n; k++) {
                            int ab = Enemies_Spawn(AK_ANTIBODY, Vector3Add(a->pos, Vector3Scale(Rng_UnitVecG(), a->radius + 0.4f)), false);
                            if (ab >= 0) {
                                Agent *b = &G.agents[ab];
                                b->targetPlayer = true;
                                b->state = AI_CHASE;
                                b->group = GROUP_SUMMON;
                                b->vel = Vector3Add(Vector3Scale(Vector3Normalize(Vector3Subtract(tp, a->pos)), 8.0f), Vector3Scale(Rng_UnitVecG(), 6.0f));
                            }
                        }
                        Sfx_Play3D(SFX_ANTIBODY, a->pos, 0.7f);
                        a->flash = 0.6f;
                    }
                } break;
                default: break;
            }
        } break;
        default: break;
    }

    desired = Vector3Add(desired, Separation(a, dt));
    desired = Vector3Add(desired, AI_AvoidWalls(a, 3.0f + a->radius));
    float flowF = (a->state == AI_ATTACK) ? 0.0f : 0.25f;
    AI_Integrate(a, desired, accel, flowF, dt);

    /* big cells shove the player around */
    if (!p->dead && p->engulfedBy.idx < 0) {
        Vector3 dv = Vector3Subtract(p->pos, a->pos);
        float rr = a->radius + p->radius;
        float l2 = Vector3LengthSqr(dv);
        if (l2 < rr * rr && l2 > 1e-6f) {
            float l = sqrtf(l2);
            Vector3 n = Vector3Scale(dv, 1.0f / l);
            float wA = a->radius * a->radius, wP = p->radius * p->radius;
            float push = (rr - l);
            p->pos = Vector3Add(p->pos, Vector3Scale(n, push * wA / (wA + wP)));
            a->pos = Vector3Subtract(a->pos, Vector3Scale(n, push * wP / (wA + wP)));
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Director                                                                   */
/* ------------------------------------------------------------------------- */

static AgentKind PickReinforcement(void)
{
    const RegionDef *d = Region_Def();
    float w[5];
    for (int i = 0; i < 5; i++) w[i] = d->weights[i];
    w[1] += 4.0f * Immune_Resist(IM_SWARM);                         /* macrophages vs swarms   */
    w[2] += 3.0f * Immune_Resist(IM_STEALTH) + 2.0f * G.region.infection; /* NK vs stealth/spread */
    w[3] += 2.0f * Immune_Resist(IM_DASH);
    w[4] += 1.5f * Immune_Resist(IM_TOXIN);
    if (G.player.stage < 1) w[4] *= 0.4f;
    float total = 0.0f;
    for (int i = 0; i < 5; i++) total += w[i];
    float x = Rng_RangeG(0.0f, total);
    for (int i = 0; i < 5; i++) {
        if (x < w[i]) return (AgentKind)(AK_NEUTROPHIL + i);
        x -= w[i];
    }
    return AK_NEUTROPHIL;
}

static bool FindSpawnPoint(float minD, float maxD, Vector3 *out)
{
    Player *p = &G.player;
    float ps = Flow_PathS(p->pos);
    for (int t = 0; t < 10; t++) {
        float off = Rng_RangeG(minD, maxD) * (Rng_FloatG() < 0.5f ? -1.0f : 1.0f);
        Vector3 c = Region_PathPoint(Clamp(ps + off, 5.0f, G.region.pathLength), NULL, NULL);
        Vector3 q;
        if (!Cave_RandomOpenPoint(&G.rng, 2.5f, c, 10.0f, &q)) continue;
        float d = Vector3Distance(q, p->pos);
        if (d < minD * 0.7f) continue;
        if (t < 6 && d < 70.0f && Cave_LineOfSight(q, p->pos)) continue;   /* don't pop in on screen */
        *out = q;
        return true;
    }
    return false;
}

static int SpawnGroup(AgentKind k, Vector3 at, int group, bool chase)
{
    int count = (k == AK_NEUTROPHIL) ? Rng_IntG(3, 5) : 1;
    int made = 0;
    for (int i = 0; i < count; i++) {
        Vector3 q;
        if (!Cave_RandomOpenPoint(&G.rng, 2.0f, at, 6.0f, &q)) q = at;
        int e = Enemies_Spawn(k, q, false);
        if (e < 0) continue;
        Agent *a = &G.agents[e];
        a->group = group;
        if (chase) { a->state = AI_CHASE; a->targetPlayer = true; a->flags |= AF_ALERTED; }
        made++;
    }
    return made;
}

static void Outbreak(float dt)
{
    RunState *r = &G.run;
    Player *p = &G.player;
    if (!sWaveActive) {
        r->waveT -= dt;
        if (r->waveT > 0.0f) return;
        r->wave++;
        sWaveActive = true;
        int budget = 5 + r->wave * 3;
        if (budget > 70) budget = 70;
        int spawned = 0;
        for (int g = 0; g < 14 && spawned < budget; g++) {
            Vector3 c;
            if (!FindSpawnPoint(35.0f, 75.0f, &c)) continue;
            spawned += SpawnGroup(PickReinforcement(), c, GROUP_WAVE, true);
        }
        char t[48], s[96];
        snprintf(t, sizeof(t), "WAVE %d", r->wave);
        if (r->wave % 5 == 0) {
            BossType bt = (BossType)(((r->wave / 5) - 1) % BOSS_COUNT);
            Vector3 c;
            if (FindSpawnPoint(40.0f, 70.0f, &c)) Bosses_Spawn(bt, c);
            snprintf(s, sizeof(s), "A guardian joins the immune response");
        } else {
            snprintf(s, sizeof(s), "%d immune cells inbound", spawned);
        }
        HUD_Banner(t, s, (Color){ 255, 120, 100, 255 });
        Sfx_Play(SFX_WARNING, 0.8f, 1.0f);
        G.region.alert = fminf(ALERT_MAX, G.region.alert + 15.0f);
        return;
    }
    int alive = 0;
    for (int i = 0; i < G.agentHigh; i++) {
        Agent *a = &G.agents[i];
        if (a->alive && a->team == TEAM_IMMUNE && a->group == GROUP_WAVE && a->kind != AK_ANTIBODY) {
            alive++;
            /* stragglers drift toward the player so a wave can always be finished */
            if (a->state == AI_PATROL) { a->state = AI_INVESTIGATE; a->goal = p->pos; a->timer = 8.0f; }
        }
    }
    if (G.boss.active) alive++;
    sWaveRemaining = alive;
    if (alive == 0) {
        sWaveActive = false;
        r->waveT = 7.0f;
        int gm = 12 + r->wave * 4;
        Player_GainGM(gm, p->pos);
        Player_GainEP(6.0f + (float)r->wave * 1.5f);
        char s[64];
        snprintf(s, sizeof(s), "+%d GM   -   next wave in 7 s", gm);
        HUD_Banner("WAVE CLEARED", s, (Color){ 140, 255, 180, 255 });
        if (r->wave > G.profile.bestWave) G.profile.bestWave = r->wave;
        if (r->wave >= 10) HUD_Achievement(ACH_OUTBREAK10);
    }
}

void Enemies_Director(float dt)
{
    RegionState *rg = &G.region;
    if (sSeenT > 0.0f) {
        sSeenT -= dt;
        rg->alert = fminf(ALERT_MAX, rg->alert + ALERT_SEEN_RATE * DIFFICULTIES[G.run.difficulty].alertGain * G.stats.alertMul * dt);
        rg->alertDelay = fmaxf(rg->alertDelay, 1.0f);
    }
    if (G.player.dead) return;
    if (G.run.mode == MODE_OUTBREAK) {
        Outbreak(dt);
        /* outbreak still gets complement pressure */
    } else {
        if (rg->portalOpen || (rg->bossSpawned && !rg->bossDefeated)) return;
        /* reinforcements scale with Alert */
        rg->spawnT -= dt;
        if (rg->spawnT <= 0.0f) {
            float a = rg->alert / ALERT_MAX;
            rg->spawnT = Lerp(7.0f, 1.8f, a) / DIFFICULTIES[G.run.difficulty].spawnMul;
            int reinf = 0;
            for (int i = 0; i < G.agentHigh; i++) {
                Agent *e = &G.agents[i];
                if (e->alive && e->team == TEAM_IMMUNE && e->group == GROUP_REINF && e->kind != AK_ANTIBODY) reinf++;
            }
            int want = (int)((a * 22.0f + (float)rg->index * 2.0f + 2.0f * (float)G.run.ngPlus) * DIFFICULTIES[G.run.difficulty].spawnMul);
            if (rg->alert < 12.0f) want = 0;
            if (reinf < want && Enemies_Active() < 220) {
                Vector3 c;
                if (FindSpawnPoint(ENEMY_SPAWN_MIN_D, ENEMY_SPAWN_MAX_D, &c)) {
                    bool chase = rg->alert > 50.0f;
                    SpawnGroup(PickReinforcement(), c, GROUP_REINF, chase);
                }
            }
        }
    }
    /* complement cascade: the membrane attack complex closes in as rings */
    if (rg->alert > 55.0f) {
        sCompT -= dt;
        if (sCompT <= 0.0f) {
            sCompT = Rng_RangeG(9.0f, 14.0f) - (rg->alert - 55.0f) / 9.0f;
            Vector3 c = Vector3Add(G.player.pos, Vector3Scale(Rng_UnitVecG(), Rng_RangeG(4.0f, 10.0f)));
            int h = Hazards_Spawn(HZ_COMPLEMENT, c, 26.0f, 2.8f, 13.0f);
            if (h >= 0) {
                Vector3 ax = Rng_UnitVecG();
                G.hazards[h].dir = ax;
            }
            Sfx_Play(SFX_WARNING, 0.5f, 1.4f);
            HUD_Hint(TUT_COMPLEMENT, "COMPLEMENT CASCADE: protein rings expand from a point. Dash through the ring or get out of its plane.");
        }
    }
}

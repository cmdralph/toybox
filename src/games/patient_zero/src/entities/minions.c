/*
 * minions.c - virions budded from infected cells.
 *
 * Idle virions orbit the pathogen in a loose, breathing formation (each
 * has its own orbit plane derived from its phase), auto-engage immune cells
 * that come near, and obey Hive Call: send everyone at the aimed target or
 * recall them into a tight defensive shell.
 */
#include "entities/minions.h"
#include "entities/player.h"
#include "entities/bosses.h"
#include "systems/ai.h"
#include "systems/combat.h"
#include "systems/immune_memory.h"
#include "systems/spatial_hash.h"
#include "world/sdf_cave.h"
#include "gfx/particles.h"
#include "gfx/camera.h"
#include "audio/sfx.h"
#include "ui/hud.h"
#include "core/pool.h"
#include "core/rng.h"
#include "core/math_utils.h"

int Minions_Count(void) { return G.agentCount[AK_MINION]; }

int Minions_Spawn(Vector3 pos, AgentRef home, bool burst)
{
    int cap = G.stats.minionMax + (burst ? 4 : 0);
    if (Minions_Count() >= cap) return -1;
    int i = Agent_Alloc();
    if (i < 0) return -1;
    Agent *a = &G.agents[i];
    PlayerStats *s = &G.stats;
    a->kind = AK_MINION;
    a->team = TEAM_PLAYER;
    a->pos = a->prev = pos;
    a->owner = home;
    a->radius = (s->minionTier >= 2) ? 0.62f : 0.45f;
    a->hp = a->maxHp = s->minionHp;
    a->phase = Rng_FloatG();
    a->spin = Rng_RangeG(-3.0f, 3.0f);
    a->dir = Rng_UnitVecG();
    a->anchor = Rng_UnitVecG();      /* orbit-plane normal */
    a->vel = Vector3Scale(Rng_UnitVecG(), 6.0f);
    a->state = AI_IDLE;
    a->target = REF_NONE;
    if (s->minionTier >= 2) a->flags |= AF_TIER2;
    if (burst) a->flags |= AF_BURSTSPAWN;
    Agent *h = Agent_Get(home);
    if (h) h->produced++;
    G.agentCount[AK_MINION]++;
    int n = G.agentCount[AK_MINION];
    if (n > G.run.stats.maxMinions) G.run.stats.maxMinions = n;
    if (n >= 30) HUD_Achievement(ACH_HIVEMIND);
    return i;
}

void Minions_OnDeath(Agent *a)
{
    Agent *h = Agent_Get(a->owner);
    if (h && h->produced > 0) h->produced--;
    if (G.agentCount[AK_MINION] > 0) G.agentCount[AK_MINION]--;
}

int Minions_SpawnDecoy(Vector3 pos, Vector3 vel)
{
    int i = Agent_Alloc();
    if (i < 0) return -1;
    Agent *a = &G.agents[i];
    a->kind = AK_DECOY;
    a->team = TEAM_PLAYER;
    a->pos = a->prev = pos;
    a->vel = vel;
    a->radius = G.player.radius * 0.9f;
    a->hp = a->maxHp = 25.0f;
    a->timer = 6.0f;
    a->phase = Rng_FloatG();
    a->dir = G.player.forward;
    G.agentCount[AK_DECOY]++;
    return i;
}

void Minions_UpdateDecoy(Agent *a, float dt)
{
    a->timer -= dt;
    a->spin += dt * 2.0f;
    if (a->timer <= 0.0f) {
        Particles_Burst(a->pos, 20, 5.0f, V4(0.6f, 2.0f, 1.4f, 1.0f), V4(0.1f, 0.4f, 0.3f, 0.0f), 0.25f, 0.5f, PF_ADD);
        Agent_Free((int)(a - G.agents));
        return;
    }
    Vector3 drift = Vector3Scale(a->vel, 0.6f);
    AI_Integrate(a, drift, 1.0f, 0.6f, dt);
}

static bool TargetPos(Agent *a, Vector3 *pos, float *r)
{
    if (a->target.idx == MINION_BOSS_TARGET) {
        if (!G.boss.active || G.boss.dying) return false;
        *pos = G.boss.pos;
        *r = G.boss.radius;
        return true;
    }
    Agent *t = Agent_Get(a->target);
    if (!t || (t->team != TEAM_IMMUNE && !(t->kind == AK_BACTERIA && !(t->flags & AF_ALLY)))) return false;
    *pos = t->pos;
    *r = t->radius;
    return true;
}

void Minions_Update(Agent *a, float dt)
{
    Player *p = &G.player;
    PlayerStats *s = &G.stats;
    a->cooldown -= dt;
    a->spin += dt * 3.0f;

    if (p->dead) {
        /* without the pathogen's direction the swarm disperses */
        AI_Integrate(a, Vector3Scale(a->dir, 2.0f), 1.0f, 0.8f, dt);
        return;
    }

    float speed = s->minionSpeed * ((a->flags & AF_TIER2) ? 1.1f : 1.0f);
    float dmg = s->minionDamage * ((a->flags & AF_TIER2) ? 1.2f : 1.0f);
    Vector3 tp;
    float tr = 0.0f;
    bool has = TargetPos(a, &tp, &tr);
    if (!has) {
        if (a->flags & AF_CMD) a->flags &= (uint16_t)~AF_CMD;
        a->target = REF_NONE;
    }

    /* auto-engage threats near the pathogen */
    a->perceptT -= dt;
    if (!has && a->perceptT <= 0.0f) {
        a->perceptT = 0.3f + Rng_FloatG() * 0.15f;
        int t = SH_Nearest(a->pos, 15.0f, AGENT_MASK_IMMUNE_CELLS | KMASK(AK_ANTIBODY) | KMASK(AK_BOSS_PART), -1);
        if (t >= 0 && Vector3Distance(G.agents[t].pos, p->pos) < 22.0f) {
            a->target = Agent_Ref(t);
            has = TargetPos(a, &tp, &tr);
        } else if (G.boss.active && !G.boss.dying && G.boss.introDone && Vector3Distance(G.boss.pos, p->pos) < G.boss.radius + 26.0f) {
            a->target = (AgentRef){ MINION_BOSS_TARGET, 0 };
            has = TargetPos(a, &tp, &tr);
        }
    }

    Vector3 desired;
    float accel = 5.0f;
    float distP = Vector3Distance(a->pos, p->pos);
    if (has && (distP < 45.0f || (a->flags & AF_CMD))) {
        /* attack: swirl around the target and strike on contact */
        Vector3 to = Vector3Subtract(tp, a->pos);
        float d = Vector3Length(to);
        Vector3 orbit = Vector3CrossProduct(to, a->anchor);
        if (Vector3Length(orbit) > 0.01f) orbit = Vector3Scale(Vector3Normalize(orbit), speed * 0.35f);
        desired = Vector3Add(Vector3Scale(to, speed * 1.2f / fmaxf(d, 0.01f)), orbit);
        accel = 6.0f;
        float hitR = a->radius + tr + 0.25f;
        bool hit = d < hitR;
        Vector3 bossHit;
        if (a->target.idx == MINION_BOSS_TARGET) hit = Bosses_HitTest(a->pos, a->radius + 0.3f, &bossHit);
        if (hit && a->cooldown <= 0.0f) {
            a->cooldown = 0.55f;
            if (a->target.idx == MINION_BOSS_TARGET) Combat_DamageBoss(dmg, DMG_MINION, a->pos);
            else {
                Agent *t = Agent_Get(a->target);
                if (t) Combat_DamageAgent(t, dmg, DMG_MINION, to);
            }
            if (s->hiveLink) Player_Heal(0.5f);
            a->vel = Vector3Scale(a->vel, -0.6f);
            Particles_Burst(a->pos, 4, 3.0f, V4(0.4f, 2.2f, 1.6f, 1.0f), V4(0.1f, 0.5f, 0.4f, 0.0f), 0.1f, 0.25f, PF_ADD);
        }
    } else {
        /* formation: each virion rides its own tilted orbit around the pathogen */
        float R = p->radius * 2.4f + 2.2f + (float)((int)(a->phase * 97.0f) % 3) * 1.1f;
        if (a->flags & AF_CMD) R *= 0.6f;
        float ang = a->phase * 6.2831f + G.simTime * (0.6f + 0.2f * a->phase);
        Vector3 n = a->anchor;
        Vector3 u = MU_AnyPerp(n);
        Vector3 v = Vector3CrossProduct(n, u);
        Vector3 off = Vector3Add(Vector3Scale(u, cosf(ang) * R), Vector3Scale(v, sinf(ang) * R * 0.6f));
        Vector3 slot = Vector3Add(Vector3Add(p->pos, off), Vector3Scale(p->vel, 0.25f));
        if (Cave_Sample(slot) < a->radius + 0.5f) slot = Vector3Add(p->pos, Vector3Scale(off, 0.4f));
        float catchup = 1.0f + Sat((distP - 10.0f) / 20.0f) * 1.5f;
        desired = AI_Arrive(a, slot, speed * 1.3f * catchup, 4.0f);
        accel = 4.0f;
        if (distP > 120.0f) {
            /* lost virions re-home to the swarm */
            Vector3 q;
            if (Cave_RandomOpenPoint(&G.rng, 1.0f, p->pos, 8.0f, &q)) a->pos = a->prev = q;
        }
    }
    desired = Vector3Add(desired, AI_Flock(a, KMASK(AK_MINION), 1.8f, 0.8f, 0.0f, 0.0f));
    desired = Vector3Add(desired, AI_AvoidWalls(a, 1.5f));
    AI_Integrate(a, desired, accel, 0.1f, dt);

    if (GetRandomValue(0, 1000) < 30)
        Particles_Emit(a->pos, Vector3Scale(a->vel, -0.1f), V4(0.3f, 1.4f, 0.9f, 0.6f), V4(0.1f, 0.4f, 0.3f, 0.0f), 0.1f, 0.02f, 0.5f, 1.0f, PF_ADD);
}

void Minions_Command(void)
{
    Player *p = &G.player;
    int n = Minions_Count();
    if (n == 0) {
        HUD_FloatText("No virions yet - infected cells build them over time", (Color){ 200, 200, 200, 255 });
        return;
    }
    AgentRef tgt = REF_NONE;
    bool boss = false;
    Agent *aim = Agent_Get(p->aimAgent);
    if (aim && (aim->team == TEAM_IMMUNE || (aim->kind == AK_BACTERIA && !(aim->flags & AF_ALLY)))) tgt = p->aimAgent;
    else if (p->aimBoss) boss = true;

    for (int i = 0; i < G.agentHigh; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive || a->kind != AK_MINION) continue;
        a->flags |= AF_CMD;
        if (boss) a->target = (AgentRef){ MINION_BOSS_TARGET, 0 };
        else a->target = tgt;
        if (tgt.idx >= 0 || boss) {
            Vector3 tp = boss ? G.boss.pos : G.agents[tgt.idx].pos;
            a->vel = Vector3Add(a->vel, Vector3Scale(Vector3Normalize(Vector3Subtract(tp, a->pos)), G.stats.minionSpeed));
        }
    }
    if (tgt.idx >= 0 || boss) {
        Vector3 tp = boss ? G.boss.pos : G.agents[tgt.idx].pos;
        Particles_Ring(tp, Vector3Subtract(tp, G.rig.cam.position), 40, 10.0f, V4(0.4f, 2.4f, 1.6f, 1.0f), V4(0.1f, 0.6f, 0.4f, 0.0f), 0.25f, 0.5f);
        Immune_Use(IM_SWARM, 0.6f + 0.06f * (float)n);
    } else {
        Particles_Ring(p->pos, Cam_Up(), 50, 14.0f, V4(0.4f, 2.0f, 1.8f, 1.0f), V4(0.1f, 0.5f, 0.4f, 0.0f), 0.25f, 0.5f);
        HUD_FloatText("Swarm recalled", (Color){ 140, 255, 210, 255 });
    }
    Sfx_Play(SFX_SWARM, 1.0f, 1.0f);
    Cam_Kick(2.0f);
}

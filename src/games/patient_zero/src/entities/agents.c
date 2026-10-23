/*
 * agents.c - the per-tick agent loop and ambient life.
 *
 * Drifters (red cells, surfactant vesicles, chyme, lymph globules,
 * synaptic vesicles) ride the flow field and tumble; the ones that drift
 * far from the player are recycled near them so the visible fluid always
 * stays dense without simulating an entire bloodstream.
 * Gut bacteria crawl along the walls and can become allies (Symbiosis).
 */
#include "entities/agents.h"
#include "entities/hostcells.h"
#include "entities/enemies.h"
#include "entities/minions.h"
#include "entities/bosses.h"
#include "entities/player.h"
#include "systems/combat.h"
#include "systems/ai.h"
#include "systems/spatial_hash.h"
#include "world/sdf_cave.h"
#include "world/flow_field.h"
#include "world/region.h"
#include "gfx/particles.h"
#include "core/pool.h"
#include "core/rng.h"
#include "core/math_utils.h"
#include <string.h>

static int sRecycleBudget = 0;

const char *Agents_KindName(int kind)
{
    static const char *n[AK_COUNT] = { "none", "drifter", "platelet", "host", "bacteria", "virion", "decoy", "neutrophil",
                                       "macrophage", "NK cell", "T cell", "B cell", "antibody", "boss part" };
    return (kind >= 0 && kind < AK_COUNT) ? n[kind] : "?";
}

void Agents_Clear(void)
{
    memset(G.agents, 0, sizeof(G.agents));
    G.agentHigh = 0;
    memset(G.agentCount, 0, sizeof(G.agentCount));
    Agent_ResetPool();
}

int Agents_Spawn(AgentKind kind, Vector3 pos)
{
    int i = Agent_Alloc();
    if (i < 0) return -1;
    Agent *a = &G.agents[i];
    const RegionDef *d = Region_Def();
    a->kind = (uint8_t)kind;
    a->team = TEAM_NEUTRAL;
    a->pos = a->prev = pos;
    a->phase = Rng_FloatG();
    a->dir = Rng_UnitVecG();
    a->anchor = Rng_UnitVecG();          /* tumble axis */
    a->spin = Rng_RangeG(-1.2f, 1.2f);
    a->variant = (uint8_t)Rng_IntG(0, 3);
    switch (kind) {
        case AK_DRIFTER:
            a->radius = d->drifterRadius * Rng_RangeG(0.85f, 1.15f);
            a->hp = a->maxHp = 1.0f;
            break;
        case AK_PLATELET:
            a->radius = 0.45f * Rng_RangeG(0.8f, 1.2f);
            a->hp = a->maxHp = 1.0f;
            break;
        case AK_BACTERIA:
            a->radius = 0.85f;
            a->hp = a->maxHp = 20.0f;
            a->anchor = pos;
            a->goal = pos;
            a->speedMul = Rng_RangeG(0.8f, 1.2f);
            a->vision = 14.0f;
            if (G.stats.bacteriaAllies) { a->flags |= AF_ALLY; a->team = TEAM_PLAYER; }
            break;
        default:
            a->radius = 1.0f;
            a->hp = a->maxHp = 1.0f;
            break;
    }
    a->vel = Vector3Scale(Flow_Sample(pos), 0.8f);
    G.agentCount[kind]++;
    return i;
}

/* ------------------------------------------------------------------------- */
/* Ambient drifters                                                           */
/* ------------------------------------------------------------------------- */

static void Recycle(Agent *a)
{
    if (sRecycleBudget <= 0) return;
    sRecycleBudget--;
    Player *p = &G.player;
    Vector3 c = p->pos;
    /* bias new cells upstream so they flow toward the player */
    Vector3 f = Flow_Sample(c);
    if (Vector3Length(f) > 0.5f) c = Vector3Subtract(c, Vector3Scale(Vector3Normalize(f), 40.0f));
    Vector3 np;
    for (int t = 0; t < 4; t++) {
        if (Cave_RandomOpenPoint(&G.rng, a->radius + 1.5f, c, 95.0f, &np) && Vector3Distance(np, p->pos) > 45.0f) {
            a->pos = a->prev = np;
            a->vel = Flow_Sample(np);
            return;
        }
    }
}

static void UpdateDrifter(Agent *a, float dt, bool menu)
{
    Vector3 flow = Flow_Sample(a->pos);
    float t = G.simTime + a->phase * 10.0f;
    Vector3 wob = { sinf(t * 0.7f) * 0.4f, sinf(t * 0.53f + 1.3f) * 0.3f, cosf(t * 0.61f) * 0.4f };
    a->vel = V3Damp(a->vel, Vector3Add(flow, wob), 1.1f, dt);
    a->pos = Vector3Add(a->pos, Vector3Scale(a->vel, dt));
    /* tumble: biconcave discs flip end over end in shear flow */
    a->dir = Vector3Normalize(MU_RotateAround(a->dir, a->anchor, a->spin * dt * (0.4f + 0.08f * Vector3Length(flow))));

    float far = Vector3Distance(a->pos, menu ? G.rig.cam.position : G.player.pos);
    int idx = (int)(a - G.agents);
    if (far < 120.0f || ((G.frame + (uint64_t)idx) & 3) == 0) Cave_Collide(&a->pos, &a->vel, a->radius, 0.4f);
    /* cells in suspension don't interpenetrate: cheap pairwise push near the viewer */
    if (far < 70.0f && ((G.frame + (uint64_t)idx) & 1) == 0) {
        int ids[8];
        int n = SH_Query(a->pos, a->radius * 2.0f, ids, 8, KMASK(AK_DRIFTER) | KMASK(AK_PLATELET));
        for (int k = 0; k < n; k++) {
            Agent *o = &G.agents[ids[k]];
            if (o == a) continue;
            Vector3 d = Vector3Subtract(a->pos, o->pos);
            float l2 = Vector3LengthSqr(d), rr = (a->radius + o->radius) * 0.9f;
            if (l2 < rr * rr && l2 > 1e-6f) {
                float l = sqrtf(l2);
                a->vel = Vector3Add(a->vel, Vector3Scale(d, (rr - l) / l * 6.0f));
            }
        }
    }
    /* stagnant pockets: cells that stop moving get recycled into the stream */
    if (Vector3LengthSqr(a->vel) < 0.36f) a->timer += dt; else a->timer = 0.0f;

    if (!menu && !G.player.dead) {
        Player *p = &G.player;
        Vector3 d = Vector3Subtract(a->pos, p->pos);
        float rr = a->radius + p->radius + 0.25f;
        float l2 = Vector3LengthSqr(d);
        if (l2 < rr * rr && l2 > 1e-6f) {
            float l = sqrtf(l2);
            Vector3 n = Vector3Scale(d, 1.0f / l);
            a->pos = Vector3Add(p->pos, Vector3Scale(n, rr));
            a->vel = Vector3Add(a->vel, Vector3Scale(n, 4.0f + Vector3Length(p->vel) * 0.6f));
            a->spin += 2.0f * (Rng_FloatG() - 0.5f);
        }
        if (far > 150.0f || (a->timer > 6.0f && far > 30.0f)) { a->timer = 0.0f; Recycle(a); }
    } else if (menu && far > 160.0f) {
        Vector3 np;
        if (sRecycleBudget-- > 0 && Cave_RandomOpenPoint(&G.rng, 3.0f, G.rig.cam.position, 100.0f, &np)) a->pos = a->prev = np;
    }
    a->spin = Clamp(a->spin * expf(-0.05f * dt), -3.0f, 3.0f);
}

/* ------------------------------------------------------------------------- */
/* Gut flora                                                                  */
/* ------------------------------------------------------------------------- */

static void UpdateBacteria(Agent *a, float dt)
{
    bool ally = G.stats.bacteriaAllies;
    if (ally && !(a->flags & AF_ALLY)) { a->flags |= AF_ALLY; a->team = TEAM_PLAYER; }
    if (!ally && (a->flags & AF_ALLY)) { a->flags &= (uint16_t)~AF_ALLY; a->team = TEAM_NEUTRAL; }
    a->cooldown -= dt;
    a->timer -= dt;
    Vector3 desired = { 0 };
    float speed = 3.5f * a->speedMul;

    if (ally) {
        a->perceptT -= dt;
        if (a->perceptT <= 0.0f) {
            a->perceptT = 0.4f + Rng_FloatG() * 0.2f;
            int t = SH_Nearest(a->pos, a->vision, AGENT_MASK_IMMUNE_CELLS, -1);
            a->target = Agent_Ref(t);
        }
        Agent *tg = Agent_Get(a->target);
        if (tg) {
            desired = AI_Seek(a, tg->pos, speed * 2.2f);
            if (Vector3Distance(a->pos, tg->pos) < a->radius + tg->radius + 0.4f && a->cooldown <= 0.0f) {
                a->cooldown = 0.5f;
                Combat_DamageAgent(tg, 3.0f + (float)G.player.stage, DMG_MINION, Vector3Subtract(tg->pos, a->pos));
            }
        } else if (!G.player.dead && Vector3Distance(a->pos, G.player.pos) < 30.0f) {
            /* loosely escort the player */
            Vector3 off = { sinf(a->phase * 6.28f + G.simTime * 0.3f) * 6.0f, 1.5f, cosf(a->phase * 6.28f + G.simTime * 0.3f) * 6.0f };
            desired = AI_Arrive(a, Vector3Add(G.player.pos, off), speed * 2.0f, 4.0f);
        }
        if (G.stats.synSymbiotic) {
            if (!G.player.dead && Vector3Distance(a->pos, G.player.pos) < 8.0f) G.player.atp = fminf(G.stats.maxAtp, G.player.atp + 1.2f * dt);
            a->timer2 += dt;
            if (a->timer2 > 25.0f && G.agentCount[AK_BACTERIA] < 220) {
                a->timer2 = Rng_RangeG(0.0f, 6.0f);
                int c = Agents_Spawn(AK_BACTERIA, Vector3Add(a->pos, Vector3Scale(Rng_UnitVecG(), 1.2f)));
                if (c >= 0) Particles_Burst(a->pos, 8, 3.0f, V4(0.6f, 1.6f, 0.5f, 1.0f), V4(0.2f, 0.6f, 0.2f, 0.0f), 0.15f, 0.5f, PF_ADD);
            }
        }
    }
    if (Vector3LengthSqr(desired) < 0.01f) {
        if (a->timer <= 0.0f || Vector3Distance(a->pos, a->goal) < 1.5f) {
            a->goal = AI_WanderGoal(a, 9.0f);
            a->timer = Rng_RangeG(3.0f, 7.0f);
        }
        desired = AI_Arrive(a, a->goal, speed, 3.0f);
    }
    desired = Vector3Add(desired, AI_AvoidWalls(a, 2.0f));
    desired = Vector3Add(desired, AI_Flock(a, KMASK(AK_BACTERIA), 3.0f, 1.0f, 0.0f, 0.0f));
    AI_Integrate(a, desired, 3.0f, 0.2f, dt);
    a->spin += dt * (1.5f + Vector3Length(a->vel) * 0.4f);   /* flagellar rotation */
}

/* ------------------------------------------------------------------------- */
/* Main loop                                                                  */
/* ------------------------------------------------------------------------- */

void Agents_Update(float dt)
{
    bool menu = (G.state == GS_MENU || G.state == GS_NEWGAME || G.state == GS_CREDITS || G.state == GS_SETTINGS || G.state == GS_BESTIARY) && !G.run.active;
    sRecycleBudget = 40;
    memset(G.agentCount, 0, sizeof(G.agentCount));
    for (int i = 0; i < G.agentHigh; i++) if (G.agents[i].alive) G.agentCount[G.agents[i].kind]++;

    int high = G.agentHigh;
    for (int i = 0; i < high; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive) continue;
        if (a->kind != AK_HOST) a->prev = a->pos;
        a->flash = fmaxf(0.0f, a->flash - dt * 5.0f);

        if (a->poisonT > 0.0f && a->team != TEAM_NEUTRAL) {
            a->poisonT -= dt;
            Combat_DamageAgent(a, a->poisonDps * dt, DMG_POISON, (Vector3){ 0 });
            if (!a->alive) continue;
            if (GetRandomValue(0, 100) < 12) Particles_Emit(Vector3Add(a->pos, Vector3Scale(Rng_UnitVecG(), a->radius)), (Vector3){ 0, 0.8f, 0 },
                                                           V4(0.5f, 1.5f, 0.3f, 0.8f), V4(0.2f, 0.6f, 0.1f, 0.0f), 0.18f, 0.05f, 0.7f, 1.0f, PF_ADD);
        }
        if (menu && a->kind != AK_DRIFTER && a->kind != AK_PLATELET && a->kind != AK_HOST && a->kind != AK_BACTERIA) continue;

        switch (a->kind) {
            case AK_DRIFTER:
            case AK_PLATELET: UpdateDrifter(a, dt, menu); break;
            case AK_HOST: Hosts_Update(a, dt); break;
            case AK_BACTERIA: if (menu) UpdateDrifter(a, dt * 0.2f, true); else UpdateBacteria(a, dt); break;
            case AK_MINION: Minions_Update(a, dt); break;
            case AK_DECOY: Minions_UpdateDecoy(a, dt); break;
            case AK_NEUTROPHIL: case AK_MACROPHAGE: case AK_NK: case AK_TCELL: case AK_BCELL: case AK_ANTIBODY:
                Enemies_Update(a, dt);
                break;
            case AK_BOSS_PART: Bosses_UpdatePart(a, dt); break;
            default: break;
        }
    }
}

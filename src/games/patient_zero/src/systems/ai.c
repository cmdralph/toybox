/*
 * ai.c - reusable steering. Immune cells "crawl" against the current
 * (flowFactor < 1), drifters ride it (flowFactor = 1).
 */
#include "systems/ai.h"
#include "systems/spatial_hash.h"
#include "systems/immune_memory.h"
#include "world/sdf_cave.h"
#include "world/flow_field.h"
#include "world/hazards.h"
#include "core/rng.h"

Vector3 AI_Seek(const Agent *a, Vector3 target, float speed)
{
    Vector3 d = Vector3Subtract(target, a->pos);
    float l = Vector3Length(d);
    if (l < 1e-4f) return (Vector3){ 0 };
    return Vector3Scale(d, speed / l);
}

Vector3 AI_Arrive(const Agent *a, Vector3 target, float speed, float slowRadius)
{
    Vector3 d = Vector3Subtract(target, a->pos);
    float l = Vector3Length(d);
    if (l < 1e-4f) return (Vector3){ 0 };
    float s = speed * Sat(l / slowRadius);
    return Vector3Scale(d, s / l);
}

Vector3 AI_Flee(const Agent *a, Vector3 from, float speed)
{
    return Vector3Negate(AI_Seek(a, from, speed));
}

Vector3 AI_Flock(const Agent *a, uint32_t mask, float radius, float sepW, float aliW, float cohW)
{
    int ids[24];
    int n = SH_Query(a->pos, radius, ids, 24, mask);
    Vector3 sep = { 0 }, ali = { 0 }, coh = { 0 };
    int cnt = 0;
    for (int k = 0; k < n; k++) {
        Agent *o = &G.agents[ids[k]];
        if (o == a) continue;
        Vector3 d = Vector3Subtract(a->pos, o->pos);
        float l = Vector3Length(d);
        float minD = a->radius + o->radius + 0.6f;
        if (l < minD && l > 1e-4f) sep = Vector3Add(sep, Vector3Scale(d, (minD - l) / (l * minD)));
        ali = Vector3Add(ali, o->vel);
        coh = Vector3Add(coh, o->pos);
        cnt++;
    }
    if (cnt == 0) return (Vector3){ 0 };
    ali = Vector3Scale(ali, 1.0f / cnt);
    coh = Vector3Subtract(Vector3Scale(coh, 1.0f / cnt), a->pos);
    return Vector3Add(Vector3Add(Vector3Scale(sep, sepW * 10.0f), Vector3Scale(Vector3Subtract(ali, a->vel), aliW)), Vector3Scale(coh, cohW));
}

Vector3 AI_AvoidWalls(const Agent *a, float lookAhead)
{
    Vector3 v = a->vel;
    float sp = Vector3Length(v);
    Vector3 dir = sp > 0.1f ? Vector3Scale(v, 1.0f / sp) : a->dir;
    Vector3 probe = Vector3Add(a->pos, Vector3Scale(dir, lookAhead));
    Vector3 g;
    float o = Cave_SampleGrad(probe, &g);
    float want = a->radius + 2.5f;
    if (o >= want) return (Vector3){ 0 };
    return Vector3Scale(g, (want - o) * 4.0f);
}

void AI_Integrate(Agent *a, Vector3 desired, float accel, float flowFactor, float dt)
{
    a->vel = Vector3Lerp(a->vel, desired, 1.0f - expf(-accel * dt));
    if (flowFactor > 0.0f) a->vel = Vector3Add(a->vel, Vector3Scale(Flow_Sample(a->pos), flowFactor * dt * 2.0f));
    a->prev = a->pos;
    a->pos = Vector3Add(a->pos, Vector3Scale(a->vel, dt));
    Cave_Collide(&a->pos, &a->vel, a->radius, 0.2f);
    Hazards_Collide(&a->pos, &a->vel, a->radius);
    float sp = Vector3Length(a->vel);
    if (sp > 0.3f) a->dir = Vector3Normalize(Vector3Lerp(a->dir, Vector3Scale(a->vel, 1.0f / sp), 1.0f - expf(-6.0f * dt)));
}

bool AI_CanSeePlayer(Agent *a)
{
    Player *p = &G.player;
    if (p->dead) return false;
    float vis = a->vision * G.stats.detectMul * (1.0f + 0.8f * Immune_Resist(IM_STEALTH));
    vis *= (1.0f + 0.3f * (float)p->stage * 0.25f);       /* bigger pathogens are easier to spot */
    if (p->cloaked) vis *= 0.12f;
    if (p->attach.idx >= 0) vis *= 1.15f;
    Vector3 to = Vector3Subtract(p->pos, a->pos);
    float d = Vector3Length(to);
    if (d > vis) return false;
    if (d > 7.0f && Vector3DotProduct(Vector3Scale(to, 1.0f / d), a->dir) < -0.25f) return false;
    return Cave_LineOfSight(a->pos, p->pos);
}

void AI_AlertGroup(Vector3 pos, float radius, Vector3 target)
{
    int ids[64];
    int n = SH_Query(pos, radius, ids, 64, AGENT_MASK_IMMUNE_CELLS);
    for (int k = 0; k < n; k++) {
        Agent *o = &G.agents[ids[k]];
        if (o->state == AI_PATROL || o->state == AI_IDLE) {
            o->state = AI_INVESTIGATE;
            o->goal = target;
            o->timer = 6.0f;
        }
        o->flags |= AF_ALERTED;
    }
}

Vector3 AI_WanderGoal(Agent *a, float range)
{
    Vector3 p;
    if (Cave_RandomOpenPoint(&G.rng, a->radius + 2.0f, a->anchor, range, &p)) return p;
    return a->anchor;
}

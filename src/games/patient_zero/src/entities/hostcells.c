/*
 * hostcells.c - host cells are the economy of the game. Each is anchored
 * to the tissue wall by its basal side (like a real epithelium) and, once
 * infected, becomes a factory that buds new virions from its surface.
 */
#include "entities/hostcells.h"
#include "entities/minions.h"
#include "entities/player.h"
#include "gfx/particles.h"
#include "audio/sfx.h"
#include "core/pool.h"
#include "core/rng.h"

int Hosts_Spawn(Vector3 pos, Vector3 normal, float radius)
{
    int i = Agent_Alloc();
    if (i < 0) return -1;
    Agent *a = &G.agents[i];
    a->kind = AK_HOST;
    a->team = TEAM_NEUTRAL;
    a->flags = AF_ATTACHED;
    a->pos = a->prev = pos;
    a->anchor = Vector3Normalize(normal);
    a->dir = a->anchor;
    a->radius = radius;
    a->hp = a->maxHp = 100.0f;
    a->phase = Rng_FloatG();
    a->variant = (uint8_t)Rng_IntG(0, 3);
    a->spin = Rng_RangeG(0.0f, 6.2831f);
    a->target = REF_NONE;
    a->owner = REF_NONE;
    G.agentCount[AK_HOST]++;
    return i;
}

void Hosts_Update(Agent *a, float dt)
{
    Player *p = &G.player;
    bool beingInjected = (p->attach.idx == (int)(a - G.agents));

    if (!(a->flags & AF_INFECTED)) {
        if (!beingInjected && a->progress > 0.0f) a->progress = fmaxf(0.0f, a->progress - 0.12f * dt);
        return;
    }

    /* cleanse progress decays unless an NK cell keeps working on it (NK sets timer2 each tick) */
    a->timer2 -= dt;
    if (a->timer2 <= 0.0f && a->progress > 0.0f) a->progress = fmaxf(0.0f, a->progress - 0.08f * dt);

    /* virion budding */
    if (p->stage >= 1 && !p->dead) {
        a->produce -= dt;
        if (a->produce <= 0.0f) {
            a->produce = G.stats.virionInterval * Rng_RangeG(0.8f, 1.2f);
            if (a->produced < G.stats.hostCapacity && Minions_Count() < G.stats.minionMax) {
                Vector3 bud = Vector3Add(a->pos, Vector3Scale(Vector3Normalize(Vector3Add(a->anchor, Vector3Scale(Rng_UnitVecG(), 0.6f))), a->radius));
                if (Minions_Spawn(bud, Agent_Ref((int)(a - G.agents)), false) >= 0) {
                    Particles_Burst(bud, 10, 4.0f, V4(0.5f, 2.2f, 1.2f, 1.0f), V4(0.1f, 0.6f, 0.3f, 0.0f), 0.18f, 0.45f, PF_ADD);
                    if (Vector3Distance(bud, p->pos) < 40.0f) Sfx_Play3D(SFX_SPAWN_VIRION, bud, 0.5f);
                }
            }
        }
    }

    /* Metabolic Siphon: nearby factories feed the pathogen */
    if (G.stats.hostDrip && !p->dead) {
        float d = Vector3Distance(a->pos, p->pos);
        if (d < 18.0f) {
            p->atp = fminf(G.stats.maxAtp, p->atp + 2.5f * dt);
            a->timer += dt;
            if (a->timer > 5.0f) { a->timer = 0.0f; Player_GainGM(1, a->pos); }
            if (GetRandomValue(0, 100) < 8) {
                Vector3 from = Vector3Add(a->pos, Vector3Scale(a->anchor, a->radius));
                Particles_Emit(from, Vector3Scale(Vector3Subtract(p->pos, from), 1.4f), V4(1.4f, 1.3f, 0.3f, 0.9f), V4(0.6f, 1.2f, 0.3f, 0.0f), 0.12f, 0.04f, 0.7f, 0.0f, PF_ADD);
            }
        }
    }

    /* spores of infection drifting off the membrane */
    if (GetRandomValue(0, 1000) < 25) {
        Vector3 s = Vector3Add(a->pos, Vector3Scale(Vector3Normalize(Vector3Add(a->anchor, Rng_UnitVecG())), a->radius));
        Particles_Emit(s, Vector3Scale(a->anchor, 0.6f), V4(0.3f, 1.4f, 0.7f, 0.0f), V4(0.2f, 0.9f, 0.5f, 0.7f), 0.14f, 0.05f, 2.5f, 0.6f, PF_ADD | PF_FLOW);
    }
}

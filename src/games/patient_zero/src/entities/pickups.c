/*
 * pickups.c - genetic material and nutrients. Small orbs are pulled in by
 * a magnetic radius that grows with the pathogen; secret caches and
 * mutagenic pods sit at the ends of side branches and must be touched.
 */
#include "entities/pickups.h"
#include "entities/player.h"
#include "world/sdf_cave.h"
#include "world/flow_field.h"
#include "gfx/particles.h"
#include "gfx/lights.h"
#include "audio/sfx.h"
#include "ui/hud.h"
#include "core/pool.h"
#include "core/rng.h"
#include <stdio.h>
#include <string.h>

static int sCursor = 0;

int Pickups_Spawn(PickupType type, Vector3 pos, float value)
{
    int i = Pool_Alloc(G.pickups, sizeof(Pickup), offsetof(Pickup, alive), MAX_PICKUPS, &sCursor);
    if (i < 0) return -1;
    Pickup *p = &G.pickups[i];
    memset(p, 0, sizeof(*p));
    p->alive = true;
    p->type = (uint8_t)type;
    p->pos = p->prev = pos;
    p->value = value;
    p->phase = Rng_FloatG() * 6.2831f;
    p->life = (type == PK_CACHE || type == PK_POD) ? -1.0f : 30.0f;
    return i;
}

void Pickups_Clear(void) { memset(G.pickups, 0, sizeof(G.pickups)); }
int Pickups_Count(void) { return Pool_CountAlive(G.pickups, sizeof(Pickup), offsetof(Pickup, alive), MAX_PICKUPS); }

static void Collect(Pickup *k)
{
    Player *p = &G.player;
    char t[64];
    switch (k->type) {
        case PK_GM:
            Player_GainGM((int)(k->value + 0.5f), k->pos);
            Sfx_PlayThrottled(SFX_PICKUP, 0.5f, 0.04f);
            Particles_Burst(k->pos, 6, 3.0f, V4(1.0f, 2.2f, 2.4f, 1.0f), V4(0.2f, 0.5f, 0.6f, 0.0f), 0.12f, 0.3f, PF_ADD);
            break;
        case PK_ATP:
            p->atp = fminf(G.stats.maxAtp, p->atp + k->value);
            Sfx_PlayThrottled(SFX_PICKUP, 0.4f, 0.04f);
            Particles_Burst(k->pos, 6, 3.0f, V4(2.4f, 2.0f, 0.4f, 1.0f), V4(0.6f, 0.4f, 0.1f, 0.0f), 0.12f, 0.3f, PF_ADD);
            break;
        case PK_BIOMASS:
            Player_Heal(k->value);
            Sfx_PlayThrottled(SFX_PICKUP, 0.4f, 0.04f);
            Particles_Burst(k->pos, 6, 3.0f, V4(2.4f, 0.6f, 0.6f, 1.0f), V4(0.6f, 0.1f, 0.1f, 0.0f), 0.12f, 0.3f, PF_ADD);
            break;
        case PK_CACHE: {
            int gm = (int)k->value;
            Player_GainGM(gm, k->pos);
            snprintf(t, sizeof(t), "+%d Genetic Material", (int)((float)gm * G.stats.gmMul));
            HUD_Toast("GENETIC CACHE", t, (Color){ 120, 230, 255, 255 });
            Sfx_Play(SFX_CACHE, 1.0f, 1.0f);
            Particles_Burst(k->pos, 60, 10.0f, V4(0.8f, 2.4f, 3.0f, 1.0f), V4(0.1f, 0.4f, 0.6f, 0.0f), 0.3f, 1.0f, PF_ADD);
            Lights_AddTemp(k->pos, (Vector3){ 1.0f, 3.0f, 4.0f }, 20.0f, 0.8f);
        } break;
        case PK_POD:
            Player_GainEP(k->value);
            snprintf(t, sizeof(t), "+%d Evolution Points", (int)(k->value * G.stats.epMul));
            HUD_Toast("MUTAGENIC POD", t, (Color){ 255, 180, 255, 255 });
            Sfx_Play(SFX_CACHE, 1.0f, 1.25f);
            Particles_Burst(k->pos, 50, 9.0f, V4(2.6f, 1.0f, 2.8f, 1.0f), V4(0.5f, 0.1f, 0.6f, 0.0f), 0.3f, 1.0f, PF_ADD);
            Lights_AddTemp(k->pos, (Vector3){ 3.0f, 1.0f, 3.5f }, 18.0f, 0.8f);
            break;
        default: break;
    }
    k->alive = false;
}

void Pickups_Update(float dt)
{
    Player *p = &G.player;
    float magnet = 9.0f + p->radius * 4.0f;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *k = &G.pickups[i];
        if (!k->alive) continue;
        k->prev = k->pos;
        k->phase += dt;
        if (k->life > 0.0f) {
            k->life -= dt;
            if (k->life <= 0.0f) { k->alive = false; continue; }
        }
        bool big = (k->type == PK_CACHE || k->type == PK_POD);
        float d = p->dead ? 1e9f : Vector3Distance(k->pos, p->pos);
        if (!big && d < magnet) {
            Vector3 to = Vector3Scale(Vector3Subtract(p->pos, k->pos), 1.0f / fmaxf(d, 0.01f));
            float pull = 40.0f * (1.0f - d / magnet) + 8.0f;
            k->vel = Vector3Add(k->vel, Vector3Scale(to, pull * dt * 3.0f));
        } else if (!big) {
            k->vel = Vector3Lerp(k->vel, Vector3Scale(Flow_Sample(k->pos), 0.5f), 1.0f - expf(-1.0f * dt));
        }
        k->vel = Vector3Scale(k->vel, expf(-1.5f * dt));
        k->pos = Vector3Add(k->pos, Vector3Scale(k->vel, dt));
        if (!big) Cave_Collide(&k->pos, &k->vel, 0.3f, 0.3f);
        if (d < p->radius + (big ? 2.2f : 0.7f)) Collect(k);
        else if (big) {
            Vector3 c = (k->type == PK_CACHE) ? (Vector3){ 0.6f, 2.0f, 2.6f } : (Vector3){ 2.4f, 0.8f, 2.6f };
            if (GetRandomValue(0, 100) < 25)
                Particles_Emit(Vector3Add(k->pos, Vector3Scale(Rng_UnitVecG(), 2.0f)), (Vector3){ 0, 0.8f, 0 }, V4(c.x, c.y, c.z, 0.0f), V4(c.x, c.y, c.z, 0.8f), 0.15f, 0.05f, 1.2f, 0.5f, PF_ADD);
        }
    }
}

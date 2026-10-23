/*
 * hazards.c - simulation of area hazards. Telegraph-then-strike hazards
 * (antibiotics, storms) give the player a readable 2-3 second window.
 */
#include "world/hazards.h"
#include "world/region.h"
#include "systems/combat.h"
#include "gfx/particles.h"
#include "gfx/lights.h"
#include "gfx/camera.h"
#include "audio/sfx.h"
#include "core/pool.h"
#include "core/rng.h"
#include "systems/spatial_hash.h"
#include <string.h>

static int sCursor = 0;
static Rng sR;

void Hazards_Clear(void)
{
    memset(G.hazards, 0, sizeof(G.hazards));
    Rng_Seed(&sR, 0x5151u);
}

int Hazards_Spawn(HazardType type, Vector3 pos, float radius, float duration, float strength)
{
    int i = Pool_Alloc(G.hazards, sizeof(Hazard), offsetof(Hazard, alive), MAX_HAZARDS, &sCursor);
    if (i < 0) return -1;
    Hazard *h = &G.hazards[i];
    memset(h, 0, sizeof(*h));
    h->alive = true;
    h->type = (uint8_t)type;
    h->pos = pos;
    h->radius = radius;
    h->radius2 = radius;
    h->duration = duration;
    h->strength = strength;
    h->phase = Rng_Range(&sR, 0, 6.28f);
    h->dir = (Vector3){ 0, 1, 0 };
    h->hp = 60.0f;
    if (type == HZ_COMPLEMENT || type == HZ_SHOCKWAVE) { h->radius = 1.0f; h->radius2 = radius; }
    if (type == HZ_CLOT) h->hp = 70.0f + 25.0f * (float)G.region.index;
    return i;
}

int Hazards_Count(void) { return Pool_CountAlive(G.hazards, sizeof(Hazard), offsetof(Hazard, alive), MAX_HAZARDS); }

float Hazards_SlowAt(Vector3 pos)
{
    float slow = 0.0f;
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G.hazards[i];
        if (!h->alive || h->type != HZ_MUCUS) continue;
        float d = Vector3Distance(pos, h->pos);
        if (d < h->radius) slow = fmaxf(slow, h->strength * (1.0f - d / h->radius * 0.5f));
    }
    return slow;
}

bool Hazards_Collide(Vector3 *pos, Vector3 *vel, float radius)
{
    bool hit = false;
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G.hazards[i];
        if (!h->alive || h->type != HZ_CLOT) continue;
        float grow = Sat(h->timer / 1.2f);
        float rr = h->radius * grow + radius;
        Vector3 d = Vector3Subtract(*pos, h->pos);
        float l = Vector3Length(d);
        if (l < rr && l > 0.001f) {
            Vector3 n = Vector3Scale(d, 1.0f / l);
            *pos = Vector3Add(h->pos, Vector3Scale(n, rr));
            if (vel) {
                float vn = Vector3DotProduct(*vel, n);
                if (vn < 0) *vel = Vector3Subtract(*vel, Vector3Scale(n, vn * 1.3f));
            }
            hit = true;
        }
    }
    return hit;
}

static bool PlayerInside(Hazard *h, float extra)
{
    return !G.player.dead && Vector3Distance(G.player.pos, h->pos) < h->radius + G.player.radius + extra;
}

static void DamageMinionsIn(Vector3 c, float r, float dmg)
{
    int ids[256];
    int n = SH_Query(c, r, ids, 256, (1u << AK_MINION) | (1u << AK_DECOY));
    for (int k = 0; k < n; k++) Combat_DamageAgent(&G.agents[ids[k]], dmg, DMG_HAZARD, (Vector3){ 0, 1, 0 });
}

void Hazards_Update(float dt)
{
    float dmgMul = Region_EnemyDmgMul();
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G.hazards[i];
        if (!h->alive) continue;
        h->timer += dt;
        if (h->duration > 0.0f && h->timer >= h->duration) { h->alive = false; continue; }
        float t = h->timer;
        switch (h->type) {
            case HZ_ACID_POOL:
                if (PlayerInside(h, 0.0f)) Combat_DamagePlayer(h->strength * dmgMul * dt, h->pos, DMG_HAZARD);
                DamageMinionsIn(h->pos, h->radius, 6.0f * dt);
                if (GetRandomValue(0, 100) < 30) {
                    Vector3 p = Vector3Add(h->pos, Vector3Scale(Rng_InSphere(&sR), h->radius * 0.9f));
                    Particles_Emit(p, (Vector3){ 0, Rng_Range(&sR, 0.5f, 2.0f), 0 }, V4(0.5f, 1.6f, 0.2f, 0.0f), V4(0.3f, 0.8f, 0.1f, 0.6f), 0.25f, 0.05f, 1.6f, 1.0f, PF_ADD);
                }
                break;
            case HZ_MUCUS:
                if (GetRandomValue(0, 100) < 6) {
                    Vector3 p = Vector3Add(h->pos, Vector3Scale(Rng_InSphere(&sR), h->radius));
                    Particles_Emit(p, Vector3Scale(Rng_UnitVec(&sR), 0.3f), V4(0.9f, 0.85f, 0.5f, 0.0f), V4(0.8f, 0.75f, 0.4f, 0.35f), 0.7f, 1.2f, 3.0f, 0.5f, 0);
                }
                break;
            case HZ_CILIA: {
                /* cilia beat in metachronal waves: pulsed push back up the tract */
                float beat = 0.55f + 0.45f * sinf(t * 3.0f + h->phase);
                if (PlayerInside(h, 0.0f)) G.player.vel = Vector3Add(G.player.vel, Vector3Scale(h->dir, h->strength * beat * dt));
            } break;
            case HZ_COMPLEMENT:
            case HZ_SHOCKWAVE: {
                float k = Sat(t / fmaxf(h->duration, 0.1f));
                h->radius = 1.0f + (h->radius2 - 1.0f) * k;
                float band = (h->type == HZ_COMPLEMENT) ? 1.6f : 2.2f;
                float d = Vector3Distance(G.player.pos, h->pos);
                if (!h->triggered && fabsf(d - h->radius) < band + G.player.radius && !G.player.dead) {
                    bool inPlane = true;
                    if (h->type == HZ_COMPLEMENT) {
                        float off = fabsf(Vector3DotProduct(Vector3Subtract(G.player.pos, h->pos), h->dir));
                        inPlane = off < 2.5f + G.player.radius;
                    }
                    if (inPlane) {
                        h->triggered = true;
                        Combat_DamagePlayer(h->strength * dmgMul, h->pos, DMG_HAZARD);
                    }
                }
            } break;
            case HZ_ANTIBIOTIC:
            case HZ_STORM: {
                float warn = (h->type == HZ_ANTIBIOTIC) ? 2.6f : 1.8f;
                if (!h->triggered && t >= warn) {
                    h->triggered = true;
                    h->duration = t + 0.6f;
                    bool storm = h->type == HZ_STORM;
                    Vector3 col = storm ? (Vector3){ 0.6f, 0.8f, 3.0f } : (Vector3){ 3.0f, 1.4f, 0.6f };
                    Lights_AddTemp(h->pos, col, h->radius * 3.0f, 0.5f);
                    Particles_Burst(h->pos, 70, h->radius * 2.2f, V4(col.x, col.y, col.z, 1.0f), V4(col.x * 0.3f, col.y * 0.3f, col.z * 0.3f, 0.0f), 0.5f, 0.7f, PF_ADD);
                    if (storm) {
                        for (int b = 0; b < 4; b++) {
                            Vector3 a = Vector3Add(h->pos, Vector3Scale(Rng_UnitVec(&sR), h->radius * 1.6f));
                            Vector3 dd = Vector3Subtract(h->pos, a);
                            for (int s = 0; s < 10; s++) {
                                Vector3 p = Vector3Add(a, Vector3Scale(dd, (float)s / 10.0f));
                                p = Vector3Add(p, Vector3Scale(Rng_UnitVec(&sR), 0.8f));
                                Particles_EmitStretch(p, Vector3Scale(dd, 0.5f), V4(1.5f, 1.8f, 4.0f, 1.0f), V4(0.3f, 0.4f, 1.5f, 0.0f), 0.18f, 0.25f, 0.12f, PF_ADD);
                            }
                        }
                    }
                    Sfx_Play3D(storm ? SFX_ZAP : SFX_EXPLODE, h->pos, 1.0f);
                    float pd = Vector3Distance(G.player.pos, h->pos);
                    Cam_AddTrauma(Sat(1.0f - pd / (h->radius * 4.0f)) * 0.6f);
                    if (PlayerInside(h, 0.0f)) Combat_DamagePlayer(h->strength * dmgMul, h->pos, DMG_HAZARD);
                    DamageMinionsIn(h->pos, h->radius, 999.0f);
                } else if (!h->triggered && GetRandomValue(0, 100) < 40) {
                    Vector3 p = Vector3Add(h->pos, Vector3Scale(Rng_UnitVec(&sR), h->radius));
                    Vector3 c = (h->type == HZ_STORM) ? (Vector3){ 0.5f, 0.7f, 2.5f } : (Vector3){ 2.5f, 0.6f, 0.3f };
                    Particles_Emit(p, Vector3Scale(Vector3Subtract(h->pos, p), 0.6f), V4(c.x, c.y, c.z, 0.0f), V4(c.x, c.y, c.z, 0.8f), 0.3f, 0.1f, 0.8f, 0.5f, PF_ADD);
                }
            } break;
            case HZ_TOXIN_CLOUD: {
                int ids[128];
                int n = SH_Query(h->pos, h->radius, ids, 128, AGENT_MASK_IMMUNE);
                for (int k = 0; k < n; k++) {
                    Agent *a = &G.agents[ids[k]];
                    a->poisonDps = fmaxf(a->poisonDps, h->strength);
                    a->poisonT = fmaxf(a->poisonT, 1.0f);
                }
                if (G.boss.active && Vector3Distance(G.boss.pos, h->pos) < h->radius + G.boss.radius) Combat_DamageBoss(h->strength * dt, DMG_POISON, G.boss.pos);
                if (GetRandomValue(0, 100) < 50) {
                    Vector3 p = Vector3Add(h->pos, Vector3Scale(Rng_InSphere(&sR), h->radius));
                    Particles_Emit(p, Vector3Scale(Rng_UnitVec(&sR), 0.4f), V4(0.4f, 1.4f, 0.3f, 0.0f), V4(0.2f, 0.7f, 0.1f, 0.4f), 0.9f, 1.6f, 1.5f, 0.6f, PF_ADD);
                }
            } break;
            case HZ_VORTEX: {
                Vector3 d = Vector3Subtract(h->pos, G.player.pos);
                float l = Vector3Length(d);
                if (l < h->radius && l > 0.5f && !G.player.dead) G.player.vel = Vector3Add(G.player.vel, Vector3Scale(d, h->strength * dt / l));
                if (GetRandomValue(0, 100) < 60) {
                    Vector3 p = Vector3Add(h->pos, Vector3Scale(Rng_UnitVec(&sR), h->radius));
                    Particles_EmitStretch(p, Vector3Scale(Vector3Subtract(h->pos, p), 1.2f), V4(1.2f, 0.6f, 1.6f, 0.0f), V4(0.8f, 0.3f, 1.2f, 0.6f), 0.2f, 0.8f, 0.08f, PF_ADD);
                }
            } break;
            case HZ_CLOT:
                if (h->hp <= 0.0f) {
                    Particles_Burst(h->pos, 40, 10.0f, V4(0.6f, 0.05f, 0.05f, 1.0f), V4(0.3f, 0.02f, 0.02f, 0.0f), 0.6f, 1.0f, 0);
                    Sfx_Play3D(SFX_SPLAT, h->pos, 0.8f);
                    h->alive = false;
                }
                break;
            default: break;
        }
    }
}

/*
 * projectiles.c - everything that flies. Player toxin globules can pierce,
 * poison and seed toxic clouds; immune perforin and boss orbs hit the
 * player and the swarm. Wall impacts splash along the surface normal.
 */
#include "entities/projectiles.h"
#include "entities/bosses.h"
#include "systems/combat.h"
#include "systems/spatial_hash.h"
#include "world/sdf_cave.h"
#include "world/hazards.h"
#include "gfx/particles.h"
#include "gfx/lights.h"
#include "audio/sfx.h"
#include "core/pool.h"
#include "core/rng.h"
#include <string.h>

static int sCursor = 0;

typedef struct { float radius, life; Vector4 color; } ProjStyle;
static const ProjStyle STYLE[PT_COUNT] = {
    [PT_TOXIN]    = { 0.30f, 2.2f, { 0.5f, 2.6f, 0.7f, 1.0f } },
    [PT_ACID]     = { 0.30f, 2.2f, { 1.6f, 2.4f, 0.3f, 1.0f } },
    [PT_PERFORIN] = { 0.34f, 3.5f, { 2.6f, 0.5f, 0.6f, 1.0f } },
    [PT_BOSS_ORB] = { 0.85f, 7.0f, { 2.4f, 0.9f, 2.2f, 1.0f } },
    [PT_MUCUS]    = { 1.10f, 5.0f, { 1.3f, 1.2f, 0.6f, 1.0f } },
    [PT_ACIDGLOB] = { 0.95f, 5.0f, { 1.2f, 2.2f, 0.2f, 1.0f } },
    [PT_BOLT]     = { 0.50f, 5.0f, { 0.8f, 1.2f, 3.0f, 1.0f } },
};

int Proj_Spawn(ProjType type, Vector3 pos, Vector3 vel, float damage, bool fromPlayer)
{
    int i = Pool_Alloc(G.proj, sizeof(Projectile), offsetof(Projectile, alive), MAX_PROJECTILES, &sCursor);
    if (i < 0) return -1;
    Projectile *p = &G.proj[i];
    memset(p, 0, sizeof(*p));
    p->alive = true;
    p->type = (uint8_t)type;
    p->team = fromPlayer ? TEAM_PLAYER : TEAM_IMMUNE;
    p->pos = p->prev = pos;
    p->vel = vel;
    p->radius = STYLE[type].radius;
    p->life = p->maxLife = STYLE[type].life;
    p->damage = damage;
    p->target = REF_NONE;
    if (fromPlayer) p->pierce = G.stats.pierce;
    return i;
}

void Proj_Clear(void) { memset(G.proj, 0, sizeof(G.proj)); }
int Proj_Count(void) { return Pool_CountAlive(G.proj, sizeof(Projectile), offsetof(Projectile, alive), MAX_PROJECTILES); }

static void Impact(Projectile *p, Vector3 normal, bool wall)
{
    Vector4 c = STYLE[p->type].color;
    Vector3 n = normal;
    for (int k = 0; k < 10; k++) {
        Vector3 v = Vector3Add(Vector3Scale(n, Rng_RangeG(2.0f, 7.0f)), Vector3Scale(Rng_UnitVecG(), 3.0f));
        Particles_Emit(p->pos, v, c, V4(c.x * 0.3f, c.y * 0.3f, c.z * 0.3f, 0.0f), p->radius * 0.6f, 0.05f, Rng_RangeG(0.25f, 0.5f), 3.0f, PF_ADD);
    }
    if (p->type == PT_ACIDGLOB && wall) {
        Hazards_Spawn(HZ_ACID_POOL, Vector3Add(p->pos, Vector3Scale(normal, 1.0f)), 4.5f, 9.0f, 10.0f);
        Sfx_Play3D(SFX_SPLAT, p->pos, 0.7f);
    } else if (p->type == PT_MUCUS) {
        Hazards_Spawn(HZ_MUCUS, p->pos, 4.0f, 6.0f, 0.6f);
        Sfx_Play3D(SFX_SPLAT, p->pos, 0.6f);
    } else if (p->type == PT_BOSS_ORB || p->type == PT_BOLT) {
        Lights_AddTemp(p->pos, Vector3Scale((Vector3){ c.x, c.y, c.z }, 0.6f), 6.0f, 0.2f);
    }
    p->alive = false;
}

static void PlayerHit(Projectile *p, Agent *a)
{
    PlayerStats *s = &G.stats;
    Player *pl = &G.player;
    float dmg = p->damage;
    Combat_DamageAgent(a, dmg, DMG_TOXIN, p->vel);
    if (a->alive && s->poisonDps > 0.0f) {
        a->poisonDps = fmaxf(a->poisonDps, s->poisonDps);
        a->poisonT = fmaxf(a->poisonT, s->poisonTime);
    }
    if (s->cloudChance > 0.0f && Rng_FloatG() < s->cloudChance) Hazards_Spawn(HZ_TOXIN_CLOUD, p->pos, 4.0f, 4.5f, fmaxf(5.0f, s->poisonDps));
    (void)pl;
}

void Proj_Update(float dt)
{
    Player *pl = &G.player;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *p = &G.proj[i];
        if (!p->alive) continue;
        p->life -= dt;
        if (p->life <= 0.0f) {
            if (p->type == PT_MUCUS || p->type == PT_ACIDGLOB) Impact(p, (Vector3){ 0, 1, 0 }, false);
            p->alive = false;
            continue;
        }
        p->prev = p->pos;
        /* homing */
        if (p->homing > 0.0f) {
            Vector3 tp;
            bool ok = false;
            if (p->targetPlayer) { if (!pl->dead) { tp = pl->pos; ok = true; } }
            else { Agent *t = Agent_Get(p->target); if (t) { tp = t->pos; ok = true; } }
            if (ok) {
                float sp = Vector3Length(p->vel);
                Vector3 want = Vector3Scale(Vector3Normalize(Vector3Subtract(tp, p->pos)), sp);
                p->vel = Vector3Lerp(p->vel, want, 1.0f - expf(-p->homing * dt));
                p->vel = Vector3Scale(Vector3Normalize(p->vel), sp);
            }
        }
        if (p->type == PT_MUCUS || p->type == PT_ACIDGLOB) p->vel.y -= 6.0f * dt;   /* heavy globs sink */
        p->pos = Vector3Add(p->pos, Vector3Scale(p->vel, dt));

        /* trail */
        Vector4 c = STYLE[p->type].color;
        if (GetRandomValue(0, 100) < (p->team == TEAM_PLAYER ? 45 : 70))
            Particles_Emit(p->pos, Vector3Scale(p->vel, -0.05f), V4(c.x * 0.7f, c.y * 0.7f, c.z * 0.7f, 0.8f), V4(c.x * 0.2f, c.y * 0.2f, c.z * 0.2f, 0.0f),
                           p->radius * 0.8f, 0.02f, 0.25f + p->radius * 0.2f, 2.0f, PF_ADD);

        /* walls */
        Vector3 g;
        float o = Cave_SampleGrad(p->pos, &g);
        if (o < p->radius * 0.4f) { Impact(p, g, true); continue; }

        if (p->team == TEAM_PLAYER) {
            int ids[8];
            int n = SH_Query(p->pos, p->radius + 0.25f, ids, 8, AGENT_MASK_IMMUNE | KMASK(AK_BACTERIA));
            bool consumed = false;
            for (int k = 0; k < n && !consumed; k++) {
                Agent *a = &G.agents[ids[k]];
                if (!a->alive || (a->flags & AF_ALLY)) continue;
                if (p->target.idx == ids[k] && p->target.gen == a->gen && p->pierce >= 0 && p->flags) continue;   /* already pierced */
                PlayerHit(p, a);
                if (p->pierce > 0) {
                    p->pierce--;
                    p->target = Agent_Ref(ids[k]);
                    p->flags = 1;
                    p->homing = 0.0f;
                } else {
                    consumed = true;
                }
            }
            if (consumed) { Impact(p, Vector3Normalize(Vector3Negate(p->vel)), false); continue; }
            Vector3 bh;
            if (G.boss.active && Bosses_HitTest(p->pos, p->radius, &bh)) {
                Combat_DamageBoss(p->damage, DMG_TOXIN, bh);
                Impact(p, Vector3Normalize(Vector3Negate(p->vel)), false);
                continue;
            }
            for (int h = 0; h < MAX_HAZARDS; h++) {
                Hazard *hz = &G.hazards[h];
                if (hz->alive && hz->type == HZ_CLOT && Vector3Distance(hz->pos, p->pos) < hz->radius + p->radius) {
                    hz->hp -= p->damage;
                    Impact(p, Vector3Normalize(Vector3Subtract(p->pos, hz->pos)), false);
                    break;
                }
            }
        } else {
            if (!pl->dead && Vector3Distance(p->pos, pl->pos) < p->radius + pl->radius) {
                if (pl->invuln <= 0.0f) {
                    Combat_DamagePlayer(p->damage, Vector3Subtract(p->pos, Vector3Scale(p->vel, 0.1f)), DMG_ENEMY);
                    if (p->type == PT_MUCUS) Hazards_Spawn(HZ_MUCUS, pl->pos, 3.5f, 3.0f, 0.65f);
                    Impact(p, Vector3Normalize(Vector3Negate(p->vel)), false);
                    continue;
                }
            }
            int ids[8];
            int n = SH_Query(p->pos, p->radius + 0.2f, ids, 8, AGENT_MASK_PLAYER_SIDE);
            if (n > 0) {
                Combat_DamageAgent(&G.agents[ids[0]], p->damage, DMG_ENEMY, p->vel);
                Impact(p, Vector3Normalize(Vector3Negate(p->vel)), false);
                continue;
            }
        }
    }
}

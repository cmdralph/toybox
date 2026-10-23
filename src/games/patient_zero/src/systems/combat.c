/*
 * combat.c - damage resolution and feedback.
 */
#include "systems/combat.h"
#include "systems/immune_memory.h"
#include "systems/spatial_hash.h"
#include "systems/ai.h"
#include "entities/enemies.h"
#include "entities/pickups.h"
#include "entities/player.h"
#include "entities/bosses.h"
#include "entities/minions.h"
#include "world/hazards.h"
#include "gfx/particles.h"
#include "gfx/lights.h"
#include "gfx/camera.h"
#include "audio/sfx.h"
#include "ui/hud.h"
#include "ui/bestiary_ui.h"
#include "core/pool.h"
#include "core/rng.h"
#include "core/math_utils.h"
#include <stdio.h>
#include <string.h>

static int sChainDepth = 0;

void Combat_HitStop(float t) { G.hitStop = fmaxf(G.hitStop, t); }
void Combat_SlowMo(float t) { G.slowmoT = fmaxf(G.slowmoT, t); }

void Combat_Popup(Vector3 pos, const char *text, Color c, float size)
{
    int best = 0;
    for (int i = 0; i < MAX_POPUPS; i++) {
        if (!G.popups[i].active) { best = i; break; }
        if (G.popups[i].life < G.popups[best].life) best = i;
    }
    Popup *p = &G.popups[best];
    p->active = true;
    p->pos = Vector3Add(pos, Vector3Scale(Rng_UnitVecG(), 0.4f));
    strncpy(p->text, text, sizeof(p->text) - 1);
    p->text[sizeof(p->text) - 1] = 0;
    p->color = c;
    p->life = p->maxLife = 1.0f;
    p->size = size;
}

Vector3 Combat_KindColor(int kind)
{
    switch (kind) {
        case AK_NEUTROPHIL: return (Vector3){ 0.85f, 0.75f, 1.0f };
        case AK_MACROPHAGE: return (Vector3){ 1.0f, 0.85f, 0.6f };
        case AK_NK: return (Vector3){ 1.0f, 0.35f, 0.35f };
        case AK_TCELL: return (Vector3){ 0.5f, 0.8f, 1.0f };
        case AK_BCELL: return (Vector3){ 1.0f, 0.85f, 0.4f };
        case AK_ANTIBODY: return (Vector3){ 1.0f, 0.95f, 0.5f };
        case AK_BACTERIA: return (Vector3){ 0.5f, 1.0f, 0.4f };
        case AK_BOSS_PART: return (Vector3){ 0.8f, 1.0f, 0.6f };
        case AK_MINION:
        case AK_DECOY: return (Vector3){ 0.3f, 1.0f, 0.7f };
        default: return (Vector3){ 1, 1, 1 };
    }
}

static bool IsPlayerSource(DamageSource s) { return s != DMG_HAZARD && s != DMG_ENEMY; }

void Combat_DamageAgent(Agent *a, float dmg, DamageSource src, Vector3 dir)
{
    if (!a || !a->alive || dmg <= 0.0f) return;
    if (a->kind == AK_DRIFTER || a->kind == AK_PLATELET || a->kind == AK_HOST) return;
    bool playerSide = (a->team == TEAM_PLAYER) || (a->kind == AK_BACTERIA && (a->flags & AF_ALLY));
    if (playerSide && IsPlayerSource(src) && src != DMG_EXPLOSION) return;
    if (!playerSide && !IsPlayerSource(src) && src != DMG_HAZARD) return;

    if ((a->flags & AF_SHIELDED) && (src == DMG_TOXIN || src == DMG_POISON)) {
        if (G.stats.shieldShred && src == DMG_TOXIN) {
            a->flags &= (uint16_t)~AF_SHIELDED;
            Particles_Burst(a->pos, 14, 8.0f, V4(0.6f, 1.6f, 2.0f, 1.0f), V4(0.2f, 0.5f, 0.8f, 0.0f), 0.2f, 0.4f, PF_ADD);
        } else {
            dmg *= (1.0f - a->shield);
        }
    }
    a->hp -= dmg;
    a->flash = 1.0f;
    float l = Vector3Length(dir);
    if (l > 0.001f) a->vel = Vector3Add(a->vel, Vector3Scale(dir, fminf(dmg * 0.5f, 12.0f) / (l * fmaxf(a->radius, 0.5f))));

    if (!playerSide) {
        G.run.stats.damageDealt += dmg;
        switch (src) {
            case DMG_TOXIN: Immune_Use(IM_TOXIN, dmg * 0.05f); break;
            case DMG_MINION: Immune_Use(IM_SWARM, dmg * 0.06f); break;
            case DMG_BURST: Immune_Use(IM_BURST, 0.7f); break;
            case DMG_DASH:
            case DMG_CONTACT: Immune_Use(IM_DASH, dmg * 0.03f); break;
            default: break;
        }
        if (src != DMG_POISON && src != DMG_CONTACT) Sfx_PlayThrottled((a->flags & AF_SHIELDED) ? SFX_HIT_SHIELD : SFX_HIT, 0.45f, 0.045f);
        if (dmg >= 1.0f && src != DMG_POISON && src != DMG_CONTACT) {
            char t[16];
            snprintf(t, sizeof(t), "%d", (int)(dmg + 0.5f));
            Combat_Popup(Vector3Add(a->pos, (Vector3){ 0, a->radius + 0.5f, 0 }), t, (a->flags & AF_SHIELDED) ? (Color){ 120, 200, 255, 255 } : (Color){ 255, 245, 210, 255 }, 20.0f);
        }
        if (a->team == TEAM_IMMUNE && !(a->flags & AF_ALERTED)) {
            a->flags |= AF_ALERTED;
            a->state = AI_CHASE;
            a->targetPlayer = true;
            AI_AlertGroup(a->pos, ENEMY_GROUP_ALERT * 0.6f, G.player.pos);
        }
    }
    if (a->hp <= 0.0f) Combat_KillAgent(a, src);
}

void Combat_KillAgent(Agent *a, DamageSource src)
{
    if (!a->alive) return;
    int idx = (int)(a - G.agents);
    Vector3 pos = a->pos;
    Vector3 col = Combat_KindColor(a->kind);
    int kind = a->kind;
    bool big = (kind == AK_MACROPHAGE) || (a->flags & AF_ELITE);

    Particles_Burst(pos, big ? 60 : 24, 8.0f + a->radius * 4.0f, V4(col.x * 1.6f, col.y * 1.6f, col.z * 1.6f, 1.0f), V4(col.x * 0.3f, col.y * 0.3f, col.z * 0.3f, 0.0f), 0.25f + a->radius * 0.15f, 0.9f, PF_ADD);
    Particles_Burst(pos, big ? 30 : 10, 4.0f, V4(col.x * 0.6f, col.y * 0.5f, col.z * 0.5f, 0.6f), V4(col.x * 0.3f, col.y * 0.2f, col.z * 0.2f, 0.0f), 0.6f + a->radius * 0.3f, 1.6f, PF_FLOW);

    if (a->team == TEAM_IMMUNE || kind == AK_BACTERIA) {
        const EnemyDef *d = Enemies_Def(kind);
        if (a->team == TEAM_IMMUNE) Enemies_OnDeath(a);
        if (d && kind != AK_ANTIBODY) {
            G.run.stats.kills[kind]++;
            G.run.stats.killsTotal++;
            G.profile.totalKills++;
            float mul = (a->flags & AF_ELITE) ? 2.0f : 1.0f;
            Player_GainEP(d->ep * mul);
            int gm = (int)((float)d->gm * mul);
            int orbs = gm > 6 ? 3 : (gm > 2 ? 2 : 1);
            for (int i = 0; i < orbs && gm > 0; i++) {
                int idp = Pickups_Spawn(PK_GM, pos, (float)(gm / orbs));
                if (idp >= 0) G.pickups[idp].vel = Vector3Scale(Rng_UnitVecG(), 6.0f);
            }
            if (Rng_FloatG() < 0.3f) Pickups_Spawn(PK_ATP, pos, 15.0f);
            if (Rng_FloatG() < 0.12f) Pickups_Spawn(PK_BIOMASS, pos, 12.0f);
            G.region.alert = fminf(ALERT_MAX, G.region.alert + ALERT_KILL * G.stats.alertMul);
            Codex_Unlock(d->codex);
            if (G.stats.synApex) Player_Heal(4.0f);
        }
        Sfx_Play3D(big ? SFX_KILL_BIG : SFX_KILL, pos, 1.0f);
        Lights_AddTemp(pos, Vector3Scale(col, 2.0f), 8.0f + a->radius * 3.0f, 0.35f);
        if (big) { Combat_HitStop(0.05f); Combat_SlowMo(0.18f); Cam_AddTrauma(0.25f); }
        if (IsPlayerSource(src) && G.stats.chainLysis > 0.0f && sChainDepth < 3 && Rng_FloatG() < G.stats.chainLysis) {
            sChainDepth++;
            Combat_Explosion(pos, 6.0f, G.stats.toxinDamage * 1.5f, DMG_EXPLOSION, true);
            sChainDepth--;
        }
    } else if (kind == AK_MINION) {
        Minions_OnDeath(a);
        if (G.stats.minionExplode && sChainDepth < 3) {
            sChainDepth++;
            Combat_Explosion(pos, 3.5f, G.stats.minionDamage * 2.5f, DMG_EXPLOSION, true);
            sChainDepth--;
        }
        if (G.stats.synCytoBurst) Hazards_Spawn(HZ_TOXIN_CLOUD, pos, 4.0f, 4.0f, 9.0f);
        Sfx_Play3D(SFX_POP, pos, 0.6f);
    } else if (kind == AK_DECOY) {
        Sfx_Play3D(SFX_POP, pos, 0.5f);
    }
    if (kind == AK_BOSS_PART) Bosses_PartDestroyed(a);
    Agent_Free(idx);
}

void Combat_DamagePlayer(float dmg, Vector3 from, DamageSource src)
{
    Player *p = &G.player;
    (void)src;
    if (p->dead || dmg <= 0.0f || G.debug.godMode) return;
    if (p->invuln > 0.0f) return;
    dmg *= (1.0f - G.stats.armor) * (1.0f + ANTIBODY_AMP * (float)p->antibodies);
    p->biomass -= dmg;
    p->regenDelay = PLAYER_REGEN_DELAY;
    p->hurtFlash = fminf(1.0f, p->hurtFlash + 0.25f + dmg * 0.02f);
    p->squash = -0.25f;
    G.run.stats.damageTaken += dmg;
    Cam_AddTrauma(0.12f + dmg * 0.015f);
    Sfx_PlayThrottled(SFX_HURT, 0.9f, 0.08f);
    Vector3 d = Vector3Subtract(p->pos, from);
    float l = Vector3Length(d);
    if (l > 0.01f) p->vel = Vector3Add(p->vel, Vector3Scale(d, fminf(dmg * 0.6f, 14.0f) / l));
    if (p->attach.idx >= 0 && dmg > 5.0f) {
        p->attach = REF_NONE;
        HUD_FloatText("Knocked loose!", (Color){ 255, 140, 120, 255 });
    }
    if (p->biomass <= 0.0f) Player_Die();
}

void Combat_DamageBoss(float dmg, DamageSource src, Vector3 at)
{
    Bosses_TakeDamage(dmg, src, at);
}

void Combat_Explosion(Vector3 pos, float radius, float dmg, DamageSource src, bool fromPlayer)
{
    int ids[128];
    uint32_t mask = fromPlayer ? (AGENT_MASK_IMMUNE | KMASK(AK_BACTERIA)) : AGENT_MASK_PLAYER_SIDE;
    int n = SH_Query(pos, radius, ids, 128, mask);
    for (int k = 0; k < n; k++) {
        Agent *a = &G.agents[ids[k]];
        if (!a->alive) continue;
        Vector3 dir = Vector3Subtract(a->pos, pos);
        float f = 1.0f - Sat(Vector3Length(dir) / (radius + a->radius));
        Combat_DamageAgent(a, dmg * (0.4f + 0.6f * f), src, dir);
    }
    Vector3 bh;
    if (fromPlayer && G.boss.active && Bosses_HitTest(pos, radius, &bh)) Combat_DamageBoss(dmg * 0.6f, src, bh);
    if (fromPlayer) {
        for (int i = 0; i < MAX_HAZARDS; i++) {
            Hazard *h = &G.hazards[i];
            if (h->alive && h->type == HZ_CLOT && Vector3Distance(h->pos, pos) < radius + h->radius) h->hp -= dmg;
        }
    }
    Vector3 c = fromPlayer ? (Vector3){ 0.6f, 2.0f, 0.8f } : (Vector3){ 2.0f, 0.8f, 0.4f };
    Particles_Burst(pos, 26, radius * 2.2f, V4(c.x, c.y, c.z, 1.0f), V4(c.x * 0.2f, c.y * 0.2f, c.z * 0.2f, 0.0f), 0.35f, 0.5f, PF_ADD);
    Lights_AddTemp(pos, c, radius * 2.5f, 0.3f);
    Sfx_Play3D(SFX_EXPLODE, pos, 0.6f);
}

void Combat_PlayerBurst(void)
{
    Player *p = &G.player;
    PlayerStats *s = &G.stats;
    float radius = s->burstRadius * (0.7f + 0.3f * p->radius / 0.6f);
    int ids[256];
    int n = SH_Query(p->pos, radius, ids, 256, AGENT_MASK_IMMUNE | KMASK(AK_BACTERIA));
    float mul = (p->cloaked && p->ambushReady) ? 2.0f : 1.0f;
    for (int k = 0; k < n; k++) {
        Agent *a = &G.agents[ids[k]];
        if (!a->alive || ((a->flags & AF_ALLY))) continue;
        Vector3 dir = Vector3Normalize(Vector3Subtract(a->pos, p->pos));
        Combat_DamageAgent(a, s->burstDamage * mul, DMG_BURST, Vector3Scale(dir, 3.0f));
        if (a->alive) {
            a->vel = Vector3Add(a->vel, Vector3Scale(dir, 22.0f / fmaxf(1.0f, a->radius)));
            a->state = AI_STUNNED;
            a->timer = 0.8f;
        }
    }
    Vector3 bh;
    if (G.boss.active && Bosses_HitTest(p->pos, radius, &bh)) Combat_DamageBoss(s->burstDamage * mul, DMG_BURST, bh);
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G.hazards[i];
        if (h->alive && h->type == HZ_CLOT && Vector3Distance(h->pos, p->pos) < radius + h->radius) h->hp -= s->burstDamage;
    }
    if (p->antibodies > 0) {
        Particles_Burst(p->pos, p->antibodies * 4, 12.0f, V4(1.5f, 1.4f, 0.6f, 1.0f), V4(0.5f, 0.4f, 0.1f, 0.0f), 0.2f, 0.6f, PF_ADD);
        p->antibodies = 0;
    }
    Particles_Ring(p->pos, Cam_Up(), 90, radius * 2.6f, V4(0.6f, 2.4f, 1.4f, 1.0f), V4(0.1f, 0.6f, 0.3f, 0.0f), 0.5f, 0.45f);
    Particles_Ring(p->pos, Cam_Forward(), 60, radius * 2.2f, V4(0.6f, 2.0f, 1.8f, 1.0f), V4(0.1f, 0.5f, 0.4f, 0.0f), 0.4f, 0.4f);
    Particles_Burst(p->pos, 40, radius * 1.5f, V4(0.8f, 2.4f, 1.2f, 1.0f), V4(0.1f, 0.4f, 0.2f, 0.0f), 0.3f, 0.5f, PF_ADD);
    Lights_AddTemp(p->pos, (Vector3){ 1.5f, 4.0f, 2.5f }, radius * 3.0f, 0.4f);
    Cam_AddTrauma(0.35f);
    Cam_Kick(6.0f);
    Sfx_Play(SFX_BURST, 1.0f, 1.0f);
    p->ambushReady = false;
    p->squash = 0.35f;
}

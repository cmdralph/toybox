/*
 * hud.c - the in-game HUD and all transient message channels.
 *
 * Layout (virtual 1280x720, anchored to edges on wide screens):
 *   top-left     evolution ring, biomass / ATP bars, genetic material, antibodies
 *   top-centre   infection meter + objective (or boss bar / outbreak wave)
 *   top-right    alert meter, detection warning, active event, immune memory
 *   bottom-left  radar (camera-relative, height-coded blips)
 *   bottom-centre ability bar with cooldown sweeps and costs
 *   centre       crosshair, infect prompt, rhythm-injection ring, struggle prompt
 */
#include "ui/hud.h"
#include "ui/widgets.h"
#include "systems/evolution.h"
#include "systems/infection.h"
#include "systems/immune_memory.h"
#include "entities/enemies.h"
#include "entities/bosses.h"
#include "entities/minions.h"
#include "world/region.h"
#include "world/events.h"
#include "audio/sfx.h"
#include "core/save.h"
#include "core/settings.h"
#include "core/pool.h"
#include <stdio.h>
#include <string.h>

const AchievementDef ACHIEVEMENTS[ACH_COUNT] = {
    [ACH_FIRST_INFECTION] = { "Patient Zero", "Infect your first host cell." },
    [ACH_RHYTHM] = { "In the Groove", "Land a 5-beat perfect injection combo." },
    [ACH_STAGE2] = { "Replication", "Evolve into a Replicator." },
    [ACH_STAGE3] = { "Colonist", "Evolve into a Colony Organism." },
    [ACH_STAGE5] = { "Pandemic", "Reach the final evolutionary stage." },
    [ACH_HIVEMIND] = { "Hive Mind", "Command 30 virions at once." },
    [ACH_ARMS_RACE] = { "Arms Race", "Force the immune system to adapt 6 times in one run." },
    [ACH_GHOST] = { "Ghost in the Blood", "Clear a region without Alert ever passing 40%." },
    [ACH_GIANT_SLAYER] = { "Giant Slayer", "Destroy a region guardian." },
    [ACH_TOTAL_INFECTION] = { "Total Infection", "Complete the campaign." },
    [ACH_CHIMERA] = { "Chimera", "Own 20 mutations at once." },
    [ACH_OUTBREAK10] = { "Endemic", "Survive 10 waves in Outbreak mode." },
    [ACH_SATURATION] = { "Saturation", "Infect every host cell in a region." },
    [ACH_SYNERGIST] = { "Synergist", "Activate a mutation synergy." },
};

typedef struct { char text[96]; Color color; float t; } FloatMsg;
static FloatMsg sFloat[4];
static float sFlash = 0.0f;
static Color sFlashColor = { 255, 255, 255, 255 };
static float sBioGhost = 1.0f;
static float sToastSlide[MAX_TOASTS];
static bool sToastAch[MAX_TOASTS];

void HUD_Reset(void)
{
    memset(sFloat, 0, sizeof(sFloat));
    memset(G.toasts, 0, sizeof(G.toasts));
    G.ui.bannerT = 0.0f;
    G.ui.hintT = 0.0f;
    sFlash = 0.0f;
    sBioGhost = 1.0f;
}

void HUD_Banner(const char *title, const char *sub, Color c)
{
    snprintf(G.ui.banner, sizeof(G.ui.banner), "%s", title ? title : "");
    snprintf(G.ui.bannerSub, sizeof(G.ui.bannerSub), "%s", sub ? sub : "");
    G.ui.bannerT = 3.6f;
    G.ui.bannerColor = c;
}

static void PushToast(const char *title, const char *text, Color c, bool ach)
{
    /* shift down, newest on top */
    for (int i = MAX_TOASTS - 1; i > 0; i--) { G.toasts[i] = G.toasts[i - 1]; sToastSlide[i] = sToastSlide[i - 1]; sToastAch[i] = sToastAch[i - 1]; }
    snprintf(G.toasts[0].title, sizeof(G.toasts[0].title), "%s", title ? title : "");
    snprintf(G.toasts[0].text, sizeof(G.toasts[0].text), "%s", text ? text : "");
    G.toasts[0].life = ach ? 6.0f : 5.0f;
    G.toasts[0].color = c;
    sToastSlide[0] = 0.0f;
    sToastAch[0] = ach;
}

void HUD_Toast(const char *title, const char *text, Color c) { PushToast(title, text, c, false); }

void HUD_Hint(TutorialId id, const char *text)
{
    if (id < 0 || id >= TUT_COUNT || G.profile.tutorials[id]) return;
    G.profile.tutorials[id] = 1;
    snprintf(G.ui.hint, sizeof(G.ui.hint), "%s", text);
    G.ui.hintT = 9.0f;
}

void HUD_Achievement(AchId id)
{
    if (id < 0 || id >= ACH_COUNT || G.profile.achievements[id]) return;
    G.profile.achievements[id] = 1;
    char t[64];
    snprintf(t, sizeof(t), "ACHIEVEMENT: %s", ACHIEVEMENTS[id].name);
    PushToast(t, ACHIEVEMENTS[id].desc, (Color){ 255, 215, 90, 255 }, true);
    Sfx_Play(SFX_ACHIEVE, 0.8f, 1.0f);
    Save_WriteProfile();
}

void HUD_Flash(Color c, float amount)
{
    sFlashColor = c;
    sFlash = fmaxf(sFlash, amount);
}

float HUD_FlashAmount(Color *c)
{
    if (c) *c = sFlashColor;
    return sFlash;
}

void HUD_FloatText(const char *text, Color c)
{
    for (int i = 0; i < 4; i++) if (sFloat[i].t > 0.0f && strcmp(sFloat[i].text, text) == 0) { sFloat[i].t = 1.8f; return; }
    for (int i = 3; i > 0; i--) sFloat[i] = sFloat[i - 1];
    snprintf(sFloat[0].text, sizeof(sFloat[0].text), "%s", text);
    sFloat[0].color = c;
    sFloat[0].t = 1.8f;
}

void HUD_RegionTitle(void) { G.ui.regionTitleT = 7.0f; }

void HUD_Update(float dt)
{
    G.ui.bannerT = fmaxf(0.0f, G.ui.bannerT - dt);
    G.ui.hintT = fmaxf(0.0f, G.ui.hintT - dt);
    G.ui.regionTitleT = fmaxf(0.0f, G.ui.regionTitleT - dt);
    sFlash = fmaxf(0.0f, sFlash - dt * 2.2f);
    for (int i = 0; i < MAX_TOASTS; i++) {
        if (G.toasts[i].life > 0.0f) G.toasts[i].life -= dt;
        sToastSlide[i] = fminf(1.0f, sToastSlide[i] + dt * 4.0f);
    }
    for (int i = 0; i < 4; i++) sFloat[i].t = fmaxf(0.0f, sFloat[i].t - dt);
    for (int i = 0; i < MAX_POPUPS; i++) {
        Popup *p = &G.popups[i];
        if (!p->active) continue;
        p->life -= dt;
        p->pos.y += dt * 2.2f;
        if (p->life <= 0.0f) p->active = false;
    }
    float bio = G.stats.maxBiomass > 0.0f ? G.player.biomass / G.stats.maxBiomass : 0.0f;
    if (bio > sBioGhost) sBioGhost = bio;
    else sBioGhost = fmaxf(bio, sBioGhost - dt * 0.35f);
}

/* ------------------------------------------------------------------------- */
/* Pieces                                                                     */
/* ------------------------------------------------------------------------- */

static const char *ROMAN[5] = { "I", "II", "III", "IV", "V" };

static void DrawVitals(void)
{
    Player *p = &G.player;
    PlayerStats *s = &G.stats;
    Color acc = UI_Accent();
    UI_Panel((Rectangle){ 14, 14, 360, 112 }, (Color){ 5, 12, 14, 150 }, (Color){ 80, 160, 140, 60 });
    Vector2 c = { 60, 62 };
    float ep = Evolution_Progress();
    UI_Ring(c, 30, 37, 0, 360, (Color){ 30, 45, 45, 255 });
    UI_Ring(c, 30, 37, -90, -90 + 360.0f * ep, ep >= 1.0f && p->stage < STAGE_COUNT - 1 ? (Color){ 255, 240, 140, 255 } : acc);
    UI_Circle(c, 27, (Color){ 10, 30, 26, 255 });
    UI_TextCentered(ROMAN[p->stage], c.x, c.y - 12, 24, (Color){ 220, 255, 235, 255 });
    UI_TextCentered(STAGES[p->stage].name, c.x, 104, 10, (Color){ 160, 210, 190, 255 });

    float x = 108;
    float bio = s->maxBiomass > 0.0f ? p->biomass / s->maxBiomass : 0.0f;
    Color bc = bio < 0.3f ? (Color){ 255, 70, 70, 255 } : (Color){ 230, 90, 100, 255 };
    if (bio < 0.3f && fmodf(G.realTime, 0.6f) < 0.3f) bc = (Color){ 255, 160, 160, 255 };
    UI_Icon(ICON_HEART, (Vector2){ x + 8, 34 }, 18, bc);
    UI_Bar((Rectangle){ x + 22, 27, 236, 14 }, bio, bc, (Color){ 40, 20, 24, 220 }, sBioGhost);
    char t[64];
    snprintf(t, sizeof(t), "%d / %d", (int)ceilf(p->biomass), (int)s->maxBiomass);
    UI_TextRight(t, x + 254, 44, 10, (Color){ 230, 200, 200, 255 });
    UI_Text("BIOMASS", x + 24, 44, 10, (Color){ 200, 150, 150, 200 });

    float atp = s->maxAtp > 0.0f ? p->atp / s->maxAtp : 0.0f;
    UI_Icon(ICON_ATP, (Vector2){ x + 8, 66 }, 18, (Color){ 255, 220, 90, 255 });
    UI_Bar((Rectangle){ x + 22, 59, 236, 12 }, atp, (Color){ 250, 205, 70, 255 }, (Color){ 40, 34, 18, 220 }, 0.0f);
    snprintf(t, sizeof(t), "%d", (int)p->atp);
    UI_TextRight(t, x + 254, 74, 10, (Color){ 240, 220, 160, 255 });
    UI_Text("ATP", x + 24, 74, 10, (Color){ 200, 180, 120, 200 });

    UI_Icon(ICON_DNA, (Vector2){ x + 8, 98 }, 20, (Color){ 120, 220, 255, 255 });
    snprintf(t, sizeof(t), "%d GM", p->gm);
    UI_Text(t, x + 22, 90, 20, (Color){ 170, 235, 255, 255 });
    if (p->antibodies > 0) {
        snprintf(t, sizeof(t), "ANTIBODIES x%d  (-%d%% speed)", p->antibodies, (int)(ANTIBODY_SLOW * 100.0f * (float)p->antibodies));
        UI_Text(t, x + 120, 94, 10, (Color){ 255, 220, 120, 255 });
    }
}

static void DrawObjective(void)
{
    RegionState *rg = &G.region;
    float W = UI_Width();
    float cx = W * 0.5f;
    if (G.boss.active) {
        Boss *b = &G.boss;
        const BossDef *d = &BOSSES[b->type];
        float w = 470;
        UI_TextCentered(d->name, cx, 18, 20, (Color){ 255, 200, 180, 255 });
        Rectangle bar = { cx - w * 0.5f, 42, w, 16 };
        UI_Bar(bar, b->hp / b->maxHp, b->shielded ? (Color){ 110, 190, 255, 255 } : (Color){ 230, 70, 60, 255 }, (Color){ 40, 15, 15, 230 }, 0.0f);
        UI_Rect2((Rectangle){ bar.x + w * 0.6f - 1, bar.y - 2, 2, 20 }, (Color){ 255, 255, 255, 140 });
        UI_Rect2((Rectangle){ bar.x + w * 0.3f - 1, bar.y - 2, 2, 20 }, (Color){ 255, 255, 255, 140 });
        char t[64];
        snprintf(t, sizeof(t), "%s   -   PHASE %s", d->title, ROMAN[b->phase]);
        UI_TextCentered(t, cx, 62, 10, (Color){ 220, 180, 170, 220 });
        const char *pat = Bosses_PatternName();
        if (pat[0]) UI_TextCentered(pat, cx, 78, 14, (Color){ 255, 120, 100, (unsigned char)(180 + 75 * sinf(G.realTime * 8.0f)) });
        return;
    }
    if (G.run.mode == MODE_OUTBREAK) {
        char t[96];
        snprintf(t, sizeof(t), "OUTBREAK  -  WAVE %d", G.run.wave);
        UI_TextCentered(t, cx, 18, 22, (Color){ 255, 170, 140, 255 });
        if (G.run.waveT > 0.0f && Enemies_WaveRemaining() == 0) snprintf(t, sizeof(t), "Next wave in %d s", (int)ceilf(G.run.waveT));
        else snprintf(t, sizeof(t), "%d immune cells remaining   -   infection %d%%", Enemies_WaveRemaining(), (int)(rg->infection * 100.0f));
        UI_TextCentered(t, cx, 46, 12, (Color){ 220, 210, 200, 255 });
        return;
    }
    float w = 380;
    Rectangle bar = { cx - w * 0.5f, 34, w, 12 };
    UI_TextCentered(Region_Def()->name, cx, 14, 12, (Color){ 200, 220, 210, 200 });
    UI_Bar(bar, rg->infection, (Color){ 90, 240, 150, 255 }, (Color){ 20, 40, 32, 220 }, 0.0f);
    float mark = bar.x + w * BOSS_INFECTION_PCT;
    UI_Rect2((Rectangle){ mark - 1, bar.y - 4, 2, 20 }, (Color){ 255, 120, 100, 220 });
    UI_Icon(ICON_SKULL, (Vector2){ mark, bar.y - 9 }, 10, (Color){ 255, 120, 100, 220 });
    char t[160];
    snprintf(t, sizeof(t), "INFECTION %d%%", (int)(rg->infection * 100.0f + 0.5f));
    UI_Text(t, bar.x + w + 10, bar.y, 12, (Color){ 140, 255, 190, 255 });
    Region_ObjectiveText(t, sizeof(t));
    UI_TextCentered(t, cx, 54, 12, (Color){ 220, 235, 225, 230 });
}

static void DrawAlert(void)
{
    RegionState *rg = &G.region;
    float W = UI_Width();
    float x = W - 290;
    UI_Panel((Rectangle){ x - 10, 14, 286, 60 }, (Color){ 5, 12, 14, 150 }, (Color){ 80, 160, 140, 60 });
    float a = rg->alert / ALERT_MAX;
    Color c = a < 0.35f ? (Color){ 120, 230, 160, 255 } : (a < 0.7f ? (Color){ 250, 200, 80, 255 } : (Color){ 255, 80, 70, 255 });
    bool det = Enemies_Detected();
    Color eye = det ? (Color){ 255, 90, 80, (unsigned char)(180 + 75 * sinf(G.realTime * 12.0f)) } : c;
    UI_Icon(ICON_EYE, (Vector2){ x + 16, 34 }, 26, eye);
    UI_Bar((Rectangle){ x + 36, 28, 196, 12 }, a, c, (Color){ 30, 30, 30, 220 }, rg->peakAlert / ALERT_MAX);
    char t[64];
    snprintf(t, sizeof(t), "%d%%", (int)(a * 100.0f));
    UI_TextRight(t, x + 270, 27, 14, c);
    UI_Text(det ? "DETECTED" : (G.player.cloaked ? "CLOAKED" : "ALERT"), x + 38, 46, 12, det ? (Color){ 255, 110, 90, 255 } : (G.player.cloaked ? (Color){ 150, 160, 255, 255 } : (Color){ 180, 200, 190, 220 }));
    if (rg->event != EV_NONE) {
        snprintf(t, sizeof(t), "%s  %ds", Events_Name(rg->event), (int)ceilf(rg->eventDur - rg->eventT));
        UI_TextRight(t, x + 270, 58, 10, (Color){ 255, 170, 120, 255 });
    }

    /* immune memory */
    float y = 82;
    UI_Panel((Rectangle){ x - 10, y, 286, 104 }, (Color){ 5, 12, 14, 130 }, (Color){ 160, 80, 80, 50 });
    UI_Text("IMMUNE MEMORY", x, y + 6, 10, (Color){ 230, 160, 150, 230 });
    for (int i = 0; i < IM_COUNT; i++) {
        float r = Immune_Resist((ImmuneCat)i);
        float yy = y + 22 + (float)i * 16;
        UI_Text(Immune_CatName((ImmuneCat)i), x, yy, 10, (Color){ 200, 200, 200, 220 });
        Color rc = r < 0.25f ? (Color){ 150, 160, 150, 255 } : (r < 0.5f ? (Color){ 240, 190, 90, 255 } : (Color){ 255, 90, 80, 255 });
        UI_Bar((Rectangle){ x + 100, yy + 1, 130, 8 }, r, rc, (Color){ 30, 30, 30, 200 }, 0.0f);
        snprintf(t, sizeof(t), "%d%%", (int)(r * 100.0f));
        UI_TextRight(t, x + 270, yy, 10, rc);
    }
}

static void DrawAbilities(void)
{
    Player *p = &G.player;
    PlayerStats *s = &G.stats;
    float W = UI_Width(), H = UI_Height();
    typedef struct { IconId icon; int action; float cd, cdMax, cost; bool locked; const char *name; } Slot;
    Slot slots[4] = {
        { ICON_DASH, ACT_DASH, p->dashCd, s->dashCd, s->dashCost, false, "DASH" },
        { ICON_BURST, ACT_BURST, p->burstCd, s->burstCd, s->burstCost, false, "LYSIS" },
        { ICON_SWARM, ACT_SWARM, p->swarmCd, s->swarmCd, 0.0f, p->stage < 1, "HIVE" },
        { ICON_CLOAK, ACT_CLOAK, p->cloaked ? 0.0f : p->cloakCd, CLOAK_COOLDOWN, 20.0f, !s->cloakUnlocked, "CLOAK" },
    };
    float size = 58, gap = 14;
    float total = 4 * size + 3 * gap;
    float x0 = W * 0.5f - total * 0.5f, y = H - size - 22;
    for (int i = 0; i < 4; i++) {
        Slot *sl = &slots[i];
        Rectangle r = { x0 + (float)i * (size + gap), y, size, size };
        bool ready = !sl->locked && sl->cd <= 0.0f && p->atp >= sl->cost;
        bool active = (i == 3 && p->cloaked);
        Color edge = sl->locked ? (Color){ 60, 60, 70, 200 } : (ready ? UI_Accent() : (Color){ 120, 120, 120, 200 });
        if (active) edge = (Color){ 150, 160, 255, 255 };
        UI_Panel(r, (Color){ 8, 16, 18, 200 }, edge);
        Vector2 c = { r.x + size * 0.5f, r.y + size * 0.5f - 4 };
        Color ic = sl->locked ? (Color){ 70, 70, 80, 255 } : (ready ? (Color){ 220, 255, 235, 255 } : (Color){ 130, 140, 135, 255 });
        UI_Icon(sl->icon, c, 34, ic);
        if (!sl->locked && sl->cd > 0.0f && sl->cdMax > 0.0f) {
            float k = sl->cd / sl->cdMax;
            UI_Ring(c, 22, 26, -90, -90 + 360.0f * k, (Color){ 255, 255, 255, 140 });
            char t[8];
            snprintf(t, sizeof(t), "%.1f", sl->cd);
            UI_TextCentered(t, c.x, c.y - 7, 14, (Color){ 255, 255, 255, 230 });
        }
        if (sl->locked) UI_Icon(ICON_LOCK, (Vector2){ r.x + size - 12, r.y + 12 }, 14, (Color){ 150, 150, 160, 255 });
        if (active) UI_Ring(c, 26, 28, -90, -90 + 360.0f * (p->cloakT / s->cloakDuration), (Color){ 150, 160, 255, 255 });
        const char *key = Settings_KeyName(G.settings.keys[sl->action]);
        UI_Panel((Rectangle){ r.x + 3, r.y + size - 15, UI_Measure(key, 10) + 8, 13 }, (Color){ 0, 0, 0, 180 }, (Color){ 0, 0, 0, 0 });
        UI_Text(key, r.x + 7, r.y + size - 13, 10, (Color){ 230, 230, 230, 255 });
        if (sl->cost > 0.0f && !sl->locked) {
            char t[8];
            snprintf(t, sizeof(t), "%d", (int)sl->cost);
            UI_TextRight(t, r.x + size - 4, r.y + size - 13, 10, p->atp >= sl->cost ? (Color){ 250, 220, 120, 255 } : (Color){ 255, 90, 80, 255 });
        }
        UI_TextCentered(sl->name, r.x + size * 0.5f, r.y - 13, 10, (Color){ 170, 200, 190, 200 });
        if (i == 2 && p->stage >= 1) {
            char t[16];
            snprintf(t, sizeof(t), "%d/%d", Minions_Count(), s->minionMax);
            UI_TextCentered(t, r.x + size * 0.5f, r.y + size + 3, 10, (Color){ 140, 255, 220, 255 });
        }
    }
    /* left/right mouse hints */
    UI_Text("LMB  toxin", x0 - 112, y + 14, 12, (Color){ 170, 210, 190, 200 });
    UI_Text("RMB  infect", x0 - 112, y + 32, 12, (Color){ 170, 210, 190, 200 });
    char t[32];
    snprintf(t, sizeof(t), "%s  genome", Settings_KeyName(G.settings.keys[ACT_MUTATIONS]));
    UI_Text(t, x0 + total + 18, y + 14, 12, (Color){ 170, 210, 190, 200 });
    if (p->gm >= 25) UI_Text("mutations available!", x0 + total + 18, y + 32, 10, (Color){ 120, 230, 255, (unsigned char)(160 + 90 * sinf(G.realTime * 4.0f)) });
}

static Vector2 RadarPoint(Vector3 rel, float R, float range, float yaw, bool *clamped)
{
    /* rotate world xz into camera space: up on the radar = camera forward */
    float fx = sinf(yaw), fz = cosf(yaw);
    float forward = rel.x * fx + rel.z * fz;
    float right = rel.x * (-fz) + rel.z * fx;
    Vector2 v = { -right / range * R, -forward / range * R };
    float l = sqrtf(v.x * v.x + v.y * v.y);
    *clamped = false;
    if (l > R) { v.x *= R / l; v.y *= R / l; *clamped = true; }
    return v;
}

static void DrawRadar(void)
{
    Player *p = &G.player;
    float H = UI_Height();
    Vector2 c = { 110, H - 112 };
    float R = 88, range = 95.0f;
    UI_Circle(c, R + 4, (Color){ 4, 12, 12, 170 });
    UI_Ring(c, R + 2, R + 4, 0, 360, (Color){ 80, 170, 150, 120 });
    UI_Ring(c, R * 0.5f - 0.5f, R * 0.5f + 0.5f, 0, 360, (Color){ 80, 170, 150, 50 });
    float sweep = fmodf(G.realTime * 90.0f, 360.0f);
    UI_Ring(c, 4, R, sweep - 30.0f, sweep, (Color){ 80, 255, 170, 18 });
    float yaw = G.rig.yaw;
    for (int i = 0; i < G.agentHigh; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive) continue;
        Color col;
        float sz;
        switch (a->kind) {
            case AK_HOST: col = (a->flags & AF_INFECTED) ? (Color){ 90, 255, 150, 230 } : (Color){ 230, 230, 230, 160 }; sz = 2.2f; break;
            case AK_MINION: col = (Color){ 120, 255, 230, 180 }; sz = 1.3f; break;
            case AK_NEUTROPHIL: case AK_MACROPHAGE: case AK_NK: case AK_TCELL: case AK_BCELL:
                col = (a->state == AI_CHASE || a->state == AI_ATTACK || a->state == AI_WINDUP) ? (Color){ 255, 70, 60, 255 } : (Color){ 255, 160, 120, 210 };
                sz = 1.8f + a->radius * 0.8f;
                break;
            case AK_BOSS_PART: col = (Color){ 120, 200, 255, 255 }; sz = 3.0f; break;
            default: continue;
        }
        Vector3 rel = Vector3Subtract(a->pos, p->pos);
        if (rel.x * rel.x + rel.z * rel.z > range * range * 1.2f) continue;
        bool cl;
        Vector2 v = RadarPoint(rel, R, range, yaw, &cl);
        if (cl) continue;
        float dy = rel.y;
        if (fabsf(dy) > 10.0f) col.a = (unsigned char)(col.a * 0.55f);
        UI_Circle((Vector2){ c.x + v.x, c.y + v.y }, sz, col);
        if (a->kind == AK_HOST && (a->flags & AF_INFECTED) && a->progress > 0.05f) UI_Ring((Vector2){ c.x + v.x, c.y + v.y }, 3.5f, 5.0f, 0, 360, (Color){ 255, 80, 80, (unsigned char)(150 + 100 * sinf(G.realTime * 10.0f)) });
    }
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *k = &G.pickups[i];
        if (!k->alive || (k->type != PK_CACHE && k->type != PK_POD)) continue;
        bool cl;
        Vector2 v = RadarPoint(Vector3Subtract(k->pos, p->pos), R, range, yaw, &cl);
        UI_Icon(ICON_STAR, (Vector2){ c.x + v.x, c.y + v.y }, cl ? 8 : 10, k->type == PK_CACHE ? (Color){ 120, 220, 255, 230 } : (Color){ 240, 140, 255, 230 });
    }
    if (G.boss.active) {
        bool cl;
        Vector2 v = RadarPoint(Vector3Subtract(G.boss.pos, p->pos), R, range, yaw, &cl);
        UI_Circle((Vector2){ c.x + v.x, c.y + v.y }, 6.0f + sinf(G.realTime * 6.0f), (Color){ 255, 60, 50, 255 });
    }
    Vector3 obj;
    if (Region_ObjectivePos(&obj)) {
        bool cl;
        Vector2 v = RadarPoint(Vector3Subtract(obj, p->pos), R, range, yaw, &cl);
        Color oc = G.region.portalOpen ? (Color){ 120, 255, 180, 255 } : (Color){ 255, 240, 120, 255 };
        if (cl) UI_Icon(ICON_DASH, (Vector2){ c.x + v.x, c.y + v.y }, 12, oc);
        else UI_Ring((Vector2){ c.x + v.x, c.y + v.y }, 4, 6, 0, 360, oc);
    }
    /* player arrow */
    UI_Circle(c, 3.0f, (Color){ 230, 255, 240, 255 });
    UI_Line(c, (Vector2){ c.x, c.y - 9 }, 2.0f, (Color){ 230, 255, 240, 255 });
}

static void DrawObjectiveMarker(void)
{
    Vector3 obj;
    if (!Region_ObjectivePos(&obj)) return;
    Camera3D cam = G.rig.cam;
    Vector3 to = Vector3Subtract(obj, cam.position);
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    float dist = Vector3Distance(obj, G.player.pos);
    if (dist < 8.0f && !G.region.portalOpen) return;
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    Vector2 sp = GetWorldToScreen(obj, cam);
    bool behind = Vector3DotProduct(to, fwd) < 0.0f;
    float s = UI_Scale();
    Vector2 v = { sp.x / s, sp.y / s };
    float W = UI_Width(), H = UI_Height();
    float m = 50.0f;
    bool off = behind || sp.x < m * s || sp.x > sw - m * s || sp.y < m * s || sp.y > sh - m * s;
    Color oc = G.region.portalOpen ? (Color){ 120, 255, 180, 255 } : (G.boss.active ? (Color){ 255, 90, 80, 255 } : (Color){ 255, 240, 140, 255 });
    if (off) {
        Vector2 cc = { W * 0.5f, H * 0.5f };
        Vector2 d = { v.x - cc.x, v.y - cc.y };
        if (behind) { d.x = -d.x; d.y = -d.y; }
        float l = sqrtf(d.x * d.x + d.y * d.y);
        if (l < 0.01f) { d = (Vector2){ 0, 1 }; l = 1.0f; }
        d.x /= l; d.y /= l;
        float rx = W * 0.5f - m, ry = H * 0.5f - m;
        float t = fminf(fabsf(rx / (d.x + 1e-5f)), fabsf(ry / (d.y + 1e-5f)));
        v = (Vector2){ cc.x + d.x * t, cc.y + d.y * t };
        float ang = atan2f(d.y, d.x);
        Vector2 tip = { v.x + cosf(ang) * 14, v.y + sinf(ang) * 14 };
        Vector2 l1 = { v.x + cosf(ang + 2.5f) * 10, v.y + sinf(ang + 2.5f) * 10 };
        Vector2 l2 = { v.x + cosf(ang - 2.5f) * 10, v.y + sinf(ang - 2.5f) * 10 };
        DrawTriangle(UI_ToScreen(tip), UI_ToScreen(l2), UI_ToScreen(l1), oc);
        DrawTriangle(UI_ToScreen(tip), UI_ToScreen(l1), UI_ToScreen(l2), oc);
    } else {
        float pulse = 1.0f + 0.15f * sinf(G.realTime * 5.0f);
        UI_Ring(v, 9 * pulse, 11 * pulse, 0, 360, oc);
    }
    char t[32];
    snprintf(t, sizeof(t), "%d um", (int)dist);
    UI_TextCentered(t, v.x, v.y + 16, 10, oc);
}

static void DrawCrosshairAndPrompts(void)
{
    Player *p = &G.player;
    float W = UI_Width(), H = UI_Height();
    Vector2 c = { W * 0.5f, H * 0.5f };
    if (p->attach.idx >= 0) {
        /* rhythm injection ring */
        Agent *h = Agent_Get(p->attach);
        float target = 54.0f;
        float ph = p->rhythmPhase;
        float rr = target + (1.0f - ph) * 110.0f;
        bool sweet = Infection_InSweetSpot();
        UI_Ring(c, target - 3, target + 3, 0, 360, sweet ? (Color){ 140, 255, 190, 255 } : (Color){ 100, 180, 150, 180 });
        UI_Ring(c, rr - 2, rr + 2, 0, 360, (Color){ 220, 255, 235, (unsigned char)(120 + 135 * ph) });
        if (h) UI_Ring(c, target + 10, target + 16, -90, -90 + 360.0f * Sat(h->progress), (Color){ 90, 255, 150, 255 });
        if (p->rhythmFlash > 0.2f) UI_Ring(c, target - 8, target + 8, 0, 360, (Color){ 180, 255, 210, (unsigned char)(255 * p->rhythmFlash) });
        if (p->rhythmFlash < -0.2f) UI_Ring(c, target - 8, target + 8, 0, 360, (Color){ 255, 90, 80, (unsigned char)(-255 * p->rhythmFlash) });
        char t[48];
        if (p->rhythmCombo > 0) { snprintf(t, sizeof(t), "PERFECT x%d", p->rhythmCombo); UI_TextCentered(t, c.x, c.y + 80, 18, (Color){ 160, 255, 200, 255 }); }
        UI_TextCentered("hold RMB  -  tap LMB on the beat", c.x, c.y + 104, 10, (Color){ 200, 230, 215, 200 });
        if (h) { snprintf(t, sizeof(t), "%d%%", (int)(h->progress * 100.0f)); UI_TextCentered(t, c.x, c.y - 8, 16, (Color){ 220, 255, 230, 255 }); }
        return;
    }
    if (p->engulfedBy.idx >= 0) {
        float pulse = 1.0f + 0.1f * sinf(G.realTime * 14.0f);
        char t[48];
        snprintf(t, sizeof(t), "STRUGGLE!  [%s]  %d / 6", Settings_KeyName(G.settings.keys[ACT_DASH]), p->struggle);
        UI_TextCentered(t, c.x, c.y - 60, 28 * pulse, (Color){ 255, 170, 100, 255 });
        UI_Bar((Rectangle){ c.x - 120, c.y - 20, 240, 10 }, (float)p->struggle / 6.0f, (Color){ 255, 170, 100, 255 }, (Color){ 40, 20, 10, 220 }, 0.0f);
        return;
    }
    bool onEnemy = p->aimAgent.idx >= 0 || p->aimBoss;
    Color cc = onEnemy ? (Color){ 255, 110, 90, 240 } : (Color){ 220, 255, 235, 200 };
    float spread = 6.0f + (G.in.fire ? 3.0f : 0.0f);
    UI_Circle(c, 1.6f, cc);
    UI_Line((Vector2){ c.x - spread - 6, c.y }, (Vector2){ c.x - spread, c.y }, 2, cc);
    UI_Line((Vector2){ c.x + spread, c.y }, (Vector2){ c.x + spread + 6, c.y }, 2, cc);
    UI_Line((Vector2){ c.x, c.y - spread - 6 }, (Vector2){ c.x, c.y - spread }, 2, cc);
    UI_Line((Vector2){ c.x, c.y + spread }, (Vector2){ c.x, c.y + spread + 6 }, 2, cc);
    Agent *aim = Agent_Get(p->aimAgent);
    if (aim && aim->team == TEAM_IMMUNE && aim->kind != AK_BOSS_PART) {
        const EnemyDef *d = Enemies_Def(aim->kind);
        if (d) {
            char t[64];
            snprintf(t, sizeof(t), "%s%s", (aim->flags & AF_ELITE) ? "ELITE " : "", d->name);
            UI_TextCentered(t, c.x, c.y + 22, 10, (Color){ 255, 190, 170, 230 });
            UI_Bar((Rectangle){ c.x - 40, c.y + 36, 80, 4 }, aim->hp / aim->maxHp, (Color){ 255, 100, 90, 255 }, (Color){ 40, 20, 20, 200 }, 0.0f);
            if (aim->flags & AF_SHIELDED) UI_Icon(ICON_SHIELD, (Vector2){ c.x + 50, c.y + 38 }, 10, (Color){ 120, 200, 255, 255 });
        }
    }
    AgentRef hr = Infection_FindTarget();
    if (hr.idx >= 0) {
        UI_TextCentered("RMB  INFECT", c.x, c.y - 34, 14, (Color){ 140, 255, 190, (unsigned char)(180 + 70 * sinf(G.realTime * 6.0f)) });
    }
}

static void DrawPopups(void)
{
    Camera3D cam = G.rig.cam;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    float s = UI_Scale();
    for (int i = 0; i < MAX_POPUPS; i++) {
        Popup *p = &G.popups[i];
        if (!p->active) continue;
        Vector3 to = Vector3Subtract(p->pos, cam.position);
        float d = Vector3Length(to);
        if (Vector3DotProduct(to, fwd) < 0.5f || d > 120.0f) continue;
        Vector2 sp = GetWorldToScreen(p->pos, cam);
        float k = p->life / p->maxLife;
        float pop = 1.0f + 0.4f * Sat((k - 0.8f) * 5.0f);
        float size = p->size * pop * Clamp(30.0f / (d + 10.0f), 0.55f, 1.3f);
        Color c = p->color;
        c.a = (unsigned char)(255.0f * Sat(k * 2.0f));
        UI_TextCentered(p->text, sp.x / s, sp.y / s, size, (Color){ 0, 0, 0, (unsigned char)(c.a * 0.6f) });
        UI_TextCentered(p->text, sp.x / s - 1, sp.y / s - 1, size, c);
    }
}

void HUD_Draw(void)
{
    if (G.state == GS_PHOTO) return;
    DrawPopups();
    DrawObjectiveMarker();
    DrawVitals();
    DrawObjective();
    DrawAlert();
    DrawRadar();
    DrawAbilities();
    DrawCrosshairAndPrompts();
}

void HUD_DrawMessages(void)
{
    float W = UI_Width(), H = UI_Height();
    /* region title card */
    if (G.ui.regionTitleT > 0.0f && G.run.active) {
        float t = G.ui.regionTitleT;
        float a = Sat((7.0f - t) / 1.0f) * Sat(t / 1.5f);
        const RegionDef *d = Region_Def();
        UI_TextCentered(d->subtitle, W * 0.5f, H * 0.28f, 16, UI_Fade((Color){ 200, 230, 220, 255 }, a));
        UI_TextCentered(G.run.mode == MODE_OUTBREAK ? "OUTBREAK" : d->name, W * 0.5f, H * 0.28f + 24, 52, UI_Fade((Color){ 255, 255, 255, 255 }, a));
        UI_TextWrapped(d->intro, W * 0.5f - 280, H * 0.28f + 90, 560, 14, 20, UI_Fade((Color){ 200, 220, 210, 255 }, a * 0.9f));
    }
    bool live = G.state == GS_PLAY || G.state == GS_EVOLVE || G.state == GS_GAMEOVER;
    /* banner */
    if (G.ui.bannerT > 0.0f && live) {
        float t = G.ui.bannerT;
        float a = Sat((3.6f - t) * 5.0f) * Sat(t / 0.6f);
        float y = H * 0.2f;
        float grow = 1.0f + 0.08f * Sat((3.6f - t) * 4.0f);
        Color c = UI_Fade(G.ui.bannerColor, a);
        float tw = UI_Measure(G.ui.banner, 40 * grow);
        UI_Rect2((Rectangle){ W * 0.5f - tw * 0.5f - 40, y - 8, tw + 80, 2 }, UI_Fade(c, 0.5f));
        UI_TextCentered(G.ui.banner, W * 0.5f + 2, y + 2, 40 * grow, UI_Fade((Color){ 0, 0, 0, 200 }, a));
        UI_TextCentered(G.ui.banner, W * 0.5f, y, 40 * grow, c);
        UI_TextCentered(G.ui.bannerSub, W * 0.5f, y + 50, 16, UI_Fade((Color){ 230, 230, 230, 255 }, a));
    }
    /* toasts */
    float ty = 196;
    for (int i = 0; i < MAX_TOASTS; i++) {
        Toast *t = &G.toasts[i];
        if (t->life <= 0.0f) continue;
        float a = Sat(t->life / 0.6f);
        float slide = (1.0f - sToastSlide[i]) * 120.0f;
        float w = 320;
        Rectangle r = { W - w - 14 + slide, ty, w, 56 };
        UI_Panel(r, (Color){ 6, 12, 14, (unsigned char)(220 * a) }, UI_Fade(t->color, a));
        float tx = r.x + 12;
        if (sToastAch[i]) { UI_Icon(ICON_STAR, (Vector2){ r.x + 24, r.y + 28 }, 26, UI_Fade(t->color, a)); tx += 30; }
        UI_Text(t->title, tx, r.y + 9, 14, UI_Fade(t->color, a));
        UI_TextWrapped(t->text, tx, r.y + 28, w - (tx - r.x) - 10, 10, 13, UI_Fade((Color){ 220, 225, 225, 255 }, a));
        ty += 62;
    }
    /* tutorial hint */
    if (G.ui.hintT > 0.0f && G.state == GS_PLAY) {
        float a = Sat(G.ui.hintT / 0.8f) * Sat((9.0f - G.ui.hintT) * 4.0f);
        float w = 560;
        Rectangle r = { W * 0.5f - w * 0.5f, H - 196, w, 70 };
        UI_Panel(r, (Color){ 6, 14, 16, (unsigned char)(220 * a) }, UI_Fade(UI_Accent(), a * 0.7f));
        UI_Text("TIP", r.x + 14, r.y + 10, 14, UI_Fade(UI_Accent(), a));
        UI_TextWrapped(G.ui.hint, r.x + 54, r.y + 11, w - 70, 12, 16, UI_Fade((Color){ 225, 240, 235, 255 }, a));
    }
    /* float texts */
    for (int i = 0; i < 4 && live; i++) {
        if (sFloat[i].t <= 0.0f) continue;
        float a = Sat(sFloat[i].t / 0.5f);
        UI_TextCentered(sFloat[i].text, W * 0.5f, H * 0.62f + (float)i * 22.0f, 16, UI_Fade(sFloat[i].color, a));
    }
}

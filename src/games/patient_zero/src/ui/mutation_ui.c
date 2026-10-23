/*
 * mutation_ui.c - the genome editor. Five branches radiate from the core;
 * tiers move outward. Lines show prerequisites, golden dashed arcs show
 * cross-branch synergies. Drag to pan, wheel to zoom, click to mutate.
 */
#include "ui/mutation_ui.h"
#include "ui/widgets.h"
#include "ui/hud.h"
#include "systems/mutation.h"
#include "systems/evolution.h"
#include "entities/player.h"
#include "gfx/render.h"
#include "audio/sfx.h"
#include "core/game_state.h"
#include "core/input.h"
#include <stdio.h>
#include <string.h>

static Vector2 sNode[MUT_COUNT];
static bool sLabelUp[MUT_COUNT];
static bool sLayoutDone = false;
static float sBuyFlash[MUT_COUNT];
static bool sDragMoved = false;

static void Layout(void)
{
    for (int b = 0; b < BR_COUNT; b++) {
        float base = -PI * 0.5f + (float)b * 2.0f * PI / (float)BR_COUNT;
        for (int tier = 1; tier <= 4; tier++) {
            int ids[8], n = 0;
            for (int i = 0; i < MUT_COUNT; i++) if ((int)MUTATIONS[i].branch == b && MUTATIONS[i].tier == tier) ids[n++] = i;
            static const float RADII[5] = { 0.0f, 140.0f, 250.0f, 365.0f, 460.0f };
            float span = (tier == 1) ? 0.4f * (float)(n - 1) : fminf(1.0f, 0.36f * (float)(n - 1));
            for (int k = 0; k < n; k++) {
                float off = (n > 1) ? ((float)k / (float)(n - 1) - 0.5f) * span : 0.0f;
                float a = base + off;
                float r = RADII[tier] + ((k % 2) ? 36.0f : 0.0f);
                sNode[ids[k]] = (Vector2){ cosf(a) * r, sinf(a) * r };
                sLabelUp[ids[k]] = false;
            }
        }
    }
    sLayoutDone = true;
}

void MutUI_Open(void)
{
    if (!sLayoutDone) Layout();
    if (G.ui.mutZoom <= 0.1f) G.ui.mutZoom = 0.62f;
    G.ui.mutSelected = -1;
    Game_SetState(GS_MUTATION);
    Sfx_Play(SFX_UI_CLICK, 0.6f, 0.8f);
}

static Vector2 ToView(Vector2 p, Vector2 center)
{
    float z = G.ui.mutZoom;
    return (Vector2){ center.x + (p.x + G.ui.mutPan.x) * z, center.y + (p.y + G.ui.mutPan.y) * z };
}

static void DashedLine(Vector2 a, Vector2 b, float thick, Color c)
{
    float len = Vector2Distance(a, b);
    int segs = (int)(len / 10.0f);
    for (int i = 0; i < segs; i += 2) {
        Vector2 p0 = Vector2Lerp(a, b, (float)i / (float)segs);
        Vector2 p1 = Vector2Lerp(a, b, (float)(i + 1) / (float)segs);
        UI_Line(p0, p1, thick, c);
    }
}

static void StatLine(float x, float *y, const char *label, const char *val)
{
    UI_Text(label, x, *y, 12, (Color){ 170, 200, 190, 255 });
    UI_TextRight(val, x + 250, *y, 12, (Color){ 230, 255, 240, 255 });
    *y += 17;
}

void MutUI_Draw(void)
{
    if (!sLayoutDone) Layout();
    float W = UI_Width(), H = UI_Height();
    Player *p = &G.player;
    UI_Backdrop(0.55f);
    Vector2 center = { (W - 300) * 0.5f, H * 0.52f };

    /* navigation */
    float wheel = UI_Wheel();
    if (wheel != 0.0f) {
        Vector2 m = UI_Mouse();
        float z0 = G.ui.mutZoom;
        float z1 = Clamp(z0 * (wheel > 0 ? 1.12f : 0.89f), 0.45f, 1.8f);
        /* keep the point under the cursor fixed */
        Vector2 world = { (m.x - center.x) / z0 - G.ui.mutPan.x, (m.y - center.y) / z0 - G.ui.mutPan.y };
        G.ui.mutPan.x = (m.x - center.x) / z1 - world.x;
        G.ui.mutPan.y = (m.y - center.y) / z1 - world.y;
        G.ui.mutZoom = z1;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        G.ui.mutDragging = true;
        G.ui.mutDragStart = UI_Mouse();
        sDragMoved = false;
    }
    if (G.ui.mutDragging && (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT))) {
        Vector2 m = UI_Mouse();
        Vector2 d = { m.x - G.ui.mutDragStart.x, m.y - G.ui.mutDragStart.y };
        if (fabsf(d.x) + fabsf(d.y) > 4.0f || sDragMoved) {
            sDragMoved = true;
            G.ui.mutPan.x += d.x / G.ui.mutZoom;
            G.ui.mutPan.y += d.y / G.ui.mutZoom;
            G.ui.mutDragStart = m;
        }
    } else {
        G.ui.mutDragging = false;
    }
    if (IsKeyPressed(KEY_HOME)) { G.ui.mutPan = (Vector2){ 0, 0 }; G.ui.mutZoom = 0.62f; }
    float panSpeed = 400.0f * GetFrameTime() / G.ui.mutZoom;
    if (IsKeyDown(KEY_LEFT)) G.ui.mutPan.x += panSpeed;
    if (IsKeyDown(KEY_RIGHT)) G.ui.mutPan.x -= panSpeed;
    if (IsKeyDown(KEY_UP)) G.ui.mutPan.y += panSpeed;
    if (IsKeyDown(KEY_DOWN)) G.ui.mutPan.y -= panSpeed;

    float z = G.ui.mutZoom;
    float nodeR = 24.0f * z;
    float t = G.realTime;

    /* branch spokes + labels */
    for (int b = 0; b < BR_COUNT; b++) {
        float a = -PI * 0.5f + (float)b * 2.0f * PI / (float)BR_COUNT;
        Vector2 e = ToView((Vector2){ cosf(a) * 500.0f, sinf(a) * 500.0f }, center);
        UI_Line(ToView((Vector2){ 0, 0 }, center), e, 1.0f, UI_Fade(BRANCH_COLORS[b], 0.12f));
        Vector2 lbl = ToView((Vector2){ cosf(a) * 540.0f, sinf(a) * 540.0f }, center);
        UI_TextCentered(BRANCH_NAMES[b], lbl.x, lbl.y - 8, 16 * z + 4, UI_Fade(BRANCH_COLORS[b], 0.85f));
    }
    /* core */
    Vector2 c0 = ToView((Vector2){ 0, 0 }, center);
    UI_Circle(c0, 46 * z, (Color){ 10, 40, 30, 240 });
    UI_Ring(c0, 44 * z, 48 * z, 0, 360, UI_Accent());
    UI_Ring(c0, 50 * z, 54 * z, -90, -90 + 360.0f * Evolution_Progress(), (Color){ 255, 240, 140, 220 });
    UI_TextCentered(STAGES[p->stage].name, c0.x, c0.y - 6 * z, 12 * z + 2, (Color){ 220, 255, 235, 255 });

    /* prerequisite links */
    for (int i = 0; i < MUT_COUNT; i++) {
        const MutationDef *m = &MUTATIONS[i];
        Vector2 a = ToView(sNode[i], center);
        bool any = false;
        for (int k = 0; k < 2; k++) {
            if (m->req[k] < 0) continue;
            any = true;
            Vector2 bpos = ToView(sNode[m->req[k]], center);
            bool lit = G.mut.owned[m->req[k]];
            UI_Line(a, bpos, (lit ? 3.0f : 1.5f) * fmaxf(z, 0.6f), lit ? UI_Fade(BRANCH_COLORS[m->branch], G.mut.owned[i] ? 0.9f : 0.5f) : (Color){ 60, 70, 70, 160 });
        }
        if (!any) UI_Line(a, c0, 1.5f * fmaxf(z, 0.6f), G.mut.owned[i] ? UI_Fade(BRANCH_COLORS[m->branch], 0.8f) : (Color){ 60, 70, 70, 140 });
    }
    /* synergies */
    for (int s = 0; s < SYNERGY_COUNT; s++) {
        Vector2 a = ToView(sNode[SYNERGIES[s].a], center), b = ToView(sNode[SYNERGIES[s].b], center);
        bool on = Mutation_SynergyActive(s);
        DashedLine(a, b, on ? 2.5f : 1.0f, on ? (Color){ 255, 215, 90, 230 } : (Color){ 255, 215, 90, 60 });
    }
    /* nodes */
    int hover = -1;
    Vector2 mouse = UI_Mouse();
    for (int i = 0; i < MUT_COUNT; i++) {
        const MutationDef *m = &MUTATIONS[i];
        Vector2 v = ToView(sNode[i], center);
        int can = Mutation_CanBuy(i);
        bool owned = G.mut.owned[i];
        Color bc = BRANCH_COLORS[m->branch];
        bool hov = Vector2Distance(mouse, v) < nodeR + 4.0f && mouse.x < W - 310;
        if (hov) hover = i;
        float pulse = 0.5f + 0.5f * sinf(t * 4.0f + (float)i);
        if (owned) {
            UI_Circle(v, nodeR + 4, UI_Fade(bc, 0.25f));
            UI_Circle(v, nodeR, bc);
            UI_Circle(v, nodeR * 0.62f, (Color){ (unsigned char)(bc.r * 0.35f), (unsigned char)(bc.g * 0.35f), (unsigned char)(bc.b * 0.35f), 255 });
        } else {
            UI_Circle(v, nodeR, (Color){ 12, 18, 20, 240 });
            Color ring = can == 0 ? UI_Fade(bc, 0.6f + 0.4f * pulse) : (can == 4 ? UI_Fade(bc, 0.35f) : (Color){ 70, 75, 80, 220 });
            UI_Ring(v, nodeR - 3, nodeR, 0, 360, ring);
            if (can == 2 || can == 3) UI_Icon(ICON_LOCK, v, nodeR * 0.9f, (Color){ 90, 95, 100, 255 });
        }
        if (hov) UI_Ring(v, nodeR + 3, nodeR + 6, 0, 360, (Color){ 255, 255, 255, 220 });
        if (G.mut.unlockAnim[i] > 0.0f) {
            float k = 1.0f - G.mut.unlockAnim[i];
            UI_Ring(v, nodeR + k * 60.0f * z, nodeR + k * 60.0f * z + 3, 0, 360, UI_Fade(bc, G.mut.unlockAnim[i]));
            G.mut.unlockAnim[i] = fmaxf(0.0f, G.mut.unlockAnim[i] - GetFrameTime() * 1.2f);
        }
        static const char *TIERS[5] = { "", "I", "II", "III", "IV" };
        if (owned || can == 0 || can == 4)
            UI_TextCentered(TIERS[m->tier], v.x, v.y - 6 * fmaxf(z, 0.6f), 12 * fmaxf(z, 0.6f), owned ? (Color){ 240, 255, 250, 255 } : (Color){ 170, 180, 180, 255 });
        if (z > 0.45f) {
            float fs = 10.0f * z + 4.0f;
            float ly = sLabelUp[i] ? v.y - nodeR - fs * 2.2f : v.y + nodeR + 3.0f;
            UI_TextCentered(m->name, v.x, ly, fs, owned ? bc : (Color){ 185, 195, 195, 230 });
            if (!owned) {
                char c[16];
                snprintf(c, sizeof(c), "%d GM", m->cost);
                UI_TextCentered(c, v.x, ly + fs + 1.0f, fs * 0.85f, p->gm >= m->cost ? (Color){ 140, 220, 255, 255 } : (Color){ 255, 110, 100, 220 });
            }
        }
        (void)sBuyFlash;
    }

    /* side panel */
    float px = W - 300;
    UI_Panel((Rectangle){ px, 14, 286, H - 28 }, (Color){ 6, 12, 14, 235 }, UI_Fade(UI_Accent(), 0.4f));
    UI_Text("GENOME", px + 16, 26, 28, (Color){ 220, 255, 235, 255 });
    char buf[96];
    UI_Icon(ICON_DNA, (Vector2){ px + 26, 76 }, 22, (Color){ 120, 220, 255, 255 });
    snprintf(buf, sizeof(buf), "%d Genetic Material", p->gm);
    UI_Text(buf, px + 44, 68, 16, (Color){ 170, 235, 255, 255 });
    if (p->stage < STAGE_COUNT - 1) snprintf(buf, sizeof(buf), "Next form: %s  (%d / %d EP)", STAGES[p->stage + 1].name, (int)p->ep, (int)STAGES[p->stage + 1].epThreshold);
    else snprintf(buf, sizeof(buf), "Final form reached");
    UI_Text(buf, px + 16, 94, 10, (Color){ 255, 240, 160, 230 });

    float y = 118;
    PlayerStats *s = &G.stats;
    StatLine(px + 16, &y, "Swim speed", TextFormat("%.1f um/s", s->speed));
    StatLine(px + 16, &y, "Max biomass", TextFormat("%d", (int)s->maxBiomass));
    StatLine(px + 16, &y, "Armour", TextFormat("%d%%", (int)(s->armor * 100.0f)));
    StatLine(px + 16, &y, "Toxin damage", TextFormat("%.1f x%d", s->toxinDamage, s->toxinCount));
    StatLine(px + 16, &y, "Fire rate", TextFormat("%.1f /s", 1.0f / s->toxinInterval));
    StatLine(px + 16, &y, "Lysis burst", TextFormat("%d dmg, r%.0f", (int)s->burstDamage, s->burstRadius));
    StatLine(px + 16, &y, "Contact lysis", TextFormat("%d /s", (int)s->contactDps));
    StatLine(px + 16, &y, "Infection speed", TextFormat("x%.2f", s->infectSpeed));
    StatLine(px + 16, &y, "Virions", TextFormat("%d max, %.0f dmg", s->minionMax, s->minionDamage));
    StatLine(px + 16, &y, "Detection range", TextFormat("x%.2f", s->detectMul));
    StatLine(px + 16, &y, "Adaptation rate", TextFormat("x%.2f", s->adaptMul));

    y += 8;
    UI_Text("SYNERGIES", px + 16, y, 14, (Color){ 255, 215, 90, 255 });
    y += 20;
    for (int i = 0; i < SYNERGY_COUNT; i++) {
        bool on = Mutation_SynergyActive(i);
        Color c = on ? (Color){ 255, 220, 120, 255 } : (Color){ 120, 120, 110, 255 };
        UI_Text(SYNERGIES[i].name, px + 16, y, 12, c);
        snprintf(buf, sizeof(buf), "%s + %s", MUTATIONS[SYNERGIES[i].a].name, MUTATIONS[SYNERGIES[i].b].name);
        UI_Text(buf, px + 16, y + 13, 9, UI_Fade(c, 0.8f));
        Rectangle r = { px + 10, y - 2, 270, 26 };
        if (UI_Hover(r)) UI_Tooltip(SYNERGIES[i].name, SYNERGIES[i].desc, (Color){ 255, 215, 90, 255 });
        y += 28;
    }

    int refund = Mutation_RespecRefund() - Mutation_RespecCost();
    if (refund < 0) refund = 0;
    snprintf(buf, sizeof(buf), "RESPEC  (+%d GM)", refund);
    if (UI_Button(40, (Rectangle){ px + 16, H - 112, 254, 36 }, buf, G.mut.gmSpent > 0)) {
        if (Mutation_Respec()) {
            Player_RecomputeStats();
            Render_RebuildPlayer();
            HUD_FloatText("Genome reset - 70% of spent GM refunded (minus respec fee)", (Color){ 200, 230, 255, 255 });
        }
    }
    if (UI_Hover((Rectangle){ px + 16, H - 112, 254, 36 })) UI_Tooltip("Respec", "Dissolve every mutation and recover 70% of the Genetic Material spent. Each respec costs a little more.", UI_Accent());
    if (UI_Button(41, (Rectangle){ px + 16, H - 68, 254, 40 }, "RETURN TO THE BODY", true) || (Input_Back() && !G.ui.seedEditing)) {
        Game_SetState(GS_PLAY);
        Sfx_Play(SFX_UI_BACK, 0.6f, 1.0f);
        return;
    }
    UI_Text("drag to pan  -  wheel to zoom  -  Home to recentre", 18, H - 26, 10, (Color){ 150, 170, 165, 200 });

    /* hover tooltip + purchase */
    if (hover >= 0) {
        const MutationDef *m = &MUTATIONS[hover];
        int can = Mutation_CanBuy(hover);
        char body[420];
        int n = snprintf(body, sizeof(body), "%s  -  Tier %d  -  %d GM\n%s", BRANCH_NAMES[m->branch], m->tier, m->cost, m->desc);
        if (m->tradeoff && n > 0 && n < (int)sizeof(body)) n += snprintf(body + n, sizeof(body) - (size_t)n, "\nTrade-off: %s", m->tradeoff);
        if (m->minStage > 0 && n > 0 && n < (int)sizeof(body)) n += snprintf(body + n, sizeof(body) - (size_t)n, "\nRequires stage: %s", STAGES[m->minStage].name);
        for (int k = 0; k < 2 && n > 0 && n < (int)sizeof(body); k++)
            if (m->req[k] >= 0) n += snprintf(body + n, sizeof(body) - (size_t)n, "\nRequires: %s", MUTATIONS[m->req[k]].name);
        if (n > 0 && n < (int)sizeof(body)) snprintf(body + n, sizeof(body) - (size_t)n, "\n\n%s", Mutation_Reason(can));
        UI_Tooltip(m->name, body, BRANCH_COLORS[m->branch]);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !sDragMoved) {
            if (can == 0) {
                Mutation_Buy(hover);
                Player_RecomputeStats();
                Render_RebuildPlayer();
                HUD_Flash((Color){ 180, 255, 220, 255 }, 0.25f);
            } else if (can != 1) {
                Sfx_Play(SFX_UI_DENY, 0.6f, 1.0f);
            }
        }
    }
}

/*
 * menu.c - every full-screen menu and overlay outside the core HUD.
 */
#include "ui/menu.h"
#include "ui/widgets.h"
#include "ui/hud.h"
#include "ui/mutation_ui.h"
#include "ui/bestiary_ui.h"
#include "systems/evolution.h"
#include "entities/bosses.h"
#include "world/region.h"
#include "core/game_state.h"
#include "core/settings.h"
#include "core/input.h"
#include "core/save.h"
#include "core/rng.h"
#include "audio/sfx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int sNgPlus = 0;

static void Title(float cx, float y, float size)
{
    const char *t = "PATIENT ZERO";
    float w = UI_Measure(t, size);
    float x = cx - w * 0.5f;
    float time = G.realTime;
    /* bioluminescent glow layers */
    for (int k = 4; k >= 1; k--) {
        float o = (float)k * 1.6f;
        Color g = { 60, 255, 170, (unsigned char)(26 / k + 6) };
        UI_Text(t, x - o, y, size, g);
        UI_Text(t, x + o, y, size, g);
        UI_Text(t, x, y - o, size, g);
        UI_Text(t, x, y + o, size, g);
    }
    /* letters drift like cells in suspension */
    char ch[2] = { 0, 0 };
    float cxp = x;
    for (int i = 0; t[i]; i++) {
        ch[0] = t[i];
        float dy = sinf(time * 1.3f + (float)i * 0.7f) * size * 0.04f;
        float glitch = (sinf(time * 7.0f + (float)i * 13.0f) > 0.995f) ? 3.0f : 0.0f;
        UI_Text(ch, cxp + glitch, y + dy, size, (Color){ 225, 255, 238, 255 });
        cxp += UI_Measure(ch, size) + size / 10.0f;
    }
}

void Menu_DrawMain(void)
{
    float W = UI_Width(), H = UI_Height();
    DrawRectangleGradientH(0, 0, (int)(W * 0.55f * UI_Scale()), GetScreenHeight(), (Color){ 0, 6, 6, 210 }, (Color){ 0, 0, 0, 0 });
    Title(330, 70, 64);
    UI_Text("a microbial survival odyssey", 112, 142, 16, (Color){ 150, 210, 190, 220 });

    float x = 70, y = 200, w = 420, h = 58, gap = 66;
    int id = 0;
    if (G.profile.hasRun) {
        char sub[96];
        RunSave *r = &G.profile.run;
        snprintf(sub, sizeof(sub), "%s  -  %s  -  %s%s", r->mode == MODE_OUTBREAK ? "Outbreak" : REGIONS[r->regionIndex].name, STAGES[r->stage].name,
                 DIFFICULTIES[r->difficulty].name, r->ngPlus > 0 ? TextFormat("  NG+%d", r->ngPlus) : "");
        if (UI_ButtonBig(id, (Rectangle){ x, y, w, h }, "CONTINUE", sub, true)) Game_ContinueRun();
        y += gap;
    }
    id++;
    if (UI_ButtonBig(id++, (Rectangle){ x, y, w, h }, "NEW INFECTION", "Begin a new campaign through the body", true)) {
        G.ui.newGameMode = MODE_CAMPAIGN;
        G.ui.newGameDiff = G.settings.difficulty;
        Game_SetState(GS_NEWGAME);
    }
    y += gap;
    bool ob = G.profile.outbreakUnlocked;
    if (UI_ButtonBig(id++, (Rectangle){ x, y, w, h }, "OUTBREAK", ob ? TextFormat("Endless waves  -  best wave %d", G.profile.bestWave) : "Locked: complete the campaign", ob)) {
        G.ui.newGameMode = MODE_OUTBREAK;
        G.ui.newGameDiff = G.settings.difficulty;
        Game_SetState(GS_NEWGAME);
    }
    y += gap;
    if (UI_ButtonBig(id++, (Rectangle){ x, y, w, h }, "CODEX", TextFormat("%d / %d specimens", Codex_Count(), CX_COUNT), true)) Game_OpenOverlay(GS_BESTIARY);
    y += gap;
    if (UI_ButtonBig(id++, (Rectangle){ x, y, w, h }, "SETTINGS", "Video, audio, controls, accessibility", true)) Game_OpenOverlay(GS_SETTINGS);
    y += gap;
    if (UI_ButtonBig(id++, (Rectangle){ x, y, w * 0.48f, h }, "CREDITS", NULL, true)) Game_SetState(GS_CREDITS);
    if (UI_ButtonBig(id++, (Rectangle){ x + w * 0.52f, y, w * 0.48f, h }, "QUIT", NULL, true)) G.quit = true;

    /* profile panel */
    float px = W - 320, py = H - 214;
    UI_Panel((Rectangle){ px, py, 300, 196 }, (Color){ 4, 10, 12, 190 }, UI_Fade(UI_Accent(), 0.3f));
    UI_Text("PROFILE", px + 16, py + 12, 14, UI_Accent());
    int ach = 0;
    for (int i = 0; i < ACH_COUNT; i++) ach += G.profile.achievements[i] ? 1 : 0;
    float ly = py + 36;
    UI_Text(TextFormat("Campaign victories     %d", G.profile.victories), px + 16, ly, 12, (Color){ 210, 230, 225, 255 }); ly += 18;
    UI_Text(TextFormat("Best region reached    %d / %d", G.profile.bestRegion + 1, REGION_COUNT), px + 16, ly, 12, (Color){ 210, 230, 225, 255 }); ly += 18;
    UI_Text(TextFormat("Cells infected         %d", G.profile.totalInfections), px + 16, ly, 12, (Color){ 210, 230, 225, 255 }); ly += 18;
    UI_Text(TextFormat("Immune cells lysed     %d", G.profile.totalKills), px + 16, ly, 12, (Color){ 210, 230, 225, 255 }); ly += 18;
    UI_Text(TextFormat("Achievements           %d / %d", ach, ACH_COUNT), px + 16, ly, 12, (Color){ 255, 220, 120, 255 }); ly += 20;
    for (int i = 0; i < ACH_COUNT; i++) {
        Vector2 c = { px + 26 + (float)(i % 7) * 38, ly + 12 + (float)(i / 7) * 26 };
        bool got = G.profile.achievements[i];
        UI_Icon(ICON_STAR, c, 20, got ? (Color){ 255, 215, 90, 255 } : (Color){ 50, 55, 55, 255 });
        if (Vector2Distance(UI_Mouse(), c) < 12) UI_Tooltip(ACHIEVEMENTS[i].name, ACHIEVEMENTS[i].desc, got ? (Color){ 255, 215, 90, 255 } : (Color){ 150, 150, 150, 255 });
    }
    UI_Text(TextFormat("v%s  -  raylib %s  -  daily seed %u", PZ_VERSION, RAYLIB_VERSION, Rng_DailySeed()), 16, H - 22, 10, (Color){ 120, 150, 140, 200 });
    if (Input_Back() && G.ui.clickCooldown <= 0.0f) { /* Esc on the main menu does nothing destructive */ }
}

void Menu_DrawNewGame(void)
{
    float W = UI_Width(), H = UI_Height();
    UI_Backdrop(0.5f);
    float pw = 900, ph = 560;
    float x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
    UI_Panel((Rectangle){ x, y, pw, ph }, (Color){ 6, 12, 14, 235 }, UI_Fade(UI_Accent(), 0.5f));
    bool outbreak = G.ui.newGameMode == MODE_OUTBREAK;
    UI_Text(outbreak ? "OUTBREAK" : "NEW INFECTION", x + 28, y + 20, 30, (Color){ 230, 255, 240, 255 });
    UI_Text(outbreak ? "Endless waves in a random region. Infect, evolve, survive. Every fifth wave a guardian joins the fight."
                     : "Spread through five regions of the body: Bloodstream, Lungs, Gut, Lymph Nodes and Brain.",
            x + 30, y + 58, 12, (Color){ 170, 210, 195, 255 });

    /* difficulty cards */
    static const char *DESC[3] = {
        "Gentler immune response. Enemies hit softer, adapt slower, and you regenerate faster.",
        "The intended experience. The immune system learns and escalates.",
        "A hyper-vigilant body. More, tougher enemies that adapt quickly. Regeneration is reduced.",
    };
    for (int d = 0; d < DIFF_COUNT; d++) {
        Rectangle r = { x + 28 + (float)d * 286, y + 96, 272, 150 };
        bool sel = G.ui.newGameDiff == d;
        bool hov = UI_Hover(r);
        Color acc = d == 0 ? (Color){ 120, 220, 255, 255 } : (d == 1 ? UI_Accent() : (Color){ 255, 110, 90, 255 });
        UI_Panel(r, sel ? UI_Fade(acc, 0.18f) : (Color){ 10, 18, 20, 220 }, sel ? acc : UI_Fade(acc, hov ? 0.6f : 0.25f));
        UI_Text(DIFFICULTIES[d].name, r.x + 16, r.y + 14, 24, sel ? acc : (Color){ 210, 230, 225, 255 });
        UI_TextWrapped(DESC[d], r.x + 16, r.y + 50, r.width - 32, 12, 17, (Color){ 190, 210, 205, 255 });
        if (hov && UI_MousePressed()) { G.ui.newGameDiff = d; Sfx_Play(SFX_UI_CLICK, 0.6f, 1.0f); }
    }
    /* seed */
    float sy = y + 270;
    UI_Text("GENOME SEED", x + 30, sy, 14, UI_Accent());
    static const char *SM[3] = { "Random", "Daily challenge", "Custom" };
    for (int i = 0; i < 3; i++) {
        Rectangle r = { x + 30 + (float)i * 170, sy + 24, 160, 34 };
        if (G.ui.newGameSeedMode == i) UI_Panel(r, UI_Fade(UI_Accent(), 0.25f), UI_Accent());
        if (UI_Button(10 + i, r, SM[i], true)) { G.ui.newGameSeedMode = i; G.ui.seedEditing = (i == 2); }
    }
    if (G.ui.newGameSeedMode == 1) UI_Text(TextFormat("Today's seed: %u  (same body for everyone today)", Rng_DailySeed()), x + 30, sy + 70, 12, (Color){ 200, 220, 210, 255 });
    if (G.ui.newGameSeedMode == 2) {
        Rectangle r = { x + 30, sy + 66, 330, 34 };
        UI_Panel(r, (Color){ 12, 20, 22, 255 }, G.ui.seedEditing ? UI_Accent() : (Color){ 80, 90, 90, 255 });
        bool caret = fmodf(G.realTime, 1.0f) < 0.5f && G.ui.seedEditing;
        UI_Text(TextFormat("%s%s", G.ui.seedText, caret ? "_" : ""), r.x + 12, r.y + 9, 16, (Color){ 230, 255, 240, 255 });
        if (UI_Hover(r) && UI_MousePressed()) G.ui.seedEditing = true;
        if (G.ui.seedEditing) {
            int c;
            while ((c = GetCharPressed()) > 0) {
                size_t len = strlen(G.ui.seedText);
                if (c >= '0' && c <= '9' && len < 10) { G.ui.seedText[len] = (char)c; G.ui.seedText[len + 1] = 0; }
            }
            if (IsKeyPressed(KEY_BACKSPACE)) { size_t len = strlen(G.ui.seedText); if (len > 0) G.ui.seedText[len - 1] = 0; }
        }
        UI_Text("digits only", r.x + r.width + 12, r.y + 11, 10, (Color){ 140, 160, 155, 255 });
    }
    /* NG+ */
    if (G.profile.ngPlusUnlocked > 0 && !outbreak) {
        int maxNg = G.profile.ngPlusUnlocked;
        if (sNgPlus > maxNg) sNgPlus = maxNg;
        const char *opts[8] = { "Off", "NG+1", "NG+2", "NG+3", "NG+4", "NG+5", "NG+6", "NG+7" };
        int count = maxNg + 1 > 8 ? 8 : maxNg + 1;
        UI_Selector(14, (Rectangle){ x + 560, sy + 24, 310, 34 }, "New Game+", &sNgPlus, opts, count);
        UI_Text("Tougher, faster immune systems; elites everywhere.", x + 560, sy + 66, 10, (Color){ 170, 190, 185, 255 });
    }
    if (UI_Button(15, (Rectangle){ x + pw - 400, y + ph - 64, 170, 44 }, "BACK", true) || (Input_Back() && !G.ui.seedEditing)) {
        Sfx_Play(SFX_UI_BACK, 0.6f, 1.0f);
        Game_SetState(GS_MENU);
        return;
    }
    if (G.ui.seedEditing && IsKeyPressed(KEY_ESCAPE)) G.ui.seedEditing = false;
    if (UI_Button(16, (Rectangle){ x + pw - 210, y + ph - 64, 180, 44 }, "INFECT", true) || (IsKeyPressed(KEY_ENTER) && !G.ui.seedEditing)) {
        uint32_t seed;
        bool daily = false;
        if (G.ui.newGameSeedMode == 1) { seed = Rng_DailySeed(); daily = true; }
        else if (G.ui.newGameSeedMode == 2 && G.ui.seedText[0]) seed = (uint32_t)strtoul(G.ui.seedText, NULL, 10);
        else seed = Rng_Hash32((uint32_t)(GetTime() * 1000.0) ^ (uint32_t)GetRandomValue(0, 0x7FFFFFFF));
        G.settings.difficulty = (Difficulty)G.ui.newGameDiff;
        Game_StartRun((RunMode)G.ui.newGameMode, (Difficulty)G.ui.newGameDiff, seed, daily, outbreak ? 0 : sNgPlus);
    }
}

void Menu_DrawPause(void)
{
    float W = UI_Width(), H = UI_Height();
    UI_Backdrop(0.55f);
    float x = W * 0.5f - 200, y = H * 0.5f - 250;
    UI_TextCentered("PAUSED", W * 0.5f, y, 44, (Color){ 230, 255, 240, 255 });
    y += 70;
    int id = 0;
    float bw = 400, bh = 42, g = 52;
    if (UI_Button(id++, (Rectangle){ x, y, bw, bh }, "RESUME", true) || Input_Back()) { Game_SetState(GS_PLAY); return; }
    y += g;
    if (UI_Button(id++, (Rectangle){ x, y, bw, bh }, "GENOME", true)) { MutUI_Open(); return; }
    y += g;
    if (UI_Button(id++, (Rectangle){ x, y, bw, bh }, "CODEX", true)) Game_OpenOverlay(GS_BESTIARY);
    y += g;
    if (UI_Button(id++, (Rectangle){ x, y, bw, bh }, "SETTINGS", true)) Game_OpenOverlay(GS_SETTINGS);
    y += g;
    if (UI_Button(id++, (Rectangle){ x, y, bw, bh }, "PHOTO MODE", true)) { Game_SetState(GS_PHOTO); return; }
    y += g;
    if (UI_Button(id++, (Rectangle){ x, y, bw, bh }, G.run.mode == MODE_CAMPAIGN ? "SAVE & QUIT TO MENU" : "ABANDON OUTBREAK", true)) { Game_ReturnToMenu(); return; }
    y += g;
    if (UI_Button(id++, (Rectangle){ x, y, bw, bh }, "QUIT TO DESKTOP", true)) G.quit = true;
    RunStats *s = &G.run.stats;
    UI_TextCentered(TextFormat("%s  -  %s  -  seed %u%s", G.run.mode == MODE_OUTBREAK ? "Outbreak" : Region_Def()->name, DIFFICULTIES[G.run.difficulty].name, G.run.seed, G.run.daily ? " (daily)" : ""),
                    W * 0.5f, H - 60, 12, (Color){ 170, 200, 190, 255 });
    UI_TextCentered(TextFormat("time %02d:%02d   infections %d   kills %d   GM earned %d", (int)s->time / 60, (int)s->time % 60, s->infections, s->killsTotal, s->gmEarned),
                    W * 0.5f, H - 40, 12, (Color){ 170, 200, 190, 255 });
}

void Menu_DrawLoading(void)
{
    float W = UI_Width(), H = UI_Height();
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){ 2, 6, 7, 255 });
    int ri = G.run.regionIndex;
    if (ri < 0 || ri >= REGION_COUNT) ri = 0;
    UI_TextCentered("SPREADING TO", W * 0.5f, H * 0.4f, 14, (Color){ 150, 200, 185, 255 });
    UI_TextCentered(REGIONS[ri].name, W * 0.5f, H * 0.4f + 22, 40, (Color){ 230, 255, 240, 255 });
    float t = G.realTime;
    for (int i = 0; i < 8; i++) {
        float a = t * 3.0f + (float)i * 0.785f;
        UI_Circle((Vector2){ W * 0.5f + cosf(a) * 26, H * 0.62f + sinf(a) * 26 }, 3.0f + 2.0f * (float)i / 8.0f, UI_Fade(UI_Accent(), (float)i / 8.0f));
    }
    static const char *TIPS[6] = {
        "Tap LMB exactly as the pulse ring meets the membrane - perfect beats inject far faster.",
        "The immune system remembers your favourite tactics. Mix toxins, swarms, dashes and stealth.",
        "Breaking line of sight lets Alert decay. Hide behind tissue folds and in side branches.",
        "Side branches end in secret chambers holding genetic caches and mutagenic pods.",
        "NK cells cleanse infected cells. Guard your factories, or take Latent Infection.",
        "Bigger forms can't be engulfed by macrophages - but they're easier to spot.",
    };
    UI_TextCentered(TIPS[(int)(G.run.seed + (uint32_t)ri) % 6], W * 0.5f, H * 0.75f, 12, (Color){ 170, 200, 190, 220 });
}

static void StatRow(float x, float *y, const char *label, const char *value)
{
    UI_Text(label, x, *y, 14, (Color){ 170, 200, 190, 255 });
    UI_TextRight(value, x + 380, *y, 14, (Color){ 235, 255, 245, 255 });
    *y += 22;
}

void Menu_DrawRegionClear(void)
{
    float W = UI_Width(), H = UI_Height();
    UI_Backdrop(0.6f);
    RegionState *rg = &G.region;
    RunStats *s = &G.run.stats;
    float x = W * 0.5f - 200, y = H * 0.5f - 230;
    UI_TextCentered("REGION INFECTED", W * 0.5f, y, 40, (Color){ 140, 255, 190, 255 });
    UI_TextCentered(Region_Def()->name, W * 0.5f, y + 48, 18, (Color){ 200, 230, 215, 255 });
    y += 96;
    StatRow(x, &y, "Host cells infected", TextFormat("%d / %d", rg->hostInfected, rg->hostTotal));
    StatRow(x, &y, "Peak alert", TextFormat("%d%%", (int)rg->peakAlert));
    StatRow(x, &y, "Total kills", TextFormat("%d", s->killsTotal));
    StatRow(x, &y, "Run time", TextFormat("%02d:%02d", (int)s->time / 60, (int)s->time % 60));
    StatRow(x, &y, "Evolution", STAGES[G.player.stage].name);
    StatRow(x, &y, "Immune adaptations", TextFormat("%d", G.immune.adaptations));
    if (rg->peakAlert < 40.0f) { UI_TextCentered("STEALTH BONUS  +40 GM", W * 0.5f, y + 6, 16, (Color){ 170, 160, 255, 255 }); y += 26; }
    y += 20;
    UI_TextWrapped(TextFormat("The immune system will remember %d%% of what it learned about you.", (int)(IMMUNE_CARRY * 100.0f)), x, y, 400, 12, 16, (Color){ 230, 170, 150, 255 });
    bool last = G.run.regionIndex >= REGION_COUNT - 1;
    if (UI_Button(0, (Rectangle){ W * 0.5f - 160, H - 120, 320, 48 }, last ? "FINAL VICTORY" : TextFormat("SPREAD TO %s", REGIONS[G.run.regionIndex + 1].name), true) || IsKeyPressed(KEY_ENTER))
        Game_NextRegion();
}

void Menu_DrawGameOver(void)
{
    float W = UI_Width(), H = UI_Height();
    UI_Backdrop(0.65f);
    RunStats *s = &G.run.stats;
    float x = W * 0.5f - 200, y = H * 0.5f - 230;
    UI_TextCentered("THE BODY RECOVERED", W * 0.5f, y, 44, (Color){ 255, 110, 100, 255 });
    UI_TextCentered("Your infection has been cleared.", W * 0.5f, y + 52, 16, (Color){ 220, 190, 185, 255 });
    y += 100;
    StatRow(x, &y, "Region", G.run.mode == MODE_OUTBREAK ? TextFormat("Outbreak wave %d", G.run.wave) : Region_Def()->name);
    StatRow(x, &y, "Time survived", TextFormat("%02d:%02d", (int)s->time / 60, (int)s->time % 60));
    StatRow(x, &y, "Cells infected", TextFormat("%d", s->infections));
    StatRow(x, &y, "Immune cells lysed", TextFormat("%d", s->killsTotal));
    StatRow(x, &y, "Damage dealt", TextFormat("%d", (int)s->damageDealt));
    StatRow(x, &y, "Peak swarm", TextFormat("%d virions", s->maxMinions));
    StatRow(x, &y, "Best injection combo", TextFormat("x%d", s->bestCombo));
    StatRow(x, &y, "Mutations", TextFormat("%d", s->mutations));
    bool canRetry = G.run.mode == MODE_CAMPAIGN && G.profile.hasRun;
    if (UI_Button(0, (Rectangle){ W * 0.5f - 330, H - 110, 310, 48 }, "RETRY REGION", canRetry) && canRetry) { Game_RetryRegion(); return; }
    if (UI_Button(1, (Rectangle){ W * 0.5f + 20, H - 110, 310, 48 }, "MAIN MENU", true)) Game_AbandonRun();
}

void Menu_DrawVictory(void)
{
    float W = UI_Width(), H = UI_Height();
    UI_Backdrop(0.5f);
    RunStats *s = &G.run.stats;
    float t = G.realTime;
    for (int i = 0; i < 40; i++) {
        float a = (float)i * 0.157f + t * 0.1f;
        float r = 200.0f + 40.0f * sinf(t + (float)i);
        UI_Circle((Vector2){ W * 0.5f + cosf(a) * r * 1.6f, H * 0.36f + sinf(a) * r * 0.5f }, 2.0f, (Color){ 120, 255, 180, 120 });
    }
    UI_TextCentered("PANDEMIC", W * 0.5f, 70, 72, (Color){ 140, 255, 190, 255 });
    UI_TextCentered("The body belongs to you now.", W * 0.5f, 150, 18, (Color){ 220, 240, 230, 255 });
    float x = W * 0.5f - 200, y = 200;
    StatRow(x, &y, "Total time", TextFormat("%02d:%02d:%02d", (int)s->time / 3600, ((int)s->time / 60) % 60, (int)s->time % 60));
    StatRow(x, &y, "Cells infected", TextFormat("%d", s->infections));
    StatRow(x, &y, "Immune cells lysed", TextFormat("%d", s->killsTotal));
    StatRow(x, &y, "Guardians destroyed", TextFormat("%d", s->bossKills));
    StatRow(x, &y, "Mutations acquired", TextFormat("%d", s->mutations));
    StatRow(x, &y, "Genetic material earned", TextFormat("%d", s->gmEarned));
    StatRow(x, &y, "Peak swarm", TextFormat("%d", s->maxMinions));
    StatRow(x, &y, "Deaths", TextFormat("%d", s->deaths));
    StatRow(x, &y, "Difficulty", TextFormat("%s%s", DIFFICULTIES[G.run.difficulty].name, G.run.ngPlus ? TextFormat("  NG+%d", G.run.ngPlus) : ""));
    UI_TextCentered("UNLOCKED:  Outbreak mode  -  New Game+", W * 0.5f, y + 14, 16, (Color){ 255, 215, 90, 255 });
    if (UI_Button(0, (Rectangle){ W * 0.5f - 330, H - 100, 310, 48 }, "CREDITS", true)) { Game_SetState(GS_CREDITS); return; }
    if (UI_Button(1, (Rectangle){ W * 0.5f + 20, H - 100, 310, 48 }, "MAIN MENU", true)) Game_AbandonRun();
}

void Menu_DrawCredits(void)
{
    float W = UI_Width(), H = UI_Height();
    UI_Backdrop(0.7f);
    static float scroll = 0.0f;
    scroll += GetFrameTime() * 32.0f;
    static const char *LINES[] = {
        "PATIENT ZERO", "", "A microbial survival odyssey", "", "", "DESIGN, CODE, ART, SOUND", "Procedurally generated - zero external assets", "", "",
        "BUILT WITH", "C11 and raylib", "", "", "TECHNOLOGY", "Signed-distance tissue volumes meshed with surface nets", "Instanced rendering of thousands of cells",
        "HDR bloom, depth of field and colour grading", "Real-time synthesised adaptive score", "Steering, flocking and a learning immune system", "", "",
        "SCIENCE NOTES", "Every cell type is modelled loosely on real microscopy:", "biconcave erythrocytes, lobed neutrophil nuclei,", "perforin-wielding NK cells, Y-shaped IgG antibodies,",
        "peristalsis, mucociliary clearance, complement cascades.", "", "", "THANK YOU FOR PLAYING", "", "Wash your hands.", NULL };
    float y = H - scroll;
    int n = 0;
    for (int i = 0; LINES[i]; i++, n++) {
        bool head = (i == 0);
        float size = head ? 48 : (LINES[i][0] && LINES[i][1] && strchr(LINES[i], ' ') == NULL ? 20 : 16);
        UI_TextCentered(LINES[i], W * 0.5f, y, size, head ? (Color){ 150, 255, 200, 255 } : (Color){ 210, 230, 222, 255 });
        y += size + 14;
    }
    if (y < -40) scroll = 0.0f;
    if (UI_Button(0, (Rectangle){ W - 200, H - 70, 170, 44 }, "BACK", true) || Input_Back() || IsKeyPressed(KEY_ENTER)) {
        scroll = 0.0f;
        Game_SetState(G.run.active ? GS_VICTORY : GS_MENU);
        if (G.run.active) Game_AbandonRun();
    }
}

void Menu_DrawBossIntro(void)
{
    float W = UI_Width(), H = UI_Height();
    if (!G.boss.active) return;
    const BossDef *d = &BOSSES[G.boss.type];
    float t = G.ui.evolveT;
    float a = Sat(t / 0.6f);
    UI_TextCentered(d->title, W * 0.5f, H * 0.70f, 16, UI_Fade((Color){ 230, 200, 190, 255 }, a));
    UI_TextCentered(d->name, W * 0.5f, H * 0.70f + 22, 46, UI_Fade((Color){ 255, 120, 100, 255 }, a));
    UI_TextCentered("SPACE / click to skip", W * 0.5f, H - 40, 10, UI_Fade((Color){ 200, 200, 200, 200 }, a));
}

void Menu_DrawEvolve(void)
{
    float W = UI_Width(), H = UI_Height();
    float t = G.ui.evolveT;
    float dur = Evolution_Duration();
    int from = G.ui.evolveFrom;
    int to = from + 1 < STAGE_COUNT ? from + 1 : from;
    float a = Sat(t / 0.5f) * Sat((dur - t) / 0.5f);
    UI_TextCentered("METAMORPHOSIS", W * 0.5f, H * 0.16f, 18, UI_Fade((Color){ 200, 255, 220, 255 }, a));
    const char *name = t < 2.0f ? STAGES[from].name : STAGES[to].name;
    UI_TextCentered(name, W * 0.5f, H * 0.16f + 26, 44, UI_Fade((Color){ 140, 255, 200, 255 }, a));
    if (t > 2.0f) UI_TextWrapped(STAGES[to].desc, W * 0.5f - 260, H * 0.80f, 520, 14, 19, UI_Fade((Color){ 220, 240, 230, 255 }, a));
}

void Menu_DrawPhoto(void)
{
    float H = UI_Height();
    if (fmodf(G.realTime, 8.0f) < 6.0f || IsKeyDown(KEY_H))
        UI_Text("PHOTO MODE   WASD / Space / Ctrl fly  -  Shift fast  -  wheel: focus  -  F12 capture  -  H help  -  Esc exit", 16, H - 26, 10, (Color){ 220, 240, 230, 160 });
}

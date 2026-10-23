/*
 * game.c - top-level state machine, run lifecycle and the frame loop.
 *
 * Simulation runs at a fixed 60 Hz (SIM_DT) from an accumulator scaled by
 * time effects (hit-stop freezes, slow-mo stretches). Rendering interpolates
 * agents between their previous and current tick with G.alpha, the camera
 * updates every rendered frame for smoothness.
 */
#include "core/game_state.h"
#include "core/input.h"
#include "core/save.h"
#include "core/settings.h"
#include "core/rng.h"
#include "core/autotest.h"
#include "gfx/shaders.h"
#include "gfx/render.h"
#include "gfx/postfx.h"
#include "gfx/particles.h"
#include "gfx/lights.h"
#include "gfx/camera.h"
#include "world/sdf_cave.h"
#include "world/flow_field.h"
#include "world/region.h"
#include "world/hazards.h"
#include "world/events.h"
#include "entities/agents.h"
#include "entities/player.h"
#include "entities/enemies.h"
#include "entities/bosses.h"
#include "entities/projectiles.h"
#include "entities/pickups.h"
#include "entities/minions.h"
#include "systems/spatial_hash.h"
#include "systems/immune_memory.h"
#include "systems/mutation.h"
#include "systems/evolution.h"
#include "audio/sfx.h"
#include "audio/music.h"
#include "ui/widgets.h"
#include "ui/hud.h"
#include "ui/menu.h"
#include "ui/mutation_ui.h"
#include "ui/settings_ui.h"
#include "ui/bestiary_ui.h"
#include "ui/debug_ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

Game G;

static float sLetterbox = 0.0f;
static float sPhotoFocus = 12.0f;
static bool sCapture = false;
static float sProfileSaveT = 0.0f;
static float sPortalSfxT = 0.0f;

/* ------------------------------------------------------------------------- */
/* State changes                                                              */
/* ------------------------------------------------------------------------- */

static bool CursorLocked(GameState s) { return s == GS_PLAY || s == GS_PHOTO; }

void Game_SetState(GameState s)
{
    GameState prev = G.state;
    G.state = s;
    if (CursorLocked(s) && !G.debug.autotest) { if (!IsCursorHidden()) DisableCursor(); }
    else if (IsCursorHidden()) EnableCursor();
    Input_ClearEdges(&G.in);
    G.in.pause = G.in.mutations = false;
    G.ui.clickCooldown = 0.15f;
    if (s == GS_PHOTO) Cam_EnterFree();
    if (prev == GS_PHOTO && s != GS_PHOTO) G.rig.mode = CAM_FOLLOW;
    if (s == GS_BOSS_INTRO) G.ui.evolveT = 0.0f;
    if (s == GS_REGION_CLEAR || s == GS_GAMEOVER || s == GS_VICTORY) Save_WriteProfile();
}

void Game_OpenOverlay(GameState s)
{
    G.ui.returnState = G.state;
    G.ui.rebindAction = -1;
    Game_SetState(s);
}

void Game_CloseOverlay(void) { Game_SetState(G.ui.returnState); }

/* ------------------------------------------------------------------------- */
/* Region loading                                                             */
/* ------------------------------------------------------------------------- */

static void MenuBackground(void)
{
    G.run.active = false;
    G.run.mode = MODE_CAMPAIGN;
    G.run.difficulty = DIFF_STANDARD;
    memset(&G.player, 0, sizeof(G.player));
    memset(&G.mut, 0, sizeof(G.mut));
    G.player.dead = true;
    G.player.attach = G.player.engulfedBy = G.player.aimAgent = REF_NONE;
    Player_RecomputeStats();
    Particles_Clear();
    Lights_ClearTemp();
    Proj_Clear();
    Bosses_Clear();
    memset(G.popups, 0, sizeof(G.popups));
    int idx = Rng_IntG(0, REGION_COUNT - 1);
    Region_Generate(idx, Rng_U32(&G.rng), true);
    G.region.index = idx;
    G.rig.pos = G.region.path[0];
    G.rig.cam.position = G.rig.pos;
    G.rig.cam.target = G.region.path[2];
    G.rig.mode = CAM_MENU;
    G.player.pos = G.region.startPos;
}

static void BeginRegionLoad(void)
{
    G.loadingFrames = 0;
    Game_SetState(GS_LOADING);
}

static void LoadRegion(void)
{
    RunState *r = &G.run;
    Particles_Clear();
    Lights_ClearTemp();
    Proj_Clear();
    Bosses_Clear();
    memset(G.popups, 0, sizeof(G.popups));
    G.player.dead = false;
    Player_RecomputeStats();
    Region_Generate(r->regionIndex, r->seed, false);
    Events_Reset();
    Enemies_Reset();
    Immune_Reset(r->regionIndex > 0 || r->stats.regionsCleared > 0);
    Player_Reset();
    Render_RebuildPlayer();
    G.rig.mode = CAM_FOLLOW;
    Cam_SnapTo(G.player.pos, Player_DesiredCamDist());
    G.accumulator = 0.0f;
    G.hitStop = 0.0f;
    G.slowmoT = 0.0f;
    if (r->mode == MODE_OUTBREAK) {
        r->waveT = 8.0f;
        G.region.nextEventT = 90.0f;
    } else {
        Save_StoreRun();
        if (r->regionIndex > G.profile.bestRegion) G.profile.bestRegion = r->regionIndex;
        Save_WriteProfile();
    }
    G.ui.bannerT = 0.0f;
    G.ui.hintT = 0.0f;
    HUD_RegionTitle();
    Music_Stinger(0);
    Game_SetState(GS_PLAY);
    if (G.player.stage == 0 && r->regionIndex == 0)
        HUD_Hint(TUT_MOVE, "WASD to swim, SPACE / CTRL to rise and sink, mouse to steer. Find host cells on the vessel walls and hold RMB to infect them.");
}

void Game_StartRun(RunMode mode, Difficulty diff, uint32_t seed, bool daily, int ngPlus)
{
    memset(&G.run, 0, sizeof(G.run));
    G.run.mode = mode;
    G.run.difficulty = diff;
    G.run.seed = seed;
    G.run.daily = daily;
    G.run.ngPlus = ngPlus;
    G.run.active = true;
    G.run.regionIndex = (mode == MODE_OUTBREAK) ? (int)(seed % REGION_COUNT) : 0;
    memset(&G.player, 0, sizeof(G.player));
    memset(&G.mut, 0, sizeof(G.mut));
    memset(&G.immune, 0, sizeof(G.immune));
    if (mode == MODE_OUTBREAK) { G.player.gm = 60; }
    G.profile.runsStarted++;
    if (mode == MODE_CAMPAIGN) Save_ClearRun();
    BeginRegionLoad();
}

void Game_ContinueRun(void)
{
    if (!Save_RestoreRun()) return;
    memset(&G.immune, 0, sizeof(G.immune));
    BeginRegionLoad();
}

void Game_RetryRegion(void) { Game_ContinueRun(); }

void Game_AbandonRun(void)
{
    if (G.run.mode == MODE_OUTBREAK && G.run.wave > G.profile.bestWave) G.profile.bestWave = G.run.wave;
    Save_WriteProfile();
    MenuBackground();
    Game_SetState(GS_MENU);
}

void Game_ReturnToMenu(void)
{
    if (G.run.mode == MODE_CAMPAIGN && !G.player.dead) Save_StoreRun();
    Game_AbandonRun();
}

void Game_NextRegion(void)
{
    RunState *r = &G.run;
    Immune_StoreCarry();
    r->stats.regionsCleared++;
    if (r->regionIndex >= REGION_COUNT - 1) {
        G.profile.victories++;
        G.profile.outbreakUnlocked = true;
        if (G.profile.ngPlusUnlocked < r->ngPlus + 1) G.profile.ngPlusUnlocked = r->ngPlus + 1;
        HUD_Achievement(ACH_TOTAL_INFECTION);
        Save_ClearRun();
        Save_WriteProfile();
        Music_Stinger(3);
        Game_SetState(GS_VICTORY);
        return;
    }
    r->regionIndex++;
    BeginRegionLoad();
}

/* ------------------------------------------------------------------------- */
/* Simulation                                                                 */
/* ------------------------------------------------------------------------- */

static void CheckProgress(float dt)
{
    RegionState *rg = &G.region;
    Player *p = &G.player;
    (void)dt;
    if (G.state != GS_PLAY) return;

    if (G.run.mode == MODE_CAMPAIGN && !rg->bossSpawned && rg->hostTotal > 0 && rg->infection >= BOSS_INFECTION_PCT - 1e-4f) {
        HUD_Toast("THE GUARDIAN STIRS", "Your infection has reached critical mass", (Color){ 255, 120, 100, 255 });
        Bosses_Spawn(Region_Def()->boss, rg->bossPos);
        return;
    }
    if (rg->portalOpen) {
        sPortalSfxT -= dt;
        if (GetRandomValue(0, 100) < 70) {
            Vector3 from = Vector3Add(rg->portalPos, Vector3Scale(Rng_UnitVecG(), 14.0f));
            Particles_Emit(from, Vector3Scale(Vector3Subtract(rg->portalPos, from), 1.4f), V4(0.5f, 2.4f, 1.4f, 0.0f), V4(1.0f, 3.0f, 2.0f, 1.0f), 0.25f, 0.05f, 0.7f, 0.0f, PF_ADD);
        }
        float d = Vector3Distance(p->pos, rg->portalPos);
        if (d < 30.0f && sPortalSfxT <= 0.0f) { sPortalSfxT = 3.0f; Sfx_Play3D(SFX_PORTAL, rg->portalPos, 0.7f); }
        if (d < 6.5f + p->radius && !p->dead) {
            if (rg->peakAlert < 40.0f) { Player_GainGM(40, p->pos); HUD_Achievement(ACH_GHOST); }
            Sfx_Play(SFX_PORTAL, 1.0f, 1.2f);
            HUD_Flash((Color){ 180, 255, 220, 255 }, 0.9f);
            Game_SetState(GS_REGION_CLEAR);
            return;
        }
    }
    if (p->dead && p->deathT > 2.8f) {
        Game_SetState(GS_GAMEOVER);
        return;
    }
    if (G.immune.adaptations >= 6) HUD_Achievement(ACH_ARMS_RACE);
    if (Evolution_Ready()) {
        HUD_Hint(TUT_EVOLVE, "Evolution! Enough Evolution Points rewrite your body into a new stage with new abilities and mutation tiers.");
        Evolution_Begin();
    }
}

static void SimStep(float dt)
{
    const RegionDef *d = Region_Def();
    G.simTime += dt;
    if (G.state == GS_PLAY) G.run.stats.time += dt;
    SH_Build();
    Player_Update(dt);
    Agents_Update(dt);
    Enemies_Director(dt);
    Bosses_Update(dt);
    Proj_Update(dt);
    Pickups_Update(dt);
    Hazards_Update(dt);
    Events_Update(dt);
    Region_Update(dt);
    Immune_Update(dt);
    int motes = G.settings.quality == 0 ? 500 : (G.settings.quality == 1 ? 900 : 1400);
    Particles_Ambient(G.rig.cam.position, d->moteA, d->moteB, motes, dt);
    Particles_Update(dt);
    Lights_UpdateTemp(dt);
    CheckProgress(dt);
}

static void BackgroundStep(float dt)
{
    const RegionDef *d = Region_Def();
    G.simTime += dt;
    SH_Build();
    Agents_Update(dt);
    Particles_Ambient(G.rig.cam.position, d->moteA, d->moteB, 1200, dt);
    Particles_Update(dt);
    Lights_UpdateTemp(dt);
}

static void RunFixed(float frameDt, bool full)
{
    float scale = 1.0f;
    if (G.hitStop > 0.0f) { G.hitStop -= frameDt; scale = 0.0f; }
    else if (G.slowmoT > 0.0f) { G.slowmoT -= frameDt; scale = 0.3f + 0.7f * Sat(1.0f - G.slowmoT * 1.5f); }
    G.timeScale = scale;
    G.accumulator += frameDt * scale;
    int steps = 0;
    while (G.accumulator >= SIM_DT && steps < MAX_SIM_STEPS) {
        double t0 = GetTime();
        if (full) SimStep(SIM_DT); else BackgroundStep(SIM_DT);
        G.debug.simMs = G.debug.simMs * 0.9f + (float)((GetTime() - t0) * 1000.0) * 0.1f;
        Input_ClearEdges(&G.in);
        G.accumulator -= SIM_DT;
        steps++;
        if (full && G.state != GS_PLAY && G.state != GS_GAMEOVER) break;
    }
    if (steps == MAX_SIM_STEPS) G.accumulator = 0.0f;
    G.debug.frameSimSteps = G.debug.frameSimSteps * 0.9f + (float)steps * 0.1f;
    G.alpha = Sat(G.accumulator / SIM_DT);
}

static Vector3 PlayerRenderPos(void) { return Vector3Lerp(G.player.prev, G.player.pos, G.alpha); }

static void UpdateState(float dt)
{
    Player *p = &G.player;
    switch (G.state) {
        case GS_BOOT: Game_SetState(GS_MENU); break;
        case GS_MENU: case GS_NEWGAME: case GS_CREDITS:
            Cam_UpdateMenu(dt);
            RunFixed(dt, false);
            break;
        case GS_SETTINGS: case GS_BESTIARY:
            if (!G.run.active) { Cam_UpdateMenu(dt); RunFixed(dt, false); }
            break;
        case GS_LOADING:
            G.loadingFrames++;
            if (G.loadingFrames == 3) LoadRegion();
            break;
        case GS_PLAY: {
            Input_Poll(&G.in, true);
            if (G.in.pause) { Game_SetState(GS_PAUSE); break; }
            if (G.in.mutations) { MutUI_Open(); break; }
            if (!p->dead) Cam_Look(G.in.lookX, G.in.lookY);
            RunFixed(dt, true);
            Cam_Update(dt, PlayerRenderPos(), Player_DesiredCamDist());
        } break;
        case GS_GAMEOVER:
            RunFixed(dt * 0.5f, true);
            Cam_Update(dt, PlayerRenderPos(), Player_DesiredCamDist() + 6.0f);
            break;
        case GS_PAUSE: case GS_REGION_CLEAR: case GS_VICTORY:
            Particles_Update(dt * 0.15f);
            break;
        case GS_MUTATION:
            if (IsKeyPressed(G.settings.keys[ACT_MUTATIONS]) && G.ui.clickCooldown <= 0.0f) { Game_SetState(GS_PLAY); break; }
            Particles_Update(dt * 0.15f);
            break;
        case GS_EVOLVE:
            Evolution_Update(dt);
            Particles_Update(dt);
            Lights_UpdateTemp(dt);
            Cam_Update(dt, p->pos, Player_DesiredCamDist());
            break;
        case GS_BOSS_INTRO:
            G.ui.evolveT += dt;
            Bosses_UpdateIntro(dt);
            Particles_Update(dt);
            Lights_UpdateTemp(dt);
            Cam_Update(dt, p->pos, Player_DesiredCamDist());
            if (G.ui.evolveT > 4.2f || (G.ui.evolveT > 0.6f && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))) {
                G.boss.introDone = true;
                G.rig.mode = CAM_FOLLOW;
                Game_SetState(GS_PLAY);
            }
            break;
        case GS_PHOTO:
            Cam_UpdateFree(dt);
            sPhotoFocus = Clamp(sPhotoFocus + GetMouseWheelMove() * 2.0f, 1.0f, 120.0f);
            if (IsKeyPressed(KEY_F12)) sCapture = true;
            if (IsKeyPressed(KEY_ESCAPE)) Game_SetState(GS_PAUSE);
            Particles_Update(dt * 0.05f);
            break;
        default: break;
    }
}

/* ------------------------------------------------------------------------- */
/* Rendering                                                                  */
/* ------------------------------------------------------------------------- */

static PostParams BuildPost(void)
{
    PostParams pp = PostFx_Defaults();
    const RegionDef *d = Region_Def();
    Player *p = &G.player;
    RegionState *rg = &G.region;
    pp.lift = d->lift; pp.gain = d->gain; pp.tint = d->tint;
    pp.saturation = d->saturation; pp.contrast = d->contrast;
    pp.hazeColor = d->hazeColor; pp.haze = d->haze;
    float heart = Music_Heartbeat();
    float alert = rg->alert / ALERT_MAX;
    bool playing = G.run.active;
    pp.focus = playing ? Vector3Distance(G.rig.cam.position, PlayerRenderPos()) : 25.0f;
    pp.dof = 0.55f;
    switch (G.state) {
        case GS_PAUSE: case GS_MUTATION: case GS_REGION_CLEAR: case GS_GAMEOVER: case GS_VICTORY: case GS_SETTINGS: case GS_BESTIARY: case GS_NEWGAME:
            if (playing || G.state == GS_NEWGAME) { pp.dof = 1.0f; pp.focus = 0.5f; }
            break;
        case GS_PHOTO: pp.focus = sPhotoFocus; pp.dof = 0.8f; break;
        default: break;
    }
    if (playing) {
        float bio = G.stats.maxBiomass > 0.0f ? p->biomass / G.stats.maxBiomass : 1.0f;
        float low = Sat((0.35f - bio) / 0.35f);
        pp.pulse = heart * (0.25f + 1.1f * alert + 1.2f * low);
        pp.damage = Sat(p->hurtFlash * 0.7f + low * (0.45f + 0.35f * heart));
        pp.chroma = 0.22f + p->hurtFlash * 1.4f + (p->dashT > 0.0f ? 0.7f : 0.0f) + alert * 0.25f;
        pp.vignette = 0.5f + 0.25f * alert;
        pp.grain = 0.03f + 0.03f * alert;
        pp.shimmer = rg->fever * 0.8f + (p->engulfedBy.idx >= 0 ? 1.6f : 0.0f);
        if (p->cloaked) {
            pp.tint = Vector3Lerp(pp.tint, (Vector3){ 0.85f, 0.92f, 1.2f }, 0.6f);
            pp.saturation *= 0.55f;
            pp.vignette += 0.35f;
        }
        if (p->engulfedBy.idx >= 0) { pp.tint = Vector3Multiply(pp.tint, (Vector3){ 1.25f, 0.9f, 0.7f }); pp.chroma += 0.8f; pp.vignette += 0.4f; }
        if (p->dead) { pp.saturation *= Sat(1.0f - p->deathT * 0.4f); pp.vignette += 0.3f; }
        if (G.state == GS_MUTATION) pp.saturation *= 0.7f;
    } else {
        pp.exposure = 1.08f;
        pp.pulse = heart * 0.2f;
    }
    Color fc;
    float f = HUD_FlashAmount(&fc);
    pp.flash = f * 0.8f;
    pp.flashColor = (Vector3){ fc.r / 255.0f, fc.g / 255.0f, fc.b / 255.0f };
    pp.exposure += f * 0.2f;
    float lbTarget = (G.state == GS_BOSS_INTRO || G.state == GS_EVOLVE) ? 0.1f : 0.0f;
    sLetterbox += (lbTarget - sLetterbox) * fminf(1.0f, GetFrameTime() * 5.0f);
    pp.letterbox = sLetterbox;
    return pp;
}

static void Draw(void)
{
    double t0 = GetTime();
    BeginDrawing();
    Render_ResetCounters();
    UI_Begin();
    if (G.state != GS_LOADING) {
        PostParams pp = BuildPost();
        Vector3 fc = Region_Def()->fogColor;
        PostFx_Begin(ColorF(fc.x, fc.y, fc.z, 1.0f));
        Render_World(G.rig.cam, G.alpha);
        PostFx_End(&pp);
        if (sCapture) {
            sCapture = false;
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            char path[700];
            time_t now = time(NULL);
            snprintf(path, sizeof(path), "%s/patient_zero_%ld.png", Save_Dir(), (long)now);
            if (ExportImage(img, path)) HUD_Toast("PHOTO SAVED", path, (Color){ 200, 230, 255, 255 });
            UnloadImage(img);
            Sfx_Play(SFX_UI_CLICK, 0.8f, 1.4f);
        }
    } else {
        ClearBackground(BLACK);
    }
    switch (G.state) {
        case GS_MENU: Menu_DrawMain(); break;
        case GS_NEWGAME: Menu_DrawNewGame(); break;
        case GS_LOADING: Menu_DrawLoading(); break;
        case GS_PLAY: HUD_Draw(); break;
        case GS_PAUSE: Menu_DrawPause(); break;
        case GS_MUTATION: MutUI_Draw(); break;
        case GS_EVOLVE: Menu_DrawEvolve(); break;
        case GS_BOSS_INTRO: Menu_DrawBossIntro(); break;
        case GS_REGION_CLEAR: Menu_DrawRegionClear(); break;
        case GS_GAMEOVER: Menu_DrawGameOver(); break;
        case GS_VICTORY: Menu_DrawVictory(); break;
        case GS_CREDITS: Menu_DrawCredits(); break;
        case GS_SETTINGS: SettingsUI_Draw(); break;
        case GS_BESTIARY: Bestiary_Draw(); break;
        case GS_PHOTO: Menu_DrawPhoto(); break;
        default: break;
    }
    if (G.state != GS_PHOTO && G.state != GS_LOADING) HUD_DrawMessages();
    DebugUI_Draw();
    if (G.debug.shot[0]) {
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        ExportImage(img, G.debug.shot);
        UnloadImage(img);
        G.debug.shot[0] = 0;
    }
    rlDrawRenderBatchActive();
    G.debug.drawMs = G.debug.drawMs * 0.9f + (float)((GetTime() - t0) * 1000.0) * 0.1f;
    EndDrawing();
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* ------------------------------------------------------------------------- */

void Game_Init(void)
{
    Save_Init();
    Save_LoadProfile();
    Settings *s = &G.settings;
    if (s->uiScale < 0.5f) s->uiScale = 1.0f;
    unsigned int flags = FLAG_WINDOW_RESIZABLE;
    if (s->msaa) flags |= FLAG_MSAA_4X_HINT;
    if (s->vsync) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    int w = RES_PRESETS[s->resIndex % RES_PRESET_COUNT][0], h = RES_PRESETS[s->resIndex % RES_PRESET_COUNT][1];
    InitWindow(w, h, PZ_TITLE);
    SetWindowMinSize(WINDOW_MIN_W, WINDOW_MIN_H);
    int mon = GetCurrentMonitor();
    if (GetMonitorWidth(mon) > 0 && (w > GetMonitorWidth(mon) || h > GetMonitorHeight(mon))) SetWindowSize(WINDOW_W, WINDOW_H);
    SetExitKey(KEY_NULL);
    if (!s->vsync) SetTargetFPS(240);
    if (s->fullscreen) ToggleBorderlessWindowed();
    InitAudioDevice();

    Rng_Seed(&G.rng, (uint64_t)time(NULL) ^ 0x9E3779B97F4A7C15ull);
    if (!Shaders_Init()) TraceLog(LOG_ERROR, "PZ: a shader failed to compile - visuals may be wrong");
    Cave_Init();
    Flow_Init();
    Particles_Init();
    Render_Init();
    PostFx_Init(GetScreenWidth(), GetScreenHeight());
    Sfx_Init();
    Music_Init();
    Cam_Init();
    HUD_Reset();
    G.ui.codexSel = 0;
    G.ui.rebindAction = -1;
    G.ui.mutZoom = 0.62f;
    G.timeScale = 1.0f;
    snprintf(G.ui.seedText, sizeof(G.ui.seedText), "1337");
    Player_RecomputeStats();
    Render_RebuildPlayer();
    MenuBackground();
    G.state = GS_MENU;
    Autotest_Init();
}

void Game_Shutdown(void)
{
    Save_WriteProfile();
    Bestiary_Shutdown();
    Music_Shutdown();
    Sfx_Shutdown();
    PostFx_Shutdown();
    Render_Shutdown();
    Cave_Shutdown();
    Flow_Shutdown();
    Shaders_Unload();
    CloseAudioDevice();
    CloseWindow();
}

void Game_Frame(void)
{
    float dt = GetFrameTime();
    if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    G.realTime += dt;
    G.frame++;
    G.ui.clickCooldown -= dt;
    DebugUI_Update(dt);

    if (IsKeyPressed(KEY_F3)) G.debug.overlay = !G.debug.overlay;
    if (IsKeyPressed(KEY_F4)) G.debug.collision = !G.debug.collision;
    if (IsKeyPressed(KEY_F6)) { G.debug.godMode = !G.debug.godMode; HUD_FloatText(G.debug.godMode ? "God mode ON" : "God mode OFF", YELLOW); }
    if (IsKeyPressed(KEY_F7) && G.run.active) { G.player.gm += 500; HUD_FloatText("+500 GM (debug)", YELLOW); }
    if (IsKeyPressed(KEY_F8) && G.run.active) { Player_GainEP(100.0f); HUD_FloatText("+100 EP (debug)", YELLOW); }
    if (IsKeyPressed(KEY_F12) && G.state != GS_PHOTO) sCapture = true;
    if (IsWindowResized()) PostFx_Resize(GetScreenWidth(), GetScreenHeight());

    Autotest_Frame(dt);
    UpdateState(dt);
    if (G.state != GS_LOADING) HUD_Update(dt);
    Sfx_Update(dt);
    Music_Update(dt);

    sProfileSaveT += dt;
    if (sProfileSaveT > 120.0f) { sProfileSaveT = 0.0f; Save_WriteProfile(); }
    Draw();
}

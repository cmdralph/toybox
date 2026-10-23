/*
 * autotest.c - scripted smoke test. Enabled with `--autotest`; inactive
 * otherwise. Every step is keyed to accumulated (clamped) frame time so the
 * script behaves the same on a fast GPU or a software rasteriser.
 */
#include "core/autotest.h"
#include "core/game_state.h"
#include "entities/player.h"
#include "entities/bosses.h"
#include "systems/infection.h"
#include "systems/spatial_hash.h"
#include "systems/mutation.h"
#include "systems/evolution.h"
#include "world/region.h"
#include "gfx/render.h"
#include "gfx/particles.h"
#include "ui/mutation_ui.h"
#include "ui/bestiary_ui.h"
#include <stdio.h>
#include <string.h>

static float sT = 0.0f;
static int sStep = 0;
static float sStatT = 0.0f;
static int sFrames = 0;
static float sTapCd = 0.0f;
static float sRegionT = 0.0f;

void Autotest_Init(void)
{
    sT = 0.0f;
    sStep = 0;
    if (G.debug.autotest) {
        G.settings.shake = 0.5f;
        printf("[autotest] started (raylib %s)\n", RAYLIB_VERSION);
    }
}

static void Shot(const char *name)
{
    snprintf(G.debug.shot, sizeof(G.debug.shot), "autotest_%02d_%s.png", sStep, name);
    printf("[autotest] t=%.1f screenshot %s  state=%d agents=%d particles=%d fps=%d\n", sT, G.debug.shot, (int)G.state, G.agentHigh, Particles_Alive(), GetFPS());
    int near[AK_COUNT] = { 0 };
    for (int i = 0; i < G.agentHigh; i++) if (G.agents[i].alive && Vector3Distance(G.agents[i].pos, G.player.pos) < 30.0f) near[G.agents[i].kind]++;
    printf("[autotest]   within 30um: drifters %d platelets %d hosts %d bacteria %d minions %d immune %d\n", near[AK_DRIFTER], near[AK_PLATELET], near[AK_HOST], near[AK_BACTERIA], near[AK_MINION],
           near[AK_NEUTROPHIL] + near[AK_MACROPHAGE] + near[AK_NK] + near[AK_TCELL] + near[AK_BCELL]);
}

static void Teleport(Vector3 p)
{
    G.player.pos = G.player.prev = p;
    G.player.vel = (Vector3){ 0 };
}

static void KillBoss(void)
{
    if (!G.boss.active) return;
    G.boss.introDone = true;
    G.boss.invuln = 0.0f;
    G.boss.shielded = false;
    Bosses_TakeDamage(G.boss.hp + 10.0f, 0, G.boss.pos);
}

static void SkipIntroAndApproach(void)
{
    G.boss.introDone = true;
    G.rig.mode = CAM_FOLLOW;
    Game_SetState(GS_PLAY);
    Vector3 d = Vector3Normalize(Vector3Subtract(G.region.startPos, G.boss.pos));
    Teleport(Vector3Add(G.boss.pos, Vector3Scale(d, G.boss.radius + 12.0f)));
    G.debug.godMode = true;
}

/* the script: each step fires once when the accumulated time passes WHEN[step] */
void Autotest_Frame(float dt)
{
    if (!G.debug.autotest) return;
    sT += dt;
    sFrames++;
    sStatT += dt;
    sRegionT += dt;
    if (sStatT > 5.0f) {
        sStatT = 0.0f;
        printf("[autotest] sim %.2f ms x%.1f/frame, draw %.2f ms\n", G.debug.simMs, G.debug.frameSimSteps, G.debug.drawMs);
        printf("[autotest] t=%.1f state=%d region=%d stage=%d hp=%.0f infected=%d/%d alert=%.0f enemies=%d minions=%d draws=%d inst=%d\n", sT, (int)G.state, G.region.index, G.player.stage,
               G.player.biomass, G.region.hostInfected, G.region.hostTotal, G.region.alert, G.agentCount[AK_NEUTROPHIL] + G.agentCount[AK_MACROPHAGE] + G.agentCount[AK_NK] + G.agentCount[AK_TCELL] + G.agentCount[AK_BCELL],
               G.agentCount[AK_MINION], Render_DrawCalls(), Render_Instances());
        fflush(stdout);
    }
    static const float WHEN[] = { 0.6f, 1.0f, 4.0f, 14.0f, 14.5f, 15.4f, 15.8f, 16.2f, 17.4f, 22.0f, 22.5f, 23.0f, 23.3f, 23.8f, 24.1f, 24.4f, 25.0f, 25.3f,
                                  26.0f, 27.0f, 28.0f, 30.5f, 31.0f, 32.0f, 36.0f, 37.5f, 38.0f, 41.0f, 41.5f, 42.5f, 46.0f, 46.5f, 49.5f, 50.0f, 53.0f,
                                  53.5f, 56.5f, 57.0f, 60.0f, 60.5f, 61.5f, 62.5f, 65.0f, 65.5f, 67.0f, 67.5f, 71.0f, 76.0f, 76.5f };
    if (sStep >= (int)(sizeof(WHEN) / sizeof(WHEN[0])) || sT < WHEN[sStep]) return;
    switch (sStep) {
        case 0: Shot("menu"); break;
        case 1: Game_StartRun(MODE_CAMPAIGN, DIFF_STANDARD, 4242u, false, 0); break;
        case 2: Shot("blood_start"); break;
        case 3: Shot("blood_autopilot"); break;
        case 4: G.player.gm += 600; MutUI_Open(); break;
        case 5: {
            int buy[] = { M_FLAGELLA_1, M_SPIKES_1, M_CAPSID_1, T_POTENCY, T_RAPID, R_FAST_INJECT, S_CAMO };
            for (size_t i = 0; i < sizeof(buy) / sizeof(buy[0]); i++) Mutation_Buy(buy[i]);
            Player_RecomputeStats();
            Render_RebuildPlayer();
        } break;
        case 6: Shot("mutation_tree"); break;
        case 7: Game_SetState(GS_PLAY); Player_GainEP(40.0f); break;
        case 8: Shot("evolve"); break;
        case 9: Shot("after_evolve"); break;
        case 10: Game_SetState(GS_PAUSE); break;
        case 11: Shot("pause"); break;
        case 12: Game_OpenOverlay(GS_SETTINGS); break;
        case 13: Shot("settings"); break;
        case 14: Game_CloseOverlay(); Game_SetState(GS_PLAY); break;
        case 15: for (int i = 0; i < CX_COUNT; i++) G.profile.codex[i] = 1; G.ui.codexSel = CX_NEUTROPHIL; Game_OpenOverlay(GS_BESTIARY); break;
        case 16: Shot("codex"); break;
        case 17: Game_CloseOverlay(); Game_SetState(GS_PLAY); break;
        case 18: Bosses_Spawn(Region_Def()->boss, G.region.bossPos); break;
        case 19: Shot("boss_intro"); break;
        case 20: SkipIntroAndApproach(); break;
        case 21: Shot("boss_fight"); break;
        case 22: KillBoss(); break;
        case 23: Shot("boss_death"); break;
        case 24: Teleport(G.region.portalPos); break;
        case 25: Shot("region_clear"); G.debug.godMode = false; break;
        case 26: Game_NextRegion(); sRegionT = 0.0f; break;
        case 27: Shot("lungs"); break;
        case 28: G.player.invuln = 0.0f; G.player.biomass = 0.0f; Player_Die(); break;
        case 29: Shot("dying"); break;
        case 30: Shot("gameover"); break;
        case 31: Game_RetryRegion(); break;
        case 32: Shot("lungs_retry"); break;
        case 33: G.run.regionIndex = 1; Game_NextRegion(); break;
        case 34: Shot("gut"); break;
        case 35: Game_NextRegion(); break;
        case 36: Shot("lymph"); break;
        case 37: Game_NextRegion(); break;
        case 38: Shot("brain"); break;
        case 39: Bosses_Spawn(Region_Def()->boss, G.region.bossPos); break;
        case 40: Shot("brain_boss_intro"); break;
        case 41: SkipIntroAndApproach(); break;
        case 42: Shot("brain_boss_fight"); break;
        case 43: G.debug.godMode = false; G.run.regionIndex = REGION_COUNT - 1; Game_NextRegion(); break;
        case 44: Shot("victory"); break;
        case 45: Game_StartRun(MODE_OUTBREAK, DIFF_STANDARD, 99u, false, 0); break;
        case 46: G.run.waveT = 0.5f; Shot("outbreak_start"); break;
        case 47: Shot("outbreak_wave"); break;
        case 48: printf("[autotest] done after %d frames\n", sFrames); fflush(stdout); G.quit = true; break;
        default: break;
    }
    sStep++;
}

void Autotest_Input(InputState *in)
{
    Player *p = &G.player;
    if (!G.run.active || p->dead || G.state != GS_PLAY) return;
    float dt = GetFrameTime();
    sTapCd -= dt;
    in->moveX = in->moveY = in->moveZ = 0.0f;
    in->fire = false;
    in->infect = false;
    in->lookX = in->lookY = 0.0f;
    Vector3 obj;
    bool have = Region_ObjectivePos(&obj);
    int e = SH_Nearest(p->pos, 26.0f, AGENT_MASK_IMMUNE_CELLS, -1);
    Vector3 aim = have ? obj : Vector3Add(p->pos, p->forward);
    if (e >= 0 && p->attach.idx < 0) aim = G.agents[e].pos;
    Vector3 d = Vector3Normalize(Vector3Subtract(aim, G.rig.cam.position));
    float yaw = atan2f(d.x, d.z), pitch = asinf(Clamp(d.y, -0.95f, 0.95f));
    float dy = yaw - G.rig.yaw;
    while (dy > PI) dy -= 2.0f * PI;
    while (dy < -PI) dy += 2.0f * PI;
    G.rig.yaw += dy * fminf(1.0f, dt * 6.0f);
    G.rig.pitch += (Clamp(pitch, -1.2f, 1.2f) - G.rig.pitch) * fminf(1.0f, dt * 6.0f);

    if (p->attach.idx >= 0) {
        in->infect = true;
        if (Infection_InSweetSpot() && sTapCd <= 0.0f) { in->firePressed = true; sTapCd = 0.5f; }
        return;
    }
    if (Infection_FindTarget().idx >= 0) {
        in->infect = true;
        in->infectPressed = true;
        return;
    }
    if (e >= 0) {
        float dist = Vector3Distance(G.agents[e].pos, p->pos);
        in->fire = true;
        in->moveY = dist > 14.0f ? 0.6f : -0.4f;
        in->moveX = sinf(G.realTime * 1.3f);
        if (dist < 7.0f && p->burstCd <= 0.0f && p->atp > 60.0f) in->burst = true;
        return;
    }
    if (have) {
        in->moveY = 1.0f;
        if (Vector3Distance(obj, p->pos) > 40.0f && p->atp > 80.0f && GetRandomValue(0, 100) < 2) in->dash = true;
    }
}

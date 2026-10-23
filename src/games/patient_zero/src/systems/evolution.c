/*
 * evolution.c - metamorphosis is a short cinematic: time slows, the camera
 * orbits, the old body convulses, then the new form bursts out in a flash
 * with new abilities announced.
 */
#include "systems/evolution.h"
#include "entities/player.h"
#include "gfx/camera.h"
#include "gfx/particles.h"
#include "gfx/lights.h"
#include "gfx/render.h"
#include "audio/sfx.h"
#include "audio/music.h"
#include "ui/hud.h"
#include "core/math_utils.h"
#include "core/game_state.h"
#include "core/rng.h"

const StageDef STAGES[STAGE_COUNT] = {
    { "Rogue Virion", "STAGE I  -  ROGUE VIRION", "A lone capsid. Fragile and quiet. Infect cells to grow.", 0.0f, 0.60f, 6.5f },
    { "Replicator", "STAGE II  -  REPLICATOR", "Infected cells now assemble virions that fight for you. Press E to direct your swarm.", 35.0f, 0.85f, 8.0f },
    { "Colony Organism", "STAGE III  -  COLONY ORGANISM", "A membrane-bound colony: tougher body, larger swarm, tier-3 mutations.", 140.0f, 1.15f, 10.5f },
    { "Apex Pathogen", "STAGE IV  -  APEX PATHOGEN", "Massive and armoured. Tier-4 mutations unlocked.", 300.0f, 1.55f, 13.5f },
    { "Pandemic Entity", "STAGE V  -  PANDEMIC ENTITY", "The final form. The body cannot hold you.", 540.0f, 2.0f, 17.0f },
};

#define EVOLVE_TIME 4.2f
static bool sApplied = false;

float Evolution_Duration(void) { return EVOLVE_TIME; }

bool Evolution_Ready(void)
{
    int s = G.player.stage;
    return s < STAGE_COUNT - 1 && G.player.ep >= STAGES[s + 1].epThreshold && !G.player.dead && G.player.attach.idx < 0;
}

float Evolution_Progress(void)
{
    int s = G.player.stage;
    if (s >= STAGE_COUNT - 1) return 1.0f;
    float a = STAGES[s].epThreshold, b = STAGES[s + 1].epThreshold;
    return Sat((G.player.ep - a) / (b - a));
}

void Evolution_Begin(void)
{
    G.ui.evolveT = 0.0f;
    G.ui.evolveFrom = G.player.stage;
    sApplied = false;
    Game_SetState(GS_EVOLVE);
    Cam_StartCinematic(G.player.pos, STAGES[G.player.stage].camDist * 1.2f + 3.0f, 2.5f, EVOLVE_TIME);
    Sfx_Play(SFX_EVOLVE, 1.0f, 1.0f);
    Music_Stinger(1);
}

void Evolution_Update(float dt)
{
    G.ui.evolveT += dt;
    float t = G.ui.evolveT;
    Player *p = &G.player;
    /* convulse, gather energy */
    if (t < 2.0f) {
        p->squash = 0.25f * sinf(t * 24.0f) * (t / 2.0f);
        if (GetRandomValue(0, 100) < 70) {
            Vector3 from = Vector3Add(p->pos, Vector3Scale(Rng_UnitVecG(), p->radius * 6.0f));
            Particles_Emit(from, Vector3Scale(Vector3Subtract(p->pos, from), 1.6f), V4(0.4f, 2.2f, 1.2f, 0.0f), V4(1.5f, 2.5f, 2.0f, 1.0f), 0.18f, 0.05f, 0.6f, 0.2f, PF_ADD);
        }
    }
    if (!sApplied && t >= 2.0f) {
        sApplied = true;
        p->stage++;
        Player_RecomputeStats();
        p->biomass = G.stats.maxBiomass;
        p->atp = G.stats.maxAtp;
        Render_RebuildPlayer();
        Particles_Burst(p->pos, 160, 26.0f, V4(1.5f, 3.0f, 2.0f, 1.0f), V4(0.2f, 0.8f, 0.6f, 0.0f), 0.5f, 1.4f, PF_ADD);
        Particles_Ring(p->pos, (Vector3){ 0, 1, 0 }, 80, 30.0f, V4(2.0f, 3.0f, 2.5f, 1.0f), V4(0.3f, 1.0f, 0.6f, 0.0f), 0.4f, 1.0f);
        Lights_AddTemp(p->pos, (Vector3){ 3.0f, 5.0f, 4.0f }, 40.0f, 1.2f);
        Cam_AddTrauma(0.6f);
        HUD_Flash((Color){ 200, 255, 230, 255 }, 0.8f);
        HUD_Banner(STAGES[p->stage].title, STAGES[p->stage].desc, (Color){ 140, 255, 200, 255 });
        if (p->stage >= 1) HUD_Achievement(ACH_STAGE2);
        if (p->stage >= 2) HUD_Achievement(ACH_STAGE3);
        if (p->stage >= 4) HUD_Achievement(ACH_STAGE5);
        if (p->stage == 1) HUD_Hint(TUT_SWARM, "You are a Replicator. Infected cells now build virions that follow you. Press E to send them at your target.");
        G.profile.codex[CX_PLAYER] = 1;
    }
    if (t >= EVOLVE_TIME) {
        p->squash = 0.0f;
        Game_SetState(GS_PLAY);
        if (Evolution_Ready()) Evolution_Begin();
    }
}

/*
 * events.c - the body fights back on a schedule. Each region rotates its
 * three event types every ~70-100 s; events are announced on screen and in
 * audio before they bite.
 */
#include "world/events.h"
#include "world/region.h"
#include "world/hazards.h"
#include "world/flow_field.h"
#include "entities/enemies.h"
#include "ui/hud.h"
#include "audio/sfx.h"
#include "gfx/camera.h"
#include "core/rng.h"
#include "core/math_utils.h"
#include "world/sdf_cave.h"

static Rng sR;
static int sNext = 0;
static float sSubT = 0.0f;

void Events_Reset(void)
{
    Rng_Seed(&sR, G.region.seed ^ 0xE7E7u);
    sNext = 0;
    sSubT = 0.0f;
    G.region.event = EV_NONE;
    G.region.fever = 0.0f;
    Flow_SetModifiers(1.0f, -1000.0f, 0.0f, 0.0f);
}

const char *Events_Name(EventType e)
{
    switch (e) {
        case EV_FEVER: return "FEVER";
        case EV_CLOTTING: return "CLOTTING CASCADE";
        case EV_ANTIBIOTIC: return "ANTIBIOTIC STRIKE";
        case EV_PERISTALSIS: return "PERISTALTIC WAVE";
        case EV_STORM: return "ELECTRICAL STORM";
        case EV_COUGH: return "COUGH REFLEX";
        case EV_SURGE: return "LYMPHOCYTE SURGE";
        default: return "";
    }
}

float Events_EnemySpeedMul(void) { return 1.0f + 0.4f * G.region.fever; }

static Vector3 AheadOfPlayer(float dist)
{
    Vector3 f = G.player.vel;
    if (Vector3Length(f) < 1.0f) f = G.player.forward;
    f = Vector3Normalize(f);
    Vector3 p = Vector3Add(G.player.pos, Vector3Scale(f, dist));
    if (Cave_Sample(p) < 2.0f) p = G.player.pos;
    return p;
}

void Events_Trigger(EventType e)
{
    RegionState *rg = &G.region;
    rg->event = e;
    rg->eventT = 0.0f;
    sSubT = 0.0f;
    switch (e) {
        case EV_FEVER:
            rg->eventDur = 22.0f;
            HUD_Banner("FEVER", "Body temperature rising - immune cells move faster", (Color){ 255, 140, 60, 255 });
            break;
        case EV_CLOTTING: {
            rg->eventDur = 26.0f;
            HUD_Banner("CLOTTING CASCADE", "Fibrin clots are sealing the vessel - shoot through them", (Color){ 220, 60, 60, 255 });
            for (int i = 0; i < 4; i++) {
                Vector3 p = AheadOfPlayer(18.0f + 9.0f * (float)i);
                p = Vector3Add(p, Vector3Scale(Rng_UnitVec(&sR), 5.0f));
                Hazards_Spawn(HZ_CLOT, p, Rng_Range(&sR, 4.0f, 6.5f), rg->eventDur, 0.0f);
            }
        } break;
        case EV_ANTIBIOTIC:
            rg->eventDur = 9.0f;
            HUD_Banner("ANTIBIOTIC STRIKE", "Get out of the red zones!", (Color){ 255, 80, 60, 255 });
            Sfx_Play(SFX_WARNING, 1.0f, 1.0f);
            break;
        case EV_PERISTALSIS:
            rg->eventDur = 14.0f;
            HUD_Banner("PERISTALTIC WAVE", "The gut contracts - a surge is coming", (Color){ 230, 170, 80, 255 });
            Sfx_Play(SFX_RUMBLE, 1.0f, 1.0f);
            break;
        case EV_STORM:
            rg->eventDur = 16.0f;
            HUD_Banner("ELECTRICAL STORM", "Neurons are firing - watch for blue sparks", (Color){ 120, 150, 255, 255 });
            break;
        case EV_COUGH:
            rg->eventDur = 5.0f;
            HUD_Banner("COUGH REFLEX", "Hold on to something!", (Color){ 240, 220, 230, 255 });
            Sfx_Play(SFX_RUMBLE, 1.0f, 1.3f);
            Cam_AddTrauma(0.5f);
            break;
        case EV_SURGE:
            rg->eventDur = 4.0f;
            HUD_Banner("LYMPHOCYTE SURGE", "A wave of immune cells is mobilising", (Color){ 150, 255, 170, 255 });
            for (int i = 0; i < 10 + 2 * G.run.ngPlus; i++) {
                Vector3 p;
                Vector3 c = Vector3Add(G.player.pos, Vector3Scale(Rng_UnitVec(&sR), 40.0f));
                if (Cave_RandomOpenPoint(&sR, 2.0f, c, 15.0f, &p)) {
                    int a = Enemies_Spawn(i % 3 == 0 ? AK_TCELL : AK_NEUTROPHIL, p, false);
                    if (a >= 0) { G.agents[a].state = AI_CHASE; G.agents[a].flags |= AF_ALERTED; }
                }
            }
            break;
        default: break;
    }
}

void Events_Update(float dt)
{
    RegionState *rg = &G.region;
    const RegionDef *d = Region_Def();
    rg->fever = FDamp(rg->fever, rg->event == EV_FEVER ? 1.0f : 0.0f, 1.5f, dt);

    if (rg->event == EV_NONE) {
        if (rg->bossSpawned && !rg->bossDefeated) return;   /* no events mid-boss fight */
        rg->nextEventT -= dt;
        if (rg->nextEventT <= 0.0f) {
            EventType e = d->events[sNext % 3];
            sNext++;
            rg->nextEventT = Rng_Range(&sR, 70.0f, 100.0f);
            Events_Trigger(e);
        }
        return;
    }

    rg->eventT += dt;
    sSubT -= dt;
    switch (rg->event) {
        case EV_ANTIBIOTIC:
            if (sSubT <= 0.0f && rg->eventT < 6.0f) {
                sSubT = 1.1f;
                for (int i = 0; i < 3; i++) {
                    Vector3 p = Vector3Add(G.player.pos, Vector3Scale(Rng_InSphere(&sR), 22.0f));
                    if (i == 0) p = Vector3Add(G.player.pos, Vector3Scale(G.player.vel, 1.2f));
                    if (Cave_Sample(p) < 0.0f) continue;
                    Hazards_Spawn(HZ_ANTIBIOTIC, p, Rng_Range(&sR, 6.0f, 9.0f), 4.0f, 32.0f);
                }
            }
            break;
        case EV_PERISTALSIS: {
            float speed = 55.0f;
            float s = Flow_PathS(G.player.pos) - 70.0f + rg->eventT * speed * 0.5f;
            Flow_SetModifiers(1.0f, s, 5.5f, 0.0f);
            if (sSubT <= 0.0f) { sSubT = 1.6f; Cam_AddTrauma(0.15f); }
        } break;
        case EV_COUGH: {
            float k = sinf(Sat(rg->eventT / rg->eventDur) * PI);
            Flow_SetModifiers(1.0f, -1000.0f, 0.0f, 4.0f * k);
            if (sSubT <= 0.0f) { sSubT = 0.5f; Cam_AddTrauma(0.2f); }
        } break;
        case EV_STORM:
            if (sSubT <= 0.0f) {
                sSubT = 1.3f;
                Vector3 p = Vector3Add(G.player.pos, Vector3Scale(Rng_InSphere(&sR), 16.0f));
                if (Cave_Sample(p) > 0.0f) Hazards_Spawn(HZ_STORM, p, Rng_Range(&sR, 5.0f, 7.0f), 3.0f, 28.0f);
            }
            break;
        default: break;
    }

    if (rg->eventT >= rg->eventDur) {
        rg->event = EV_NONE;
        Flow_SetModifiers(1.0f, -1000.0f, 0.0f, 0.0f);
    }
}

/*
 * debug_ui.c - F3 developer overlay.
 *   F3 overlay   F4 AI/path debug draw   F6 god mode   F7 +500 GM   F8 +100 EP
 */
#include "ui/debug_ui.h"
#include "ui/widgets.h"
#include "gfx/render.h"
#include "gfx/lights.h"
#include "gfx/particles.h"
#include "world/sdf_cave.h"
#include "world/hazards.h"
#include "entities/agents.h"
#include "entities/projectiles.h"
#include "entities/pickups.h"
#include "systems/immune_memory.h"
#include <stdio.h>

void DebugUI_Update(float frameDt)
{
    DebugState *d = &G.debug;
    d->frameTimes[d->frameIdx] = frameDt;
    d->frameIdx = (d->frameIdx + 1) % 120;
}

void DebugUI_Draw(void)
{
    DebugState *d = &G.debug;
    if (!d->overlay) return;
    float x = 14, y = 140;
    UI_Panel((Rectangle){ x, y, 300, 440 }, (Color){ 0, 0, 0, 190 }, (Color){ 255, 255, 0, 120 });
    float avg = 0.0f, worst = 0.0f;
    for (int i = 0; i < 120; i++) { avg += d->frameTimes[i]; worst = fmaxf(worst, d->frameTimes[i]); }
    avg /= 120.0f;
    char b[128];
    snprintf(b, sizeof(b), "FPS %d   avg %.2f ms   worst %.2f ms", GetFPS(), avg * 1000.0f, worst * 1000.0f);
    UI_Text(b, x + 10, y + 8, 10, YELLOW);
    /* frame graph: 16.6 ms line */
    float gx = x + 10, gy = y + 24, gw = 280, gh = 50;
    UI_Rect2((Rectangle){ gx, gy, gw, gh }, (Color){ 20, 20, 20, 200 });
    UI_Rect2((Rectangle){ gx, gy + gh - gh * (16.6f / 33.3f), gw, 1 }, (Color){ 0, 255, 0, 160 });
    for (int i = 0; i < 120; i++) {
        float ft = d->frameTimes[(d->frameIdx + i) % 120];
        float h = fminf(1.0f, ft / 0.0333f) * gh;
        Color c = ft > 0.0175f ? RED : (Color){ 120, 255, 120, 255 };
        UI_Rect2((Rectangle){ gx + (float)i * (gw / 120.0f), gy + gh - h, gw / 120.0f, h }, c);
    }
    float ly = gy + gh + 8;
#define LINE(...) do { snprintf(b, sizeof(b), __VA_ARGS__); UI_Text(b, x + 10, ly, 10, (Color){ 230, 230, 230, 255 }); ly += 14; } while (0)
    int total = 0;
    for (int k = 0; k < AK_COUNT; k++) total += G.agentCount[k];
    LINE("sim %.2f ms (x%.1f/frame)   draw %.2f ms", G.debug.simMs, G.debug.frameSimSteps, G.debug.drawMs);
    LINE("agents %d (high %d / %d)", total, G.agentHigh, MAX_AGENTS);
    for (int k = 1; k < AK_COUNT; k++) if (G.agentCount[k]) LINE("  %-12s %d", Agents_KindName(k), G.agentCount[k]);
    LINE("particles %d / %d", Particles_Alive(), MAX_PARTICLES);
    LINE("projectiles %d   pickups %d   hazards %d", Proj_Count(), Pickups_Count(), Hazards_Count());
    LINE("draw calls %d   instances %d", Render_DrawCalls(), Render_Instances());
    LINE("lights %d / candidates %d", Lights_Count(), Lights_Candidates());
    LINE("cave tris %d   chunks %d drawn %d", Cave_TriangleCount(), Cave_MeshCount(), Cave_DrawnCount());
    LINE("seed %u  region %d  alert %.1f  time %.0f", G.region.seed, G.region.index, G.region.alert, G.region.time);
    LINE("pos %.1f %.1f %.1f   state %d", G.player.pos.x, G.player.pos.y, G.player.pos.z, (int)G.state);
    LINE("god %s (F6)   ai-debug %s (F4)", d->godMode ? "ON" : "off", d->collision ? "ON" : "off");
#undef LINE
}

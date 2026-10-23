/*
 * region.c - region content tables and generation.
 *
 * Layout recipe (all seeded, so a seed always yields the same body):
 *   1. random-walk control points across the volume -> Catmull-Rom main path
 *   2. tubes along the path, a start chamber and a boss arena at the end
 *   3. side branches ending in secret chambers, plus one shortcut loop
 *   4. organ features: alveoli clusters, lymph-node chambers with germinal
 *      blobs, neuronal dendrite spurs, folded gut walls (ridged noise)
 *   5. noise-roughened walls -> surface-nets mesh -> flow field
 *   6. population: wall-anchored host cells, drifters, patrols, secrets,
 *      hazards and landmark decor
 */
#include "world/region.h"
#include "world/flow_field.h"
#include "world/hazards.h"
#include "entities/agents.h"
#include "entities/hostcells.h"
#include "entities/enemies.h"
#include "entities/pickups.h"
#include "core/rng.h"
#include "core/noise.h"
#include "core/math_utils.h"
#include "core/settings.h"
#include <stdio.h>
#include <string.h>

#define V(x, y, z) { x, y, z }

const RegionDef REGIONS[REGION_COUNT] = {
    [REGION_BLOOD] = {
        .name = "THE BLOODSTREAM", .subtitle = "Region I  -  Vascular System",
        .intro = "Warm, fast, crowded. Ride the current, find endothelial cells along the vessel walls and make them yours.",
        .fogColor = V(0.20f, 0.012f, 0.025f), .fogDensity = 0.0072f,
        .ambient = V(0.55f, 0.20f, 0.20f), .sunColor = V(1.0f, 0.55f, 0.45f), .sunDir = V(0.3f, -1.0f, 0.2f),
        .look = { .base = { 150, 22, 32, 255 }, .accent = { 92, 8, 20, 255 }, .detail = V(1.25f, 0.6f, 0.6f), .vein = V(0.32f, 0.02f, 0.05f),
                  .caustic = V(0.9f, 0.22f, 0.18f), .rim = V(0.9f, 0.25f, 0.2f), .veinGlow = 0.0f, .wallPulse = 0.35f, .spec = 0.6f, .rimStrength = 0.25f },
        .flow = 9.0f, .drifters = 1400, .platelets = 220, .hosts = 60, .bacteria = 0,
        .hostRadiusMin = 2.2f, .hostRadiusMax = 3.0f, .hostColor = V(0.95f, 0.72f, 0.72f),
        .enemyBase = 12, .weights = { 6, 1, 0.3f, 1, 0 },
        .events = { EV_FEVER, EV_CLOTTING, EV_ANTIBIOTIC }, .boss = BOSS_SENTINEL,
        .lift = V(0.02f, 0.0f, 0.0f), .gain = V(1.05f, 0.95f, 0.95f), .tint = V(1.04f, 0.97f, 0.97f), .saturation = 1.1f, .contrast = 1.08f,
        .hazeColor = V(0.6f, 0.08f, 0.08f), .haze = 0.35f, .moteA = V(1.2f, 0.28f, 0.22f), .moteB = V(1.0f, 0.6f, 0.4f),
        .tubeRadius = 16.0f, .tubeVar = 4.0f, .noiseAmp = 2.2f, .noiseFreq = 0.04f, .noiseRidged = 0.1f,
        .codexHost = CX_ENDOTHELIAL, .codexDrifter = CX_RBC, .bossRadius = 42.0f,
        .musicRoot = 110.0f, .musicTempo = 84, .musicMode = 0, .drifterRadius = 1.15f, .drifterColor = V(0.85f, 0.12f, 0.12f) },
    [REGION_LUNGS] = {
        .name = "THE LUNGS", .subtitle = "Region II  -  Respiratory Tract",
        .intro = "Air sacs and mucus. Cilia sweep intruders back toward the throat, and alveolar macrophages patrol every chamber.",
        .fogColor = V(0.30f, 0.18f, 0.22f), .fogDensity = 0.0064f,
        .ambient = V(0.52f, 0.38f, 0.44f), .sunColor = V(0.9f, 0.76f, 0.74f), .sunDir = V(-0.2f, -1.0f, 0.3f),
        .look = { .base = { 210, 130, 145, 255 }, .accent = { 160, 80, 100, 255 }, .detail = V(1.1f, 0.9f, 0.95f), .vein = V(0.6f, 0.15f, 0.25f),
                  .caustic = V(0.8f, 0.6f, 0.65f), .rim = V(1.0f, 0.7f, 0.8f), .veinGlow = 0.0f, .wallPulse = 0.6f, .spec = 0.8f, .rimStrength = 0.3f },
        .flow = 2.5f, .drifters = 420, .platelets = 0, .hosts = 70, .bacteria = 0,
        .hostRadiusMin = 2.2f, .hostRadiusMax = 3.2f, .hostColor = V(0.95f, 0.85f, 0.9f),
        .enemyBase = 16, .weights = { 4, 3, 0.5f, 1, 2 }, .mucus = 12, .cilia = 7,
        .events = { EV_FEVER, EV_COUGH, EV_ANTIBIOTIC }, .boss = BOSS_WARDEN,
        .lift = V(0.01f, 0.0f, 0.02f), .gain = V(1.0f, 0.97f, 1.0f), .tint = V(1.0f, 0.97f, 1.0f), .saturation = 0.95f, .contrast = 1.04f,
        .hazeColor = V(0.7f, 0.5f, 0.56f), .haze = 0.3f, .moteA = V(1.0f, 0.9f, 0.95f), .moteB = V(0.8f, 0.7f, 1.0f),
        .tubeRadius = 13.0f, .tubeVar = 3.0f, .noiseAmp = 2.6f, .noiseFreq = 0.05f, .noiseRidged = 0.2f,
        .codexHost = CX_PNEUMOCYTE, .codexDrifter = CX_SURFACTANT, .bossRadius = 44.0f,
        .musicRoot = 146.83f, .musicTempo = 72, .musicMode = 1, .drifterRadius = 0.8f, .drifterColor = V(0.95f, 0.88f, 0.92f) },
    [REGION_GUT] = {
        .name = "THE GUT", .subtitle = "Region III  -  Digestive Tract",
        .intro = "Acid, enzymes and a trillion bacteria. Peristaltic waves surge down the tract. Some of the flora might even help you.",
        .fogColor = V(0.18f, 0.11f, 0.03f), .fogDensity = 0.0078f,
        .ambient = V(0.42f, 0.30f, 0.16f), .sunColor = V(0.85f, 0.62f, 0.34f), .sunDir = V(0.1f, -1.0f, -0.3f),
        .look = { .base = { 165, 92, 48, 255 }, .accent = { 100, 48, 22, 255 }, .detail = V(1.2f, 0.9f, 0.6f), .vein = V(0.4f, 0.2f, 0.05f),
                  .caustic = V(0.9f, 0.7f, 0.3f), .rim = V(0.9f, 0.6f, 0.3f), .veinGlow = 0.0f, .wallPulse = 0.5f, .spec = 1.0f, .rimStrength = 0.3f },
        .flow = 3.0f, .drifters = 460, .platelets = 0, .hosts = 75, .bacteria = 120,
        .hostRadiusMin = 2.4f, .hostRadiusMax = 3.3f, .hostColor = V(0.95f, 0.85f, 0.65f),
        .enemyBase = 18, .weights = { 4, 2, 2, 2, 1 }, .acidPools = 9,
        .events = { EV_FEVER, EV_PERISTALSIS, EV_ANTIBIOTIC }, .boss = BOSS_LEVIATHAN,
        .lift = V(0.02f, 0.01f, 0.0f), .gain = V(1.04f, 1.0f, 0.92f), .tint = V(1.04f, 1.0f, 0.9f), .saturation = 1.05f, .contrast = 1.06f,
        .hazeColor = V(0.5f, 0.36f, 0.12f), .haze = 0.22f, .moteA = V(1.0f, 0.8f, 0.3f), .moteB = V(0.6f, 1.0f, 0.4f),
        .tubeRadius = 21.0f, .tubeVar = 5.0f, .noiseAmp = 3.6f, .noiseFreq = 0.045f, .noiseRidged = 0.6f,
        .codexHost = CX_ENTEROCYTE, .codexDrifter = CX_CHYME, .bossRadius = 46.0f,
        .musicRoot = 92.5f, .musicTempo = 92, .musicMode = 2, .drifterRadius = 0.9f, .drifterColor = V(0.6f, 0.42f, 0.2f) },
    [REGION_LYMPH] = {
        .name = "THE LYMPH NODES", .subtitle = "Region IV  -  Lymphatic System",
        .intro = "The immune system's barracks. Every node is a fortress full of lymphocytes. Expect resistance everywhere.",
        .fogColor = V(0.05f, 0.11f, 0.07f), .fogDensity = 0.0082f,
        .ambient = V(0.30f, 0.46f, 0.36f), .sunColor = V(0.72f, 1.0f, 0.82f), .sunDir = V(0.4f, -1.0f, 0.1f),
        .look = { .base = { 175, 190, 135, 255 }, .accent = { 100, 125, 80, 255 }, .detail = V(0.9f, 1.1f, 0.85f), .vein = V(0.3f, 0.6f, 0.25f),
                  .caustic = V(0.5f, 0.9f, 0.6f), .rim = V(0.6f, 1.0f, 0.7f), .veinGlow = 0.35f, .wallPulse = 0.2f, .spec = 0.7f, .rimStrength = 0.3f },
        .flow = 3.5f, .drifters = 380, .platelets = 0, .hosts = 80, .bacteria = 0,
        .hostRadiusMin = 2.0f, .hostRadiusMax = 2.8f, .hostColor = V(0.85f, 0.95f, 0.8f),
        .enemyBase = 30, .weights = { 4, 2, 2, 3, 3 },
        .events = { EV_SURGE, EV_FEVER, EV_ANTIBIOTIC }, .boss = BOSS_CITADEL,
        .lift = V(0.0f, 0.015f, 0.0f), .gain = V(0.97f, 1.04f, 0.98f), .tint = V(0.95f, 1.05f, 0.97f), .saturation = 0.9f, .contrast = 1.1f,
        .hazeColor = V(0.3f, 0.6f, 0.4f), .haze = 0.4f, .moteA = V(0.6f, 1.2f, 0.7f), .moteB = V(0.9f, 1.0f, 0.6f),
        .tubeRadius = 10.0f, .tubeVar = 2.0f, .noiseAmp = 2.2f, .noiseFreq = 0.05f, .noiseRidged = 0.3f,
        .codexHost = CX_LYMPHOCYTE, .codexDrifter = CX_LYMPHGLOB, .bossRadius = 46.0f,
        .musicRoot = 130.81f, .musicTempo = 100, .musicMode = 0, .drifterRadius = 0.75f, .drifterColor = V(0.9f, 0.95f, 0.75f) },
    [REGION_BRAIN] = {
        .name = "THE BRAIN", .subtitle = "Region V  -  Central Nervous System",
        .intro = "Behind the blood-brain barrier, thought itself crackles along the walls. Microglia guard the mind. End this here.",
        .fogColor = V(0.035f, 0.025f, 0.09f), .fogDensity = 0.0072f,
        .ambient = V(0.26f, 0.22f, 0.42f), .sunColor = V(0.6f, 0.52f, 0.85f), .sunDir = V(-0.3f, -1.0f, -0.2f),
        .look = { .base = { 140, 120, 170, 255 }, .accent = { 70, 55, 110, 255 }, .detail = V(1.0f, 0.9f, 1.2f), .vein = V(0.3f, 0.6f, 1.4f),
                  .caustic = V(0.5f, 0.5f, 1.2f), .rim = V(0.6f, 0.5f, 1.2f), .veinGlow = 0.7f, .wallPulse = 0.15f, .spec = 0.9f, .rimStrength = 0.35f },
        .flow = 1.2f, .drifters = 520, .platelets = 0, .hosts = 70, .bacteria = 0,
        .hostRadiusMin = 2.2f, .hostRadiusMax = 2.8f, .hostColor = V(0.85f, 0.8f, 1.0f),
        .enemyBase = 24, .weights = { 3, 2, 3, 3, 1 },
        .events = { EV_STORM, EV_FEVER, EV_ANTIBIOTIC }, .boss = BOSS_OVERMIND,
        .lift = V(0.0f, 0.0f, 0.02f), .gain = V(0.97f, 0.97f, 1.06f), .tint = V(0.95f, 0.95f, 1.08f), .saturation = 1.1f, .contrast = 1.12f,
        .hazeColor = V(0.35f, 0.25f, 0.75f), .haze = 0.35f, .moteA = V(0.6f, 0.7f, 1.6f), .moteB = V(1.2f, 0.6f, 1.4f),
        .tubeRadius = 11.0f, .tubeVar = 3.0f, .noiseAmp = 3.0f, .noiseFreq = 0.05f, .noiseRidged = 0.8f,
        .codexHost = CX_NEURON, .codexDrifter = CX_VESICLE, .bossRadius = 50.0f,
        .musicRoot = 82.41f, .musicTempo = 66, .musicMode = 3, .drifterRadius = 0.6f, .drifterColor = V(0.85f, 0.8f, 1.45f) },
};

const RegionDef *Region_Def(void) { return &REGIONS[G.region.index]; }

static Vector3 sSecrets[MAX_BRANCHES * 2];
static int sSecretCount = 0;
int Region_SecretCount(void) { return sSecretCount; }

/* ------------------------------------------------------------------------- */
/* Layout                                                                     */
/* ------------------------------------------------------------------------- */

static Vector3 Box(Vector3 p)
{
    float mx = WORLD_SX * 0.5f - 42.0f, my = WORLD_SY * 0.5f - 26.0f, mz = WORLD_SZ * 0.5f - 42.0f;
    return (Vector3){ Clamp(p.x, -mx, mx), Clamp(p.y, -my, my), Clamp(p.z, -mz, mz) };
}

static bool InBox(Vector3 p)
{
    float mx = WORLD_SX * 0.5f - 42.0f, my = WORLD_SY * 0.5f - 26.0f, mz = WORLD_SZ * 0.5f - 42.0f;
    return fabsf(p.x) <= mx && fabsf(p.y) <= my && fabsf(p.z) <= mz;
}

static Vector3 DirYP(float yaw, float pitch) { return (Vector3){ cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw) }; }

/* Random-walk control points, then Catmull-Rom resample at ~7 units. */
static int WalkPath(Rng *r, Vector3 start, float yaw, int ctrlCount, float stepMin, float stepMax, float turn, Vector3 *out, int maxOut)
{
    Vector3 ctrl[24];
    int nc = 0;
    ctrl[nc++] = start;
    float pitch = 0.0f;
    for (int i = 1; i < ctrlCount && nc < 24; i++) {
        Vector3 prev = ctrl[nc - 1], next = prev;
        for (int attempt = 0; attempt < 12; attempt++) {
            float y2 = yaw + Rng_Range(r, -turn, turn);
            float p2 = Clamp(pitch + Rng_Range(r, -0.3f, 0.3f), -0.35f, 0.35f);
            next = Vector3Add(prev, Vector3Scale(DirYP(y2, p2), Rng_Range(r, stepMin, stepMax)));
            bool clear = InBox(next);
            for (int q = 0; q < nc - 1 && clear; q++) if (Vector3Distance(next, ctrl[q]) < stepMin * 0.9f) clear = false;
            if (clear) { yaw = y2; pitch = p2; break; }
            yaw = atan2f(-prev.z, -prev.x) + Rng_Range(r, -0.6f, 0.6f);   /* steer back toward the centre */
            if (attempt == 11) { next = Box(next); yaw = y2; pitch = p2; }
        }
        ctrl[nc++] = next;
    }
    int n = 0;
    for (int i = 0; i < nc - 1 && n < maxOut - 1; i++) {
        Vector3 p0 = ctrl[i > 0 ? i - 1 : 0], p1 = ctrl[i], p2 = ctrl[i + 1], p3 = ctrl[i + 2 < nc ? i + 2 : nc - 1];
        int steps = (int)(Vector3Distance(p1, p2) / 7.0f) + 1;
        for (int k = 0; k < steps && n < maxOut - 1; k++) out[n++] = MU_CatmullRom(p0, p1, p2, p3, (float)k / (float)steps);
    }
    out[n++] = ctrl[nc - 1];
    return n;
}

static void AddShape(RegionState *rg, Vector3 a, Vector3 b, float r0, float r1, int kind, float s0, float s1, int branch)
{
    if (rg->shapeCount >= MAX_SHAPES) return;
    rg->shapes[rg->shapeCount++] = (CaveShape){ a, b, r0, r1, (uint8_t)kind, s0, s1, branch };
    if (kind == 2) Cave_StampSolid(a, r0);
    else Cave_StampCapsule(a, b, r0, r1);
}

static float RadiusAt(const RegionDef *d, float s, float phase)
{
    return d->tubeRadius + d->tubeVar * (0.6f * sinf(s * 0.031f + phase) + 0.4f * sinf(s * 0.083f + phase * 2.3f));
}

Vector3 Region_PathPoint(float s, Vector3 *tangent, float *radius)
{
    RegionState *rg = &G.region;
    if (rg->pathCount < 2) { if (tangent) *tangent = (Vector3){ 1, 0, 0 }; if (radius) *radius = 10.0f; return (Vector3){ 0 }; }
    s = Clamp(s, 0.0f, rg->pathLength);
    int i = 0;
    while (i < rg->pathCount - 2 && rg->pathS[i + 1] < s) i++;
    float t = (s - rg->pathS[i]) / fmaxf(0.001f, rg->pathS[i + 1] - rg->pathS[i]);
    Vector3 p = Vector3Lerp(rg->path[i], rg->path[i + 1], t);
    if (tangent) *tangent = Vector3Normalize(Vector3Subtract(rg->path[i + 1], rg->path[i]));
    if (radius) *radius = RadiusAt(Region_Def(), s, (float)(rg->seed % 100));
    return p;
}

static void BuildLayout(Rng *r, const RegionDef *d, RegionState *rg)
{
    float phase = (float)(rg->seed % 100);
    Vector3 start = { -WORLD_SX * 0.5f + 48.0f, Rng_Range(r, -12, 12), Rng_Range(r, -70, 70) };
    int ctrl = (rg->index == REGION_BRAIN) ? 12 : 11;
    rg->pathCount = WalkPath(r, start, Rng_Range(r, -0.4f, 0.4f), ctrl, 34.0f, 44.0f, 0.85f, rg->path, MAX_PATH);
    rg->pathS[0] = 0.0f;
    for (int i = 1; i < rg->pathCount; i++) rg->pathS[i] = rg->pathS[i - 1] + Vector3Distance(rg->path[i - 1], rg->path[i]);
    rg->pathLength = rg->pathS[rg->pathCount - 1];

    /* main tube */
    for (int i = 0; i < rg->pathCount - 1; i++) {
        float r0 = RadiusAt(d, rg->pathS[i], phase), r1 = RadiusAt(d, rg->pathS[i + 1], phase);
        AddShape(rg, rg->path[i], rg->path[i + 1], r0, r1, 0, rg->pathS[i], rg->pathS[i + 1], -1);
    }
    rg->startPos = rg->path[0];
    AddShape(rg, rg->startPos, rg->startPos, 17.0f, 17.0f, 1, 0, 0, -1);

    /* boss arena at the end of the path */
    Vector3 endDir = Vector3Normalize(Vector3Subtract(rg->path[rg->pathCount - 1], rg->path[rg->pathCount - 2]));
    rg->bossRadius = d->bossRadius;
    rg->bossPos = Box(Vector3Add(rg->path[rg->pathCount - 1], Vector3Scale(endDir, d->bossRadius * 0.7f)));
    rg->bossPos.y = Clamp(rg->bossPos.y, -WORLD_SY * 0.5f + d->bossRadius * 0.55f + 6.0f, WORLD_SY * 0.5f - d->bossRadius * 0.55f - 6.0f);
    AddShape(rg, rg->path[rg->pathCount - 1], rg->bossPos, RadiusAt(d, rg->pathLength, phase), d->bossRadius * 0.55f, 0, rg->pathLength, rg->pathLength + 10.0f, -1);
    AddShape(rg, rg->bossPos, rg->bossPos, d->bossRadius * 0.6f, d->bossRadius * 0.6f, 1, 0, 0, -1);
    for (int k = 0; k < 5; k++) {
        Vector3 o = Rng_UnitVec(r);
        o.y *= 0.35f;
        Vector3 c = Box(Vector3Add(rg->bossPos, Vector3Scale(o, d->bossRadius * 0.45f)));
        AddShape(rg, c, c, d->bossRadius * 0.42f, d->bossRadius * 0.42f, 1, 0, 0, -1);
    }
    rg->portalPos = rg->bossPos;

    /* branches with secret chambers */
    sSecretCount = 0;
    int branches = (rg->index == REGION_LUNGS) ? 3 : (rg->index == REGION_BRAIN) ? 4 : 4;
    for (int b = 0; b < branches; b++) {
        int idx = Rng_Int(r, (int)(rg->pathCount * 0.15f), (int)(rg->pathCount * 0.8f));
        Vector3 tng = Vector3Normalize(Vector3Subtract(rg->path[idx + 1], rg->path[idx]));
        Vector3 side = Vector3Normalize(Vector3CrossProduct(tng, Rng_UnitVec(r)));
        float yaw = atan2f(side.z, side.x);
        Vector3 bp[MAX_PATH];
        int n = WalkPath(r, rg->path[idx], yaw, Rng_Int(r, 3, 4), 24.0f, 32.0f, 0.6f, bp, MAX_PATH);
        float s = rg->pathS[idx];
        float br = RadiusAt(d, s, phase) * 0.55f;
        for (int i = 0; i < n - 1; i++) {
            float s0 = s + (float)i * 7.0f;
            AddShape(rg, bp[i], bp[i + 1], br, br * 0.9f, 0, s0, s0 + 7.0f, b);
        }
        Vector3 endp = bp[n - 1];
        AddShape(rg, endp, endp, Rng_Range(r, 9.0f, 12.0f), 0, 1, 0, 0, b);
        if (sSecretCount < MAX_BRANCHES * 2) sSecrets[sSecretCount++] = endp;
    }

    /* one shortcut: connect two path points that are far apart along the path but close in space */
    float bestD = 1e9f;
    int bi = -1, bj = -1;
    for (int i = 0; i < rg->pathCount; i += 3)
        for (int j = i + 1; j < rg->pathCount; j += 3) {
            if (rg->pathS[j] - rg->pathS[i] < 170.0f) continue;
            float dd = Vector3Distance(rg->path[i], rg->path[j]);
            if (dd < 120.0f && dd < bestD) { bestD = dd; bi = i; bj = j; }
        }
    if (bi >= 0) {
        Vector3 mid = Vector3Add(Vector3Scale(Vector3Add(rg->path[bi], rg->path[bj]), 0.5f), Vector3Scale(Rng_UnitVec(r), 10.0f));
        AddShape(rg, rg->path[bi], mid, 7.0f, 6.5f, 0, rg->pathS[bi], rg->pathS[bi], 7);
        AddShape(rg, mid, rg->path[bj], 6.5f, 7.0f, 0, rg->pathS[bj], rg->pathS[bj], 7);
    }

    /* organ features */
    if (rg->index == REGION_LUNGS) {
        for (int c = 0; c < 8; c++) {
            int idx = Rng_Int(r, 4, rg->pathCount - 6);
            Vector3 tng = Vector3Normalize(Vector3Subtract(rg->path[idx + 1], rg->path[idx]));
            Vector3 side = Vector3Normalize(Vector3CrossProduct(tng, Rng_UnitVec(r)));
            Vector3 cen = Vector3Add(rg->path[idx], Vector3Scale(side, RadiusAt(d, rg->pathS[idx], phase) + 7.0f));
            for (int k = 0; k < 7; k++) {
                Vector3 p = Box(Vector3Add(cen, Vector3Scale(Rng_InSphere(r), 9.0f)));
                AddShape(rg, p, p, Rng_Range(r, 5.5f, 8.5f), 0, 1, 0, 0, 6);
            }
        }
    } else if (rg->index == REGION_LYMPH) {
        float fr[3] = { 0.25f, 0.5f, 0.74f };
        for (int c = 0; c < 3; c++) {
            Vector3 p = Region_PathPoint(rg->pathLength * fr[c], NULL, NULL);
            float R = Rng_Range(r, 23.0f, 27.0f);
            AddShape(rg, p, p, R, R, 1, 0, 0, -1);
            for (int k = 0; k < 4; k++) {
                Vector3 o = Rng_UnitVec(r);
                AddShape(rg, Vector3Add(p, Vector3Scale(o, R * 0.85f)), (Vector3){ 0 }, Rng_Range(r, 4.0f, 6.0f), 0, 2, 0, 0, -1);
            }
        }
    } else if (rg->index == REGION_BRAIN) {
        for (int c = 0; c < 9; c++) {
            int idx = Rng_Int(r, 3, rg->pathCount - 4);
            Vector3 dir = Rng_UnitVec(r);
            dir.y *= 0.5f;
            Vector3 a = rg->path[idx];
            Vector3 b = Box(Vector3Add(a, Vector3Scale(Vector3Normalize(dir), Rng_Range(r, 20.0f, 30.0f))));
            AddShape(rg, a, b, 6.5f, 4.5f, 0, rg->pathS[idx], rg->pathS[idx], 5);
            AddShape(rg, b, b, 6.0f, 0, 1, 0, 0, 5);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Population                                                                 */
/* ------------------------------------------------------------------------- */

static AgentKind PickEnemy(Rng *r, const RegionDef *d)
{
    float total = 0.0f;
    for (int i = 0; i < 5; i++) total += d->weights[i];
    float x = Rng_Range(r, 0.0f, total);
    for (int i = 0; i < 5; i++) {
        if (x < d->weights[i]) return (AgentKind)(AK_NEUTROPHIL + i);
        x -= d->weights[i];
    }
    return AK_NEUTROPHIL;
}

static void Populate(Rng *r, const RegionDef *d, RegionState *rg, bool menuOnly)
{
    float density = Settings_DrifterDensity();
    Vector3 p;

    /* ambient drifters fill the fluid */
    int nd = (int)((float)d->drifters * density);
    for (int i = 0; i < nd; i++) if (Cave_RandomOpenPoint(r, 3.0f, (Vector3){ 0 }, 0.0f, &p)) Agents_Spawn(AK_DRIFTER, p);
    int npl = (int)((float)d->platelets * density);
    for (int i = 0; i < npl; i++) if (Cave_RandomOpenPoint(r, 2.0f, (Vector3){ 0 }, 0.0f, &p)) Agents_Spawn(AK_PLATELET, p);

    /* host cells anchored to the walls */
    int surf = Cave_SurfaceCount();
    Vector3 placed[200];
    int np = 0;
    for (int tries = 0; tries < 40000 && np < d->hosts && np < 200 && surf > 0; tries++) {
        int si = Rng_Int(r, 0, surf - 1);
        Vector3 sp = Cave_SurfacePos(si), sn = Cave_SurfaceNormal(si);
        if (Cave_Sample(Vector3Add(sp, Vector3Scale(sn, 6.0f))) < 3.0f) continue;
        if (Vector3Distance(sp, rg->startPos) < 26.0f) continue;
        bool ok = true;
        for (int q = 0; q < np && ok; q++) if (Vector3Distance(placed[q], sp) < 10.0f) ok = false;
        if (!ok) continue;
        float rad = Rng_Range(r, d->hostRadiusMin, d->hostRadiusMax);
        if (Hosts_Spawn(Vector3Add(sp, Vector3Scale(sn, rad * 0.45f)), sn, rad) >= 0) placed[np++] = sp;
    }
    rg->hostTotal = np;
    rg->hostInfected = 0;

    for (int i = 0; i < d->bacteria; i++) {
        if (surf == 0) break;
        int si = Rng_Int(r, 0, surf - 1);
        Vector3 sp = Vector3Add(Cave_SurfacePos(si), Vector3Scale(Cave_SurfaceNormal(si), 3.0f));
        if (Cave_Sample(sp) > 1.5f) Agents_Spawn(AK_BACTERIA, sp);
    }

    /* landmark decor */
    rg->decorCount = 0;
    float ringStep = (rg->index == REGION_LUNGS) ? 22.0f : (rg->index == REGION_BLOOD) ? 55.0f : 0.0f;
    if (ringStep > 0.0f) {
        for (float s = 30.0f; s < rg->pathLength - 20.0f && rg->decorCount < MAX_DECOR; s += ringStep) {
            Vector3 t;
            float rr;
            Vector3 c = Region_PathPoint(s, &t, &rr);
            rg->decor[rg->decorCount++] = (Decor){ 0, c, t, rr * 0.93f, Rng_Range(r, 0, 6.28f) };
        }
    }
    if (rg->index == REGION_BRAIN) {
        for (int i = 0; i < 26 && rg->decorCount < MAX_DECOR && surf > 0; i++) {
            int si = Rng_Int(r, 0, surf - 1);
            rg->decor[rg->decorCount++] = (Decor){ 1, Cave_SurfacePos(si), Cave_SurfaceNormal(si), 1.0f, Rng_Range(r, 0, 6.28f) };
        }
    }

    /* region hazards (static) */
    for (int i = 0; i < d->acidPools; i++) {
        Vector3 c = Region_PathPoint(Rng_Range(r, 60.0f, rg->pathLength - 40.0f), NULL, NULL);
        Vector3 floorp;
        float hit;
        if (Cave_Raycast(c, (Vector3){ 0, -1, 0 }, 40.0f, 0.5f, &hit)) floorp = Vector3Add(c, (Vector3){ 0, -hit + 3.0f, 0 });
        else floorp = c;
        Hazards_Spawn(HZ_ACID_POOL, floorp, Rng_Range(r, 6.0f, 9.0f), -1.0f, 9.0f);
    }
    for (int i = 0; i < d->mucus; i++) {
        Vector3 c = Region_PathPoint(Rng_Range(r, 40.0f, rg->pathLength - 30.0f), NULL, NULL);
        Hazards_Spawn(HZ_MUCUS, Vector3Add(c, Vector3Scale(Rng_InSphere(r), 6.0f)), Rng_Range(r, 6.0f, 10.0f), -1.0f, 0.75f);
    }
    for (int i = 0; i < d->cilia; i++) {
        float s = Rng_Range(r, 50.0f, rg->pathLength - 40.0f);
        Vector3 t;
        float rr;
        Vector3 c = Region_PathPoint(s, &t, &rr);
        int h = Hazards_Spawn(HZ_CILIA, c, rr + 4.0f, -1.0f, 9.0f);
        if (h >= 0) G.hazards[h].dir = Vector3Negate(t);
        int added = 0;
        for (int tries = 0; tries < 3000 && added < 70 && rg->decorCount < MAX_DECOR; tries++) {
            int si = Rng_Int(r, 0, surf - 1);
            Vector3 sp = Cave_SurfacePos(si);
            if (Vector3Distance(sp, c) > rr + 6.0f) continue;
            rg->decor[rg->decorCount++] = (Decor){ 2, sp, Cave_SurfaceNormal(si), Rng_Range(r, 0.8f, 1.3f), Rng_Range(r, 0, 6.28f) };
            added++;
        }
    }

    if (menuOnly) return;

    /* secrets: GM caches and mutation pods at branch ends */
    for (int i = 0; i < sSecretCount; i++) {
        Pickups_Spawn(PK_CACHE, sSecrets[i], 40.0f + 12.0f * (float)rg->index);
        if (i % 2 == 0) Pickups_Spawn(PK_POD, Vector3Add(sSecrets[i], (Vector3){ 0, 3.0f, 0 }), 15.0f);
    }
    for (int i = 0; i < 18; i++) {
        Vector3 c = Region_PathPoint(Rng_Range(r, 30.0f, rg->pathLength), NULL, NULL);
        if (Cave_RandomOpenPoint(r, 2.5f, c, 10.0f, &p)) Pickups_Spawn(i % 3 == 0 ? PK_BIOMASS : PK_ATP, p, i % 3 == 0 ? 15.0f : 25.0f);
    }

    /* immune patrols spread along the path (never at the start) */
    int groups = (int)((float)d->enemyBase * DIFFICULTIES[G.run.difficulty].spawnMul * 0.45f);
    for (int g = 0; g < groups; g++) {
        float s = Rng_Range(r, 90.0f, rg->pathLength * 0.92f);
        Vector3 c = Region_PathPoint(s, NULL, NULL);
        AgentKind k = PickEnemy(r, d);
        int count = (k == AK_NEUTROPHIL) ? Rng_Int(r, 3, 5) : 1;
        for (int i = 0; i < count; i++) {
            if (Cave_RandomOpenPoint(r, 2.5f, c, 8.0f, &p)) Enemies_Spawn(k, p, true);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Entry points                                                               */
/* ------------------------------------------------------------------------- */

void Region_Generate(int index, uint32_t seed, bool menuOnly)
{
    RegionState *rg = &G.region;
    memset(rg, 0, sizeof(*rg));
    rg->index = index;
    rg->seed = seed;
    const RegionDef *d = &REGIONS[index];
    rg->lightDir = Vector3Normalize(d->sunDir);
    rg->flowStrength = d->flow;

    Rng r;
    Rng_Seed(&r, (uint64_t)seed * 7919u + (uint64_t)index * 104729u + 17u);
    Noise_SetSeed(seed ^ (uint32_t)(index * 0x9E3779B1u));

    Agents_Clear();
    Hazards_Clear();
    Pickups_Clear();

    Cave_Clear();
    BuildLayout(&r, d, rg);
    Cave_ApplyNoise(d->noiseAmp, d->noiseFreq, d->noiseRidged, 9.0f);
    /* make sure the start chamber and boss arena stay clear after noise */
    Cave_StampSphere(rg->startPos, 12.0f);
    Cave_StampSphere(rg->bossPos, d->bossRadius * 0.45f);
    Cave_SealBorders();
    Cave_BuildMesh(&d->look);
    Flow_Build(rg, d->flow);
    Flow_SetModifiers(1.0f, -1000.0f, 0.0f, 0.0f);

    Populate(&r, d, rg, menuOnly);

    rg->nextEventT = 70.0f + Rng_Range(&r, 0.0f, 30.0f);
    rg->spawnT = 6.0f;
    G.boss.active = false;
}

float Region_EnemyHpMul(void)
{
    return (1.0f + 0.35f * (float)G.region.index) * DIFFICULTIES[G.run.difficulty].enemyHp * (1.0f + 0.5f * (float)G.run.ngPlus);
}

float Region_EnemyDmgMul(void)
{
    return (1.0f + 0.22f * (float)G.region.index) * DIFFICULTIES[G.run.difficulty].enemyDamage * (1.0f + 0.35f * (float)G.run.ngPlus);
}

bool Region_ObjectivePos(Vector3 *out)
{
    RegionState *rg = &G.region;
    if (G.run.mode == MODE_OUTBREAK) return false;
    if (rg->portalOpen) { *out = rg->portalPos; return true; }
    if (rg->bossSpawned && G.boss.active) { *out = G.boss.pos; return true; }
    if (rg->bossSpawned && !rg->bossDefeated) { *out = rg->bossPos; return true; }
    /* otherwise: nearest uninfected host */
    float best = 1e9f;
    bool found = false;
    for (int i = 0; i < G.agentHigh; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive || a->kind != AK_HOST || (a->flags & AF_INFECTED)) continue;
        float d = Vector3DistanceSqr(a->pos, G.player.pos);
        if (d < best) { best = d; *out = a->pos; found = true; }
    }
    return found;
}

void Region_ObjectiveText(char *buf, int n)
{
    RegionState *rg = &G.region;
    if (G.run.mode == MODE_OUTBREAK) {
        snprintf(buf, (size_t)n, "OUTBREAK  -  survive wave %d", G.run.wave);
    } else if (rg->portalOpen) {
        snprintf(buf, (size_t)n, "Enter the metastasis channel at the heart of the arena");
    } else if (rg->bossSpawned) {
        snprintf(buf, (size_t)n, "The guardian has awakened. Destroy it");
    } else {
        int need = (int)ceilf(BOSS_INFECTION_PCT * (float)rg->hostTotal);
        snprintf(buf, (size_t)n, "Infect host cells  %d / %d   (%d%% awakens the guardian)", rg->hostInfected, need, (int)(BOSS_INFECTION_PCT * 100.0f));
    }
}

void Region_Update(float dt)
{
    RegionState *rg = &G.region;
    rg->time += dt;
    rg->infection = (rg->hostTotal > 0) ? (float)rg->hostInfected / (float)rg->hostTotal : 0.0f;
    if (rg->alertDelay > 0.0f) rg->alertDelay -= dt;
    else rg->alert = fmaxf(0.0f, rg->alert - ALERT_DECAY * G.stats.alertDecayMul * dt);
    rg->peakAlert = fmaxf(rg->peakAlert, rg->alert);
    if (rg->alert > G.run.stats.maxAlert) G.run.stats.maxAlert = rg->alert;
}

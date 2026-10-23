/*
 * render.c - scene rendering.
 *
 * Instancing goes straight through rlgl so it behaves identically on raylib
 * 5.5 and 6.x: one ring-buffered dynamic VBO holds per-instance mat4s, the
 * attribute pointers are attached to each mesh's VAO with divisor 1 for the
 * duration of the draw and removed afterwards.
 *
 * Frame order:
 *   lights + frame uniforms -> cave -> decor -> opaque instanced groups ->
 *   boss + player (single meshes) -> translucent membranes & hazards ->
 *   particles (soft billboards, alpha then additive).
 */
#include "gfx/render.h"
#include "gfx/meshgen.h"
#include "gfx/lights.h"
#include "gfx/particles.h"
#include "gfx/camera.h"
#include "world/sdf_cave.h"
#include "world/region.h"
#include "entities/enemies.h"
#include "entities/bosses.h"
#include "entities/minions.h"
#include "systems/evolution.h"
#include "systems/mutation.h"
#include "audio/music.h"
#include "core/math_utils.h"
#include "core/settings.h"
#include <string.h>
#include <stdlib.h>

#define RING_INSTANCES 32768
#define SCRATCH 9000

static unsigned int sInstVbo = 0;
static int sRingOffset = 0;
static InstData sScratch[SCRATCH];
static int sDrawCalls = 0, sInstances = 0;
static bool sReady = false;

/* mesh library */
static Mesh mQuad, mSphere, mSphereHi, mTorus, mTorusThin, mCilium, mCrystal, mFlagSeg;
static Mesh mDrifter[REGION_COUNT], mHost[REGION_COUNT], mPlatelet, mBacteria;
static Mesh mVirion, mNeutroShell, mNeutroNucleus, mGranules, mNKShell, mNKNucleus, mNKGranules;
static Mesh mMacrophage, mTCell, mBCell, mAntibody, mClot, mSegment;
static Mesh mBoss[BOSS_COUNT];
static Mesh mPlayer;
static bool mPlayerValid = false;
static Material sLitMat;

Mesh Render_QuadMesh(void) { return mQuad; }
int Render_DrawCalls(void) { return sDrawCalls; }
int Render_Instances(void) { return sInstances; }
void Render_ResetCounters(void) { sDrawCalls = 0; sInstances = 0; }

/* ------------------------------------------------------------------------- */
/* Instancing core                                                            */
/* ------------------------------------------------------------------------- */

void Render_DrawInstanced(Mesh mesh, ShaderId sid, const InstData *data, int count)
{
    if (count <= 0 || mesh.vaoId == 0 || sInstVbo == 0) return;
    Shader sh = Shaders_Get(sid);
    const ShaderLocs *L = Shaders_Locs(sid);
    if (L->instance < 0) return;
    rlDrawRenderBatchActive();
    Matrix mvp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    rlEnableShader(sh.id);
    rlSetUniformMatrix(L->mvp, mvp);
    int loc = L->instance;
    int done = 0;
    while (done < count) {
        int n = count - done;
        if (n > INSTANCE_CAP) n = INSTANCE_CAP;
        int bytes = n * (int)sizeof(InstData);
        if (sRingOffset + bytes > RING_INSTANCES * (int)sizeof(InstData)) sRingOffset = 0;
        rlUpdateVertexBuffer(sInstVbo, data + done, bytes, sRingOffset);
        rlEnableVertexArray(mesh.vaoId);
        rlEnableVertexBuffer(sInstVbo);
        for (int i = 0; i < 4; i++) {
            rlEnableVertexAttribute((unsigned int)(loc + i));
            rlSetVertexAttribute((unsigned int)(loc + i), 4, RL_FLOAT, false, (int)sizeof(InstData), sRingOffset + i * 16);
            rlSetVertexAttributeDivisor((unsigned int)(loc + i), 1);
        }
        if (mesh.indices) rlDrawVertexArrayElementsInstanced(0, mesh.triangleCount * 3, 0, n);
        else rlDrawVertexArrayInstanced(0, mesh.vertexCount, n);
        for (int i = 0; i < 4; i++) {
            rlSetVertexAttributeDivisor((unsigned int)(loc + i), 0);
            rlDisableVertexAttribute((unsigned int)(loc + i));
        }
        rlDisableVertexArray();
        rlDisableVertexBuffer();
        sRingOffset += bytes;
        done += n;
        sDrawCalls++;
        sInstances += n;
    }
    rlDisableShader();
}

/* ------------------------------------------------------------------------- */
/* Setup                                                                      */
/* ------------------------------------------------------------------------- */

static Mesh BlobMesh(float lowAmp, float lowFreq, float hiAmp, float hiFreq, float ridged, float flatten, uint32_t seed, Color a, Color b, int rings, int slices)
{
    BlobParams bp = { .lowAmp = lowAmp, .lowFreq = lowFreq, .hiAmp = hiAmp, .hiFreq = hiFreq, .ridged = ridged, .flatten = flatten, .seed = seed, .colA = a, .colB = b };
    return MeshGen_Blob(&bp, rings, slices);
}

bool Render_Init(void)
{
    sInstVbo = rlLoadVertexBuffer(NULL, RING_INSTANCES * (int)sizeof(InstData), true);
    mQuad = MeshGen_Quad();
    mSphere = MeshGen_Sphere(8, 12);
    mSphereHi = MeshGen_Sphere(20, 30);
    mTorus = MeshGen_Torus(1.0f, 0.08f, 48, 10);
    mTorusThin = MeshGen_Torus(1.0f, 0.03f, 64, 6);
    mCilium = MeshGen_Cilium();
    mCrystal = MeshGen_Crystal((Color){ 255, 255, 255, 255 }, (Color){ 200, 200, 210, 255 });
    {
        MeshBuilder mb;
        MB_Init(&mb, 200, 400);
        MB_AddCylinder(&mb, MatrixIdentity(), 1.0f, 0.9f, 6, (Color){ 200, 255, 225, 255 }, (Color){ 220, 255, 235, 255 });
        mFlagSeg = MB_Upload(&mb);
    }

    Color w0 = { 250, 248, 250, 255 }, w1 = { 205, 200, 210, 255 };
    mDrifter[REGION_BLOOD] = MeshGen_RBC();
    mDrifter[REGION_LUNGS] = BlobMesh(0.10f, 1.5f, 0.03f, 6.0f, 0.0f, 0.0f, 31, w0, w1, 10, 14);
    mDrifter[REGION_GUT] = BlobMesh(0.34f, 1.8f, 0.12f, 5.0f, 0.5f, 0.15f, 32, w0, (Color){ 170, 160, 150, 255 }, 10, 14);
    mDrifter[REGION_LYMPH] = BlobMesh(0.05f, 2.0f, 0.02f, 7.0f, 0.0f, 0.0f, 33, w0, w1, 8, 12);
    mDrifter[REGION_BRAIN] = BlobMesh(0.03f, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f, 34, w0, w0, 8, 12);
    mPlatelet = MeshGen_Platelet();

    mHost[REGION_BLOOD] = BlobMesh(0.10f, 1.6f, 0.04f, 6.0f, 0.2f, 0.55f, 41, (Color){ 255, 235, 235, 255 }, (Color){ 215, 175, 185, 255 }, 18, 26);
    mHost[REGION_LUNGS] = BlobMesh(0.12f, 1.4f, 0.07f, 9.0f, 0.4f, 0.25f, 42, (Color){ 255, 245, 250, 255 }, (Color){ 215, 195, 215, 255 }, 18, 26);
    mHost[REGION_GUT] = BlobMesh(0.08f, 1.5f, 0.10f, 12.0f, 0.6f, -0.45f, 43, (Color){ 255, 245, 225, 255 }, (Color){ 210, 185, 150, 255 }, 20, 26);
    mHost[REGION_LYMPH] = BlobMesh(0.06f, 2.0f, 0.03f, 8.0f, 0.0f, 0.0f, 44, (Color){ 250, 255, 245, 255 }, (Color){ 200, 220, 195, 255 }, 16, 24);
    mHost[REGION_BRAIN] = MeshGen_Neuron(45);

    mBacteria = MeshGen_Bacteria();
    mVirion = MeshGen_Virion(10, (Color){ 120, 255, 190, 255 }, (Color){ 210, 255, 240, 255 });
    mNeutroShell = BlobMesh(0.12f, 1.6f, 0.05f, 8.0f, 0.3f, 0.0f, 51, (Color){ 245, 240, 255, 255 }, (Color){ 210, 200, 235, 255 }, 16, 22);
    mNeutroNucleus = MeshGen_Nucleus(3, 52, (Color){ 140, 90, 190, 255 });
    mGranules = MeshGen_Granules(53, 30, (Color){ 235, 215, 255, 255 });
    mNKShell = BlobMesh(0.08f, 1.8f, 0.04f, 7.0f, 0.2f, 0.0f, 54, (Color){ 255, 235, 235, 255 }, (Color){ 220, 190, 195, 255 }, 16, 22);
    mNKNucleus = MeshGen_Nucleus(1, 55, (Color){ 150, 60, 90, 255 });
    mNKGranules = MeshGen_Granules(56, 16, (Color){ 255, 80, 80, 255 });
    mMacrophage = MeshGen_Macrophage(61, 1.0f);
    mTCell = MeshGen_TCell();
    mBCell = MeshGen_BCell();
    mAntibody = MeshGen_Antibody((Color){ 255, 235, 150, 255 });
    mClot = BlobMesh(0.35f, 1.4f, 0.15f, 5.0f, 0.7f, 0.0f, 71, (Color){ 140, 20, 25, 255 }, (Color){ 80, 10, 15, 255 }, 16, 22);
    mSegment = BlobMesh(0.10f, 2.0f, 0.06f, 6.0f, 0.4f, 0.1f, 72, (Color){ 210, 170, 100, 255 }, (Color){ 130, 90, 45, 255 }, 16, 22);
    for (int b = 0; b < BOSS_COUNT; b++) mBoss[b] = MeshGen_BossBody(b);

    sLitMat = LoadMaterialDefault();
    sLitMat.shader = Shaders_Get(SHD_LIT);
    sReady = true;
    return sInstVbo != 0;
}

void Render_Shutdown(void)
{
    if (!sReady) return;
    Mesh *all[] = { &mQuad, &mSphere, &mSphereHi, &mTorus, &mTorusThin, &mCilium, &mCrystal, &mFlagSeg, &mPlatelet, &mBacteria, &mVirion, &mNeutroShell, &mNeutroNucleus,
                    &mGranules, &mNKShell, &mNKNucleus, &mNKGranules, &mMacrophage, &mTCell, &mBCell, &mAntibody, &mClot, &mSegment };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) UnloadMesh(*all[i]);
    for (int i = 0; i < REGION_COUNT; i++) { UnloadMesh(mDrifter[i]); UnloadMesh(mHost[i]); }
    for (int i = 0; i < BOSS_COUNT; i++) UnloadMesh(mBoss[i]);
    if (mPlayerValid) UnloadMesh(mPlayer);
    sLitMat.shader = (Shader){ 0 };
    MemFree(sLitMat.maps);
    if (sInstVbo) rlUnloadVertexBuffer(sInstVbo);
    sInstVbo = 0;
    sReady = false;
}

void Render_RebuildPlayer(void)
{
    if (mPlayerValid) UnloadMesh(mPlayer);
    mPlayer = MeshGen_PlayerCore(&G.stats, G.player.stage);
    mPlayerValid = true;
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static Vector3 sCamPos, sCamFwd;
static float sTime;

static bool Visible(Vector3 p, float r, float maxDist)
{
    Vector3 to = Vector3Subtract(p, sCamPos);
    float d2 = Vector3LengthSqr(to);
    if (d2 > (maxDist + r) * (maxDist + r)) return false;
    float along = Vector3DotProduct(to, sCamFwd);
    if (along < -r) return false;
    float d = sqrtf(d2);
    if (d < r + 2.0f) return true;
    return along / d > 0.42f - r / d;
}

static MaterialParams Mat(Vector3 rim, float rimStrength, float sss, float spec)
{
    MaterialParams m = Shaders_DefaultMaterial();
    m.rimColor = rim;
    m.rimStrength = rimStrength;
    m.sss = sss;
    m.spec = spec;
    return m;
}

static void Flush(Mesh mesh, ShaderId s, const MaterialParams *m, int n)
{
    if (n <= 0) return;
    Shaders_SetMaterial(s, m);
    Render_DrawInstanced(mesh, s, sScratch, n);
}

static Matrix ScaleY(Matrix m, float s) { m.m4 *= s; m.m5 *= s; m.m6 *= s; return m; }

static Vector3 Lerp3(Vector3 a, Vector3 b, float t) { return Vector3Lerp(a, b, t); }

static inline Vector3 AgentPos(const Agent *a) { return Lerp3(a->prev, a->pos, G.alpha); }

static void DrawLit(Mesh m, Matrix xf, const MaterialParams *mp, Color tint)
{
    Shaders_SetMaterial(SHD_LIT, mp);
    sLitMat.shader = Shaders_Get(SHD_LIT);
    sLitMat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    DrawMesh(m, sLitMat, xf);
    sDrawCalls++;
}

/* ------------------------------------------------------------------------- */
/* Lights                                                                     */
/* ------------------------------------------------------------------------- */

static void GatherLights(void)
{
    Player *p = &G.player;
    Lights_Begin();
    if (G.run.active && !p->dead) {
        float stageGlow = 0.45f + 0.15f * (float)p->stage;
        Vector3 c = p->cloaked ? (Vector3){ 0.2f, 0.2f, 0.5f } : (Vector3){ 0.3f * stageGlow, 1.0f * stageGlow, 0.6f * stageGlow };
        Lights_Add(Vector3Lerp(p->prev, p->pos, G.alpha), c, 10.0f + p->radius * 6.0f);
    }
    for (int i = 0; i < G.agentHigh; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive) continue;
        if (a->kind == AK_HOST && (a->flags & AF_INFECTED)) {
            if (Vector3DistanceSqr(a->pos, sCamPos) < 110.0f * 110.0f) {
                float pulse = 0.75f + 0.25f * sinf(sTime * 3.0f + a->phase * 6.28f);
                Lights_Add(Vector3Add(a->pos, Vector3Scale(a->anchor, a->radius)), (Vector3){ 0.15f * pulse, 0.8f * pulse, 0.4f * pulse }, 8.0f + a->radius * 2.0f);
            }
        } else if (a->kind == AK_NK && a->state == AI_WINDUP) {
            Lights_Add(a->pos, (Vector3){ 2.5f, 0.3f, 0.3f }, 9.0f);
        } else if (a->kind == AK_BOSS_PART) {
            Lights_Add(a->pos, (Vector3){ 0.6f, 1.6f, 2.6f }, 10.0f);
        }
    }
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *pr = &G.proj[i];
        if (!pr->alive) continue;
        if (pr->type == PT_BOSS_ORB || pr->type == PT_BOLT || pr->type == PT_PERFORIN) {
            Vector3 c = pr->type == PT_PERFORIN ? (Vector3){ 1.6f, 0.3f, 0.3f } : (pr->type == PT_BOLT ? (Vector3){ 0.6f, 0.9f, 2.4f } : (Vector3){ 1.5f, 0.6f, 1.4f });
            Lights_Add(pr->pos, c, 6.0f);
        } else if (pr->team == TEAM_PLAYER && (i % 3) == 0) {
            Lights_Add(pr->pos, (Vector3){ 0.3f, 1.2f, 0.4f }, 5.0f);
        }
    }
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *k = &G.pickups[i];
        if (!k->alive) continue;
        if (k->type == PK_CACHE) Lights_Add(k->pos, (Vector3){ 0.6f, 2.0f, 2.6f }, 14.0f);
        else if (k->type == PK_POD) Lights_Add(k->pos, (Vector3){ 2.4f, 0.8f, 2.6f }, 14.0f);
    }
    for (int i = 0; i < MAX_HAZARDS; i++) {
        Hazard *h = &G.hazards[i];
        if (!h->alive) continue;
        float w = 0.5f + 0.5f * sinf(sTime * 12.0f);
        switch (h->type) {
            case HZ_ACID_POOL: Lights_Add(h->pos, (Vector3){ 0.6f, 1.6f, 0.2f }, h->radius * 2.0f); break;
            case HZ_ANTIBIOTIC: if (!h->triggered) Lights_Add(h->pos, (Vector3){ 2.0f * w, 0.4f * w, 0.2f * w }, h->radius * 2.0f); break;
            case HZ_STORM: if (!h->triggered) Lights_Add(h->pos, (Vector3){ 0.4f * w, 0.6f * w, 2.4f * w }, h->radius * 2.0f); break;
            case HZ_VORTEX: Lights_Add(h->pos, (Vector3){ 1.4f, 0.5f, 1.8f }, h->radius); break;
            default: break;
        }
    }
    RegionState *rg = &G.region;
    for (int i = 0; i < rg->decorCount; i++) {
        Decor *d = &rg->decor[i];
        if (d->type != 1) continue;
        float f = 0.5f + 0.5f * sinf(sTime * 2.3f + d->phase);
        f = f * f * f;
        Lights_Add(Vector3Add(d->pos, Vector3Scale(d->normal, 1.5f)), (Vector3){ 0.3f * f, 0.45f * f, 1.5f * f }, 14.0f);
    }
    if (G.boss.active) {
        const BossDef *bd = &BOSSES[G.boss.type];
        Lights_Add(G.boss.pos, Vector3Scale(bd->glow, 0.3f + 0.5f * G.boss.flash), G.boss.radius * 3.5f);
    }
    if (rg->portalOpen) {
        float f = 0.8f + 0.2f * sinf(sTime * 4.0f);
        Lights_Add(rg->portalPos, (Vector3){ 1.5f * f, 4.0f * f, 2.5f * f }, 40.0f);
    }
    Lights_Commit(sCamPos);
}

/* ------------------------------------------------------------------------- */
/* Groups                                                                     */
/* ------------------------------------------------------------------------- */

static void DrawDecor(void)
{
    RegionState *rg = &G.region;
    const RegionDef *def = Region_Def();
    Vector3 base = MU_ColorToV3(def->look.base);
    int n = 0;
    /* cartilage / elastic rings */
    for (int i = 0; i < rg->decorCount && n < SCRATCH; i++) {
        Decor *d = &rg->decor[i];
        if (d->type != 0 || !Visible(d->pos, d->scale, CAVE_DRAW_DIST)) continue;
        Matrix m = MU_BasisAxis(d->normal, d->pos, d->scale);
        m = ScaleY(m, 2.4f);
        Vector3 tint = Vector3Scale(Vector3Add(base, (Vector3){ 0.25f, 0.2f, 0.2f }), 0.95f);
        sScratch[n++] = Inst_Make(m, tint, d->phase);
    }
    MaterialParams mr = Mat(MU_ColorToV3(ColorBrightness(def->look.base, 0.3f)), 0.4f, 0.5f, 0.6f);
    mr.wobble = 0.0f;
    Flush(mTorus, SHD_INST, &mr, n);

    /* synaptic boutons (brain) */
    n = 0;
    for (int i = 0; i < rg->decorCount && n < SCRATCH; i++) {
        Decor *d = &rg->decor[i];
        if (d->type != 1 || !Visible(d->pos, 3.0f, CAVE_DRAW_DIST)) continue;
        float f = 0.5f + 0.5f * sinf(sTime * 2.3f + d->phase);
        f = f * f * f;
        for (int k = 0; k < 5; k++) {
            float a = (float)k * 1.3f + d->phase;
            Vector3 o = Vector3Add(Vector3Scale(d->normal, 0.6f + 0.3f * (float)k), Vector3Scale(MU_AnyPerp(d->normal), sinf(a) * 0.9f));
            float s = 0.7f - 0.08f * (float)k;
            Matrix m = MU_BasisAxis(d->normal, Vector3Add(d->pos, o), s);
            sScratch[n++] = Inst_Make(m, (Vector3){ 0.8f + 0.6f * f, 0.9f + 0.8f * f, 1.0f + 2.5f * f }, d->phase);
        }
    }
    MaterialParams ms = Mat((Vector3){ 0.6f, 0.7f, 1.5f }, 0.8f, 0.8f, 0.8f);
    ms.wobble = 0.05f;
    Flush(mSphere, SHD_INST, &ms, n);

    /* cilia tufts beating in metachronal waves */
    n = 0;
    for (int i = 0; i < rg->decorCount; i++) {
        Decor *d = &rg->decor[i];
        if (d->type != 2 || !Visible(d->pos, 4.0f, 150.0f)) continue;
        Vector3 t1 = MU_AnyPerp(d->normal);
        Vector3 t2 = Vector3CrossProduct(d->normal, t1);
        for (int k = 0; k < 9 && n < SCRATCH; k++) {
            float ox = (float)(k % 3) - 1.0f, oz = (float)(k / 3) - 1.0f;
            Vector3 p = Vector3Add(d->pos, Vector3Add(Vector3Scale(t1, ox * 0.7f), Vector3Scale(t2, oz * 0.7f)));
            float beat = sinf(sTime * 7.0f + d->phase + (p.x + p.z) * 0.35f);
            Vector3 axis = Vector3Normalize(Vector3Add(d->normal, Vector3Scale(t1, beat * 0.75f)));
            Matrix m = MU_BasisAxis(axis, p, 1.0f);
            m.m0 *= 0.12f; m.m1 *= 0.12f; m.m2 *= 0.12f;
            m.m8 *= 0.12f; m.m9 *= 0.12f; m.m10 *= 0.12f;
            m = ScaleY(m, 2.6f * d->scale);
            sScratch[n++] = Inst_Make(m, (Vector3){ 1.0f, 0.95f, 0.98f }, d->phase);
        }
    }
    MaterialParams mc = Mat((Vector3){ 1.0f, 0.9f, 0.95f }, 0.5f, 0.6f, 0.5f);
    Flush(mCilium, SHD_INST, &mc, n);
}

static int BuildKind(AgentKind kind, float maxDist, float scaleMul, Vector3 (*tintFn)(const Agent *), bool axisAnchored, float flatY)
{
    int n = 0;
    for (int i = 0; i < G.agentHigh && n < SCRATCH; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive || a->kind != kind) continue;
        Vector3 p = AgentPos(a);
        float r = a->radius * scaleMul;
        if (!Visible(p, r * 1.5f, maxDist)) continue;
        Matrix m;
        float breath = 1.0f + 0.035f * sinf(sTime * 2.2f + a->phase * 6.28f);
        if (axisAnchored) {
            m = MU_BasisAxis(a->anchor, p, r * breath);
            if (flatY != 1.0f) m = ScaleY(m, flatY);
        } else {
            Vector3 up = (Vector3){ 0, 1, 0 };
            m = MU_Basis(a->dir, up, p, (Vector3){ r * breath, r / breath, r * breath });
        }
        Vector3 tint = tintFn ? tintFn(a) : (Vector3){ 1, 1, 1 };
        if (a->flash > 0.0f) tint = Vector3Add(tint, (Vector3){ a->flash * 1.6f, a->flash * 1.6f, a->flash * 1.6f });
        sScratch[n++] = Inst_Make(m, tint, a->phase);
    }
    return n;
}

static Vector3 TintDrifter(const Agent *a)
{
    const RegionDef *d = Region_Def();
    float v = 0.85f + 0.05f * (float)a->variant;
    if (G.region.index == REGION_BLOOD) return (Vector3){ v, v, v };
    return Vector3Scale(d->drifterColor, v);
}

static Vector3 TintPlatelet(const Agent *a) { (void)a; return (Vector3){ 1.0f, 0.95f, 0.85f }; }

static Vector3 TintHost(const Agent *a)
{
    Vector3 base = Region_Def()->hostColor;
    if (a->flags & AF_INFECTED) {
        float pulse = 0.5f + 0.5f * sinf(sTime * 3.0f + a->phase * 6.28f);
        Vector3 t = { 0.5f, 1.08f + 0.22f * pulse, 0.78f };
        if (a->progress > 0.0f) {
            /* being cleansed by an NK cell: flicker red */
            float f = a->progress * (0.5f + 0.5f * sinf(sTime * 20.0f));
            t = Vector3Lerp(t, (Vector3){ 1.8f, 0.4f, 0.4f }, f);
        }
        return t;
    }
    if (a->progress > 0.0f) {
        float f = a->progress;
        return Vector3Lerp(base, (Vector3){ 0.55f, 1.2f, 0.8f }, f * (0.7f + 0.3f * sinf(sTime * 14.0f)));
    }
    return base;
}

static Vector3 TintBacteria(const Agent *a)
{
    if (a->flags & AF_ALLY) return (Vector3){ 0.6f, 1.25f, 0.9f };
    return (Vector3){ 0.75f + 0.05f * (float)a->variant, 0.95f, 0.6f };
}

static Vector3 TintMinion(const Agent *a)
{
    float k = (a->flags & AF_TIER2) ? 1.6f : 1.25f;
    return (Vector3){ 0.6f, k, 0.95f };
}

static Vector3 TintEnemy(const Agent *a)
{
    const EnemyDef *d = Enemies_Def(a->kind);
    Vector3 c = d ? d->color : (Vector3){ 1, 1, 1 };
    if (a->flags & AF_ELITE) c = Vector3Add(c, (Vector3){ 0.35f, 0.15f, 0.0f });
    if (a->kind == AK_NK && a->state == AI_WINDUP) c = Vector3Add(c, (Vector3){ 1.2f, 0.0f, 0.0f });
    if (a->kind == AK_ANTIBODY) c = (Vector3){ 1.5f, 1.4f, 0.6f };
    return c;
}

static Vector3 TintNucleus(const Agent *a) { (void)a; return (Vector3){ 1.0f, 1.0f, 1.0f }; }
static Vector3 TintGranuleNK(const Agent *a) { return (a->state == AI_WINDUP || a->state == AI_ATTACK) ? (Vector3){ 2.8f, 0.5f, 0.5f } : (Vector3){ 1.6f, 0.5f, 0.5f }; }
static Vector3 TintPart(const Agent *a) { (void)a; return (Vector3){ 0.9f, 1.6f, 2.2f }; }

static void DrawAgentsOpaque(void)
{
    int n;
    int ri = G.region.index;
    /* drifters */
    n = BuildKind(AK_DRIFTER, 170.0f, 1.0f, TintDrifter, false, 1.0f);
    MaterialParams md = Mat((Vector3){ 1.0f, 0.5f, 0.5f }, 0.3f, 0.9f, 0.45f);
    if (ri == REGION_BRAIN) { md.rimColor = (Vector3){ 0.6f, 0.7f, 1.6f }; md.rimStrength = 0.8f; }
    if (ri == REGION_LUNGS) { md.rimColor = (Vector3){ 1.0f, 0.95f, 1.0f }; md.spec = 0.9f; }
    md.wobble = 0.02f;
    md.wobbleFreq = 3.0f;
    Flush(mDrifter[ri], SHD_INST, &md, n);
    n = BuildKind(AK_PLATELET, 140.0f, 1.0f, TintPlatelet, false, 1.0f);
    Flush(mPlatelet, SHD_INST, &md, n);

    /* host cells */
    float flat = (ri == REGION_BLOOD) ? 1.0f : 1.0f;
    n = BuildKind(AK_HOST, 220.0f, 1.0f, TintHost, true, flat);
    MaterialParams mh = Mat(Region_Def()->look.rim, 0.45f, 0.8f, 0.6f);
    mh.wobble = 0.03f;
    mh.wobbleFreq = 1.6f;
    Flush(mHost[ri], SHD_INST, &mh, n);

    /* bacteria */
    n = 0;
    for (int i = 0; i < G.agentHigh && n < SCRATCH; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive || a->kind != AK_BACTERIA) continue;
        Vector3 p = AgentPos(a);
        if (!Visible(p, 2.0f, 160.0f)) continue;
        Vector3 f = Vector3LengthSqr(a->vel) > 0.04f ? a->vel : a->dir;
        Matrix m = MU_Basis(f, MU_RotateAround((Vector3){ 0, 1, 0 }, Vector3Normalize(f), a->spin), p, (Vector3){ a->radius, a->radius, a->radius });
        Vector3 t = TintBacteria(a);
        if (a->flash > 0.0f) t = Vector3Add(t, Vector3Scale((Vector3){ 1, 1, 1 }, a->flash * 1.5f));
        sScratch[n++] = Inst_Make(m, t, a->phase);
    }
    MaterialParams mb = Mat((Vector3){ 0.6f, 1.0f, 0.5f }, 0.4f, 0.6f, 0.7f);
    Flush(mBacteria, SHD_INST, &mb, n);

    /* virions (minions) and decoys */
    bool ghost = G.stats.synGhostSwarm && G.player.cloaked;
    n = 0;
    for (int i = 0; i < G.agentHigh && n < SCRATCH; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive || a->kind != AK_MINION || ghost) continue;
        Vector3 p = AgentPos(a);
        if (!Visible(p, 1.0f, 160.0f)) continue;
        Vector3 ax = MU_RotateAround(a->dir, (Vector3){ 0, 1, 0 }, a->spin);
        Matrix m = MU_Basis(ax, (Vector3){ 0, 1, 0 }, p, (Vector3){ a->radius, a->radius, a->radius });
        Vector3 t = TintMinion(a);
        if (a->flash > 0.0f) t = Vector3Add(t, Vector3Scale((Vector3){ 1, 1, 1 }, a->flash));
        sScratch[n++] = Inst_Make(m, t, a->phase);
    }
    MaterialParams mv = Mat((Vector3){ 0.4f, 1.6f, 1.0f }, 0.8f, 0.4f, 0.8f);
    mv.emissive = (Vector3){ 0.05f, 0.25f, 0.15f };
    Flush(mVirion, SHD_INST, &mv, n);

    /* immune cells: opaque bodies */
    n = BuildKind(AK_MACROPHAGE, AGENT_DRAW_DIST, 1.0f, TintEnemy, false, 1.0f);
    {
        /* hide macrophages that are engulfing (they're drawn translucent) */
        int w = 0;
        for (int k = 0; k < n; k++) sScratch[w++] = sScratch[k];
        n = w;
    }
    MaterialParams mm = Mat((Vector3){ 1.0f, 0.85f, 0.6f }, 0.45f, 0.7f, 0.5f);
    mm.wobble = 0.06f;
    mm.wobbleFreq = 1.4f;
    if (G.player.engulfedBy.idx < 0) Flush(mMacrophage, SHD_INST, &mm, n);
    else {
        int w = 0;
        Agent *eng = Agent_Get(G.player.engulfedBy);
        for (int i = 0; i < G.agentHigh && w < SCRATCH; i++) {
            Agent *a = &G.agents[i];
            if (!a->alive || a->kind != AK_MACROPHAGE || a == eng) continue;
            Vector3 p = AgentPos(a);
            if (!Visible(p, a->radius * 1.5f, AGENT_DRAW_DIST)) continue;
            Matrix m = MU_Basis(a->dir, (Vector3){ 0, 1, 0 }, p, (Vector3){ a->radius, a->radius, a->radius });
            sScratch[w++] = Inst_Make(m, TintEnemy(a), a->phase);
        }
        Flush(mMacrophage, SHD_INST, &mm, w);
    }

    n = BuildKind(AK_TCELL, AGENT_DRAW_DIST, 1.0f, TintEnemy, false, 1.0f);
    MaterialParams mt = Mat((Vector3){ 0.5f, 0.8f, 1.2f }, 0.5f, 0.6f, 0.6f);
    mt.wobble = 0.03f;
    Flush(mTCell, SHD_INST, &mt, n);
    n = BuildKind(AK_BCELL, AGENT_DRAW_DIST, 1.0f, TintEnemy, false, 1.0f);
    MaterialParams mbc = Mat((Vector3){ 1.2f, 0.9f, 0.4f }, 0.5f, 0.6f, 0.6f);
    Flush(mBCell, SHD_INST, &mbc, n);
    n = BuildKind(AK_ANTIBODY, 120.0f, 1.6f, TintEnemy, false, 1.0f);
    MaterialParams mab = Mat((Vector3){ 1.4f, 1.3f, 0.5f }, 0.8f, 0.3f, 0.8f);
    Flush(mAntibody, SHD_INST, &mab, n);

    /* nuclei and granules seen through translucent neutrophil / NK membranes */
    n = BuildKind(AK_NEUTROPHIL, AGENT_DRAW_DIST, 0.62f, TintNucleus, false, 1.0f);
    MaterialParams mn = Mat((Vector3){ 0.7f, 0.5f, 1.0f }, 0.3f, 0.5f, 0.4f);
    Flush(mNeutroNucleus, SHD_INST, &mn, n);
    n = BuildKind(AK_NEUTROPHIL, 120.0f, 0.8f, TintNucleus, false, 1.0f);
    Flush(mGranules, SHD_INST, &mn, n);
    n = BuildKind(AK_NK, AGENT_DRAW_DIST, 0.6f, TintNucleus, false, 1.0f);
    Flush(mNKNucleus, SHD_INST, &mn, n);
    n = BuildKind(AK_NK, AGENT_DRAW_DIST, 0.85f, TintGranuleNK, false, 1.0f);
    MaterialParams mg = Mat((Vector3){ 1.6f, 0.4f, 0.4f }, 0.5f, 0.3f, 0.6f);
    mg.emissive = (Vector3){ 0.3f, 0.05f, 0.05f };
    Flush(mNKGranules, SHD_INST, &mg, n);

    /* boss shield nodes */
    n = 0;
    for (int i = 0; i < G.agentHigh && n < SCRATCH; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive || a->kind != AK_BOSS_PART) continue;
        Matrix m = MU_Basis(MU_RotateAround((Vector3){ 1, 0, 0 }, (Vector3){ 0, 1, 0 }, a->spin), (Vector3){ 0, 1, 0 }, AgentPos(a), (Vector3){ a->radius, a->radius, a->radius });
        Vector3 t = TintPart(a);
        if (a->flash > 0.0f) t = Vector3Add(t, Vector3Scale((Vector3){ 1, 1, 1 }, a->flash * 2.0f));
        sScratch[n++] = Inst_Make(m, t, a->phase);
    }
    MaterialParams mp = Mat((Vector3){ 0.6f, 1.6f, 2.4f }, 1.0f, 0.3f, 1.0f);
    mp.emissive = (Vector3){ 0.1f, 0.3f, 0.5f };
    Flush(mCrystal, SHD_INST, &mp, n);
}

static void DrawAgentsTranslucent(void)
{
    int n;
    /* neutrophil membranes */
    n = BuildKind(AK_NEUTROPHIL, AGENT_DRAW_DIST, 1.0f, TintEnemy, false, 1.0f);
    MaterialParams m = Mat((Vector3){ 0.9f, 0.85f, 1.2f }, 0.9f, 0.9f, 0.9f);
    m.alpha = 0.22f;
    m.rimAlpha = 0.65f;
    m.rimPower = 2.2f;
    m.wobble = 0.07f;
    m.wobbleFreq = 2.2f;
    Flush(mNeutroShell, SHD_INST, &m, n);
    n = BuildKind(AK_NK, AGENT_DRAW_DIST, 1.0f, TintEnemy, false, 1.0f);
    MaterialParams mk = m;
    mk.rimColor = (Vector3){ 1.3f, 0.6f, 0.6f };
    Flush(mNKShell, SHD_INST, &mk, n);

    /* a macrophage that has engulfed you becomes see-through */
    Agent *eng = Agent_Get(G.player.engulfedBy);
    if (eng) {
        Matrix mm = MU_Basis(eng->dir, (Vector3){ 0, 1, 0 }, AgentPos(eng), (Vector3){ eng->radius, eng->radius, eng->radius });
        sScratch[0] = Inst_Make(mm, (Vector3){ 1.3f, 0.9f, 0.6f }, eng->phase);
        MaterialParams me = Mat((Vector3){ 1.5f, 0.8f, 0.4f }, 1.0f, 0.8f, 0.5f);
        me.alpha = 0.3f;
        me.rimAlpha = 0.6f;
        me.wobble = 0.12f;
        Flush(mMacrophage, SHD_INST, &me, 1);
    }

    /* decoys: ghostly copies of the pathogen */
    n = 0;
    for (int i = 0; i < G.agentHigh && n < SCRATCH; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive || a->kind != AK_DECOY) continue;
        Matrix mm = MU_Basis(MU_RotateAround(a->dir, (Vector3){ 0, 1, 0 }, a->spin), (Vector3){ 0, 1, 0 }, AgentPos(a), (Vector3){ a->radius, a->radius, a->radius });
        sScratch[n++] = Inst_Make(mm, (Vector3){ 0.6f, 1.4f, 1.0f }, a->phase);
    }
    MaterialParams mdc = Mat((Vector3){ 0.4f, 1.6f, 1.2f }, 1.2f, 0.4f, 0.5f);
    mdc.alpha = 0.3f;
    mdc.rimAlpha = 0.5f;
    Flush(mVirion, SHD_INST, &mdc, n);
}

static void DrawPickupsAndProjectiles(void)
{
    int n = 0;
    for (int i = 0; i < MAX_PICKUPS && n < SCRATCH; i++) {
        Pickup *k = &G.pickups[i];
        if (!k->alive || k->type == PK_ATP || k->type == PK_CACHE) continue;
        Vector3 p = Lerp3(k->prev, k->pos, G.alpha);
        if (!Visible(p, 2.0f, 150.0f)) continue;
        float bob = sinf(k->phase * 2.0f) * 0.15f;
        p.y += bob;
        float s = 0.32f;
        Vector3 t = { 0.5f, 1.8f, 2.2f };
        if (k->type == PK_BIOMASS) { t = (Vector3){ 2.0f, 0.5f, 0.5f }; s = 0.42f; }
        if (k->type == PK_POD) { t = (Vector3){ 2.0f, 0.7f, 2.4f }; s = 1.1f + 0.1f * sinf(k->phase * 3.0f); }
        if (k->life > 0.0f && k->life < 4.0f && fmodf(k->life, 0.3f) < 0.12f) s *= 0.6f;
        sScratch[n++] = Inst_Make(MU_BasisAxis((Vector3){ 0, 1, 0 }, p, s), t, k->phase);
    }
    MaterialParams mo = Mat((Vector3){ 1, 1, 1 }, 0.6f, 0.2f, 1.0f);
    mo.emissive = (Vector3){ 0.2f, 0.2f, 0.2f };
    Flush(mSphere, SHD_INST, &mo, n);

    n = 0;
    for (int i = 0; i < MAX_PICKUPS && n < SCRATCH; i++) {
        Pickup *k = &G.pickups[i];
        if (!k->alive || (k->type != PK_ATP && k->type != PK_CACHE)) continue;
        Vector3 p = Lerp3(k->prev, k->pos, G.alpha);
        if (!Visible(p, 3.0f, 150.0f)) continue;
        bool cache = k->type == PK_CACHE;
        float s = cache ? 1.4f : 0.38f;
        Vector3 ax = MU_RotateAround((Vector3){ 1, 0, 0 }, (Vector3){ 0, 1, 0 }, k->phase * 1.5f);
        Matrix m = MU_Basis(ax, (Vector3){ 0, 1, 0 }, p, (Vector3){ s, s, s });
        sScratch[n++] = Inst_Make(m, cache ? (Vector3){ 0.6f, 2.0f, 2.6f } : (Vector3){ 2.2f, 1.8f, 0.4f }, k->phase);
        if (cache) {
            for (int c = 0; c < 4; c++) {
                float a = k->phase * 0.7f + (float)c * 1.57f;
                Vector3 q = Vector3Add(p, (Vector3){ cosf(a) * 2.2f, sinf(a * 2.0f) * 0.6f, sinf(a) * 2.2f });
                sScratch[n++] = Inst_Make(MU_Basis(ax, (Vector3){ 0, 1, 0 }, q, (Vector3){ 0.5f, 0.5f, 0.5f }), (Vector3){ 0.6f, 2.2f, 2.8f }, k->phase);
            }
        }
    }
    Flush(mCrystal, SHD_INST, &mo, n);

    /* projectiles: glowing droplets stretched along their velocity */
    n = 0;
    for (int i = 0; i < MAX_PROJECTILES && n < SCRATCH; i++) {
        Projectile *pr = &G.proj[i];
        if (!pr->alive) continue;
        Vector3 p = Lerp3(pr->prev, pr->pos, G.alpha);
        if (!Visible(p, pr->radius, 200.0f)) continue;
        float sp = Vector3Length(pr->vel);
        float st = 1.0f + Clamp(sp * 0.03f, 0.0f, 1.2f);
        Matrix m = MU_Basis(sp > 0.1f ? pr->vel : (Vector3){ 0, 0, 1 }, (Vector3){ 0, 1, 0 }, p, (Vector3){ pr->radius, pr->radius, pr->radius * st });
        Vector3 t;
        switch (pr->type) {
            case PT_TOXIN: t = (Vector3){ 1.3f, 3.2f, 1.5f }; break;
            case PT_ACID: t = (Vector3){ 2.6f, 3.2f, 1.2f }; break;
            case PT_PERFORIN: t = (Vector3){ 3.2f, 1.2f, 1.3f }; break;
            case PT_BOSS_ORB: t = (Vector3){ 3.0f, 1.6f, 2.8f }; break;
            case PT_MUCUS: t = (Vector3){ 1.0f, 0.95f, 0.6f }; break;
            case PT_ACIDGLOB: t = (Vector3){ 1.6f, 2.8f, 1.1f }; break;
            default: t = (Vector3){ 1.4f, 1.8f, 3.4f }; break;
        }
        sScratch[n++] = Inst_Make(m, t, (float)i * 0.37f);
    }
    MaterialParams mpj = Mat((Vector3){ 1, 1, 1 }, 0.8f, 0.0f, 0.8f);
    mpj.emissive = (Vector3){ 0.3f, 0.3f, 0.3f };
    mpj.wobble = 0.08f;
    mpj.wobbleFreq = 4.0f;
    Flush(mSphere, SHD_INST, &mpj, n);
}

static void DrawHazardsOpaque(void)
{
    int n = 0;
    for (int i = 0; i < MAX_HAZARDS && n < SCRATCH; i++) {
        Hazard *h = &G.hazards[i];
        if (!h->alive || h->type != HZ_CLOT) continue;
        float grow = MU_EaseOutBack(Sat(h->timer / 1.2f));
        Matrix m = MU_Basis(MU_RotateAround((Vector3){ 1, 0, 0 }, (Vector3){ 0, 1, 0 }, h->phase), (Vector3){ 0, 1, 0 }, h->pos, Vector3Scale((Vector3){ 1, 1, 1 }, h->radius * grow));
        float dmg = 1.0f - Sat(h->hp / (70.0f + 25.0f * (float)G.region.index));
        sScratch[n++] = Inst_Make(m, (Vector3){ 1.0f + dmg * 0.6f, 0.9f, 0.9f }, h->phase);
    }
    MaterialParams mc = Mat((Vector3){ 1.0f, 0.3f, 0.3f }, 0.4f, 0.4f, 0.8f);
    mc.wobble = 0.05f;
    Flush(mClot, SHD_INST, &mc, n);
}

static void DrawHazardsTranslucent(void)
{
    int n = 0;
    MaterialParams m = Mat((Vector3){ 1, 1, 1 }, 1.0f, 0.3f, 0.9f);
    /* spherical volumes */
    for (int i = 0; i < MAX_HAZARDS && n < SCRATCH; i++) {
        Hazard *h = &G.hazards[i];
        if (!h->alive) continue;
        Vector3 t;
        Vector3 s = { h->radius, h->radius, h->radius };
        float ph = h->phase;
        switch (h->type) {
            case HZ_ACID_POOL: t = (Vector3){ 0.9f, 1.6f, 0.4f }; s.y *= 0.35f; break;
            case HZ_MUCUS: t = (Vector3){ 1.0f, 0.95f, 0.65f }; break;
            case HZ_TOXIN_CLOUD: t = (Vector3){ 0.5f, 1.3f, 0.4f }; break;
            case HZ_ANTIBIOTIC:
            case HZ_STORM: {
                bool storm = h->type == HZ_STORM;
                float warn = storm ? 1.8f : 2.6f;
                float k = Sat(h->timer / warn);
                float pulse = 0.5f + 0.5f * sinf(sTime * (10.0f + 20.0f * k));
                t = storm ? (Vector3){ 0.6f, 0.9f, 1.6f + pulse * 1.4f } : (Vector3){ 1.6f + pulse * 1.4f, 0.5f, 0.3f };
                if (h->triggered) t = Vector3Scale(t, 2.0f);
            } break;
            case HZ_SHOCKWAVE: t = (Vector3){ 2.0f, 1.6f, 1.2f }; break;
            default: continue;
        }
        Matrix mm = MU_Basis((Vector3){ 0, 0, 1 }, (Vector3){ 0, 1, 0 }, h->pos, s);
        sScratch[n++] = Inst_Make(mm, t, ph);
    }
    m.alpha = 0.16f;
    m.rimAlpha = 0.5f;
    m.rimPower = 2.0f;
    m.wobble = 0.05f;
    m.wobbleFreq = 0.6f;
    m.emissive = (Vector3){ 0.08f, 0.08f, 0.08f };
    Flush(mSphereHi, SHD_INST, &m, n);

    /* inner "time to strike" cores for telegraphed strikes */
    n = 0;
    for (int i = 0; i < MAX_HAZARDS && n < SCRATCH; i++) {
        Hazard *h = &G.hazards[i];
        if (!h->alive || (h->type != HZ_ANTIBIOTIC && h->type != HZ_STORM) || h->triggered) continue;
        float warn = (h->type == HZ_STORM) ? 1.8f : 2.6f;
        float k = Sat(h->timer / warn);
        Vector3 t = (h->type == HZ_STORM) ? (Vector3){ 0.8f, 1.2f, 3.0f } : (Vector3){ 3.0f, 0.8f, 0.4f };
        sScratch[n++] = Inst_Make(MU_BasisAxis((Vector3){ 0, 1, 0 }, h->pos, h->radius * k), t, 0.0f);
    }
    MaterialParams mi = m;
    mi.alpha = 0.25f;
    Flush(mSphereHi, SHD_INST, &mi, n);

    /* rings: complement cascade and vortex */
    n = 0;
    for (int i = 0; i < MAX_HAZARDS && n < SCRATCH - 4; i++) {
        Hazard *h = &G.hazards[i];
        if (!h->alive) continue;
        if (h->type == HZ_COMPLEMENT) {
            Matrix mm = MU_BasisAxis(h->dir, h->pos, h->radius);
            mm = ScaleY(mm, 18.0f / fmaxf(h->radius, 1.0f));
            sScratch[n++] = Inst_Make(mm, (Vector3){ 2.6f, 1.6f, 2.8f }, h->phase);
        } else if (h->type == HZ_VORTEX) {
            for (int k = 1; k <= 3; k++) {
                float rr = h->radius * (float)k / 3.2f;
                Vector3 ax = MU_RotateAround((Vector3){ 0, 1, 0 }, (Vector3){ 1, 0, 0 }, 0.3f * sinf(sTime + (float)k));
                Matrix mm = MU_BasisAxis(ax, h->pos, rr);
                sScratch[n++] = Inst_Make(mm, (Vector3){ 1.8f, 0.8f, 2.2f }, (float)k);
            }
        }
    }
    MaterialParams mr = Mat((Vector3){ 1.5f, 1.0f, 1.8f }, 1.0f, 0.2f, 0.5f);
    mr.alpha = 0.55f;
    mr.emissive = (Vector3){ 0.3f, 0.2f, 0.3f };
    Flush(mTorus, SHD_INST, &mr, n);
}

static void DrawPortal(void)
{
    RegionState *rg = &G.region;
    if (!rg->portalOpen) return;
    int n = 0;
    for (int k = 0; k < 5; k++) {
        float a = sTime * (0.6f + 0.25f * (float)k) + (float)k;
        Vector3 ax = Vector3Normalize((Vector3){ sinf(a), cosf(a * 0.7f), cosf(a) });
        Matrix m = MU_BasisAxis(ax, rg->portalPos, 3.0f + 1.2f * (float)k);
        sScratch[n++] = Inst_Make(m, (Vector3){ 1.4f, 3.0f, 2.0f }, (float)k);
    }
    MaterialParams mr = Mat((Vector3){ 1.0f, 2.5f, 1.6f }, 1.0f, 0.0f, 0.4f);
    mr.emissive = (Vector3){ 0.3f, 0.6f, 0.4f };
    Flush(mTorus, SHD_INST, &mr, n);
    float pulse = 1.0f + 0.1f * sinf(sTime * 5.0f);
    sScratch[0] = Inst_Make(MU_BasisAxis((Vector3){ 0, 1, 0 }, rg->portalPos, 2.4f * pulse), (Vector3){ 2.5f, 4.0f, 3.0f }, 0.0f);
    MaterialParams mc = Mat((Vector3){ 2, 4, 3 }, 2.0f, 0.0f, 0.0f);
    mc.alpha = 0.8f;
    mc.wobble = 0.15f;
    mc.wobbleFreq = 3.0f;
    mc.emissive = (Vector3){ 1.0f, 1.6f, 1.2f };
    Flush(mSphereHi, SHD_INST, &mc, 1);
}

/* ------------------------------------------------------------------------- */
/* Boss                                                                       */
/* ------------------------------------------------------------------------- */

static void DrawBoss(bool translucentPass)
{
    Boss *b = &G.boss;
    if (!b->active) return;
    const BossDef *d = &BOSSES[b->type];
    Vector3 pos = Vector3Lerp(b->prev, b->pos, G.alpha);
    float breath = 1.0f + 0.03f * sinf(b->anim * 1.7f) + 0.02f * b->flash;
    if (b->pattern == BP_CHARGE && b->patternStep == 0) breath += 0.06f * sinf(b->anim * 30.0f);
    if (!translucentPass) {
        float spin = (b->type == BOSS_CITADEL) ? b->anim * 0.4f : 0.0f;
        Vector3 fwd = MU_RotateAround(b->facing, (Vector3){ 0, 1, 0 }, spin);
        Matrix xf = MU_Basis(fwd, (Vector3){ 0, 1, 0 }, pos, (Vector3){ b->radius * breath, b->radius / breath, b->radius * breath });
        MaterialParams m = Mat(Vector3Scale(d->glow, 0.5f), 0.6f, 0.8f, 0.7f);
        m.emissive = Vector3Scale(d->glow, 0.04f + 0.35f * b->flash);
        m.wobble = 0.05f;
        m.wobbleFreq = 1.2f;
        if (b->dying) m.emissive = Vector3Scale(d->glow, 0.4f + 0.6f * b->flash);
        Color tint = ColorF(d->color.x, d->color.y, d->color.z, 1.0f);
        if (b->pattern == BP_TELEPORT && b->patternStep == 0) { m.alpha = 0.4f; }
        DrawLit(mBoss[b->type], xf, &m, tint);

        /* worm segments */
        int n = 0;
        for (int i = 0; i < b->segCount && n < SCRATCH; i++) {
            Vector3 prev = (i == 0) ? b->pos : b->seg[i - 1];
            Vector3 dir = Vector3Subtract(prev, b->seg[i]);
            float r = b->radius * (0.92f - 0.035f * (float)i);
            float wave = 1.0f + 0.08f * sinf(b->anim * 4.0f - (float)i * 0.7f);
            Matrix mm = MU_Basis(dir, (Vector3){ 0, 1, 0 }, b->seg[i], (Vector3){ r * wave, r * wave, r * 1.1f });
            Vector3 t = Vector3Add(d->color, Vector3Scale((Vector3){ 1, 1, 1 }, b->flash * 0.8f));
            sScratch[n++] = Inst_Make(mm, t, (float)i * 0.3f);
        }
        MaterialParams ms = Mat((Vector3){ 1.4f, 1.0f, 0.4f }, 0.5f, 0.7f, 0.9f);
        ms.wobble = 0.04f;
        Flush(mSegment, SHD_INST, &ms, n);
    } else {
        int n = 0;
        if (b->shielded) {
            float s = b->radius * 1.45f * (1.0f + 0.02f * sinf(b->anim * 6.0f));
            sScratch[n++] = Inst_Make(MU_BasisAxis((Vector3){ 0, 1, 0 }, pos, s), (Vector3){ 0.8f, 1.8f, 2.8f }, 0.0f);
            MaterialParams m = Mat((Vector3){ 0.6f, 1.6f, 2.8f }, 1.6f, 0.0f, 1.0f);
            m.alpha = 0.08f;
            m.rimAlpha = 0.7f;
            m.rimPower = 3.0f;
            m.wobble = 0.03f;
            Flush(mSphereHi, SHD_INST, &m, n);
        }
        /* charge telegraph: a line of glowing beads along the charge path */
        if (b->pattern == BP_CHARGE && b->patternStep == 0) {
            n = 0;
            float k = Sat(b->patternT / 1.1f);
            for (int i = 1; i < 24; i++) {
                Vector3 q = Vector3Add(pos, Vector3Scale(b->chargeDir, b->radius + (float)i * 2.2f));
                float s = 0.35f + 0.25f * sinf(sTime * 14.0f - (float)i * 0.6f);
                sScratch[n++] = Inst_Make(MU_BasisAxis((Vector3){ 0, 1, 0 }, q, s * (0.4f + k)), (Vector3){ 2.6f + 2.0f * k, 0.6f, 0.4f }, (float)i);
            }
            MaterialParams m = Mat((Vector3){ 3, 1, 1 }, 1.0f, 0.0f, 0.0f);
            m.alpha = 0.7f;
            m.emissive = (Vector3){ 0.5f, 0.1f, 0.1f };
            Flush(mSphere, SHD_INST, &m, n);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Player                                                                     */
/* ------------------------------------------------------------------------- */

static void DrawPlayer(bool translucentPass)
{
    Player *p = &G.player;
    PlayerStats *s = &G.stats;
    if (!G.run.active || !mPlayerValid) return;
    if (p->dead && p->deathT > 0.05f) return;
    Vector3 pos = Vector3Lerp(p->prev, p->pos, G.alpha);
    float r = p->radius;
    float sq = p->squash;
    float breath = 0.03f * sinf(p->animT * 2.5f);
    Vector3 fwd = p->forward;
    Vector3 up = (Vector3){ 0, 1, 0 };
    float cloakA = p->cloaked ? 0.12f + 0.06f * sinf(sTime * 9.0f) : 1.0f;

    float camD = Vector3Distance(sCamPos, pos);
    float nearFade = Sat((camD - r * 1.4f) / (r * 1.6f));
    if (!translucentPass) {
        if (p->cloaked) return;
        /* capsid core, slowly rotating about the swim axis */
        Vector3 rup = MU_RotateAround(up, fwd, p->animT * 0.6f);
        Matrix xf = MU_Basis(fwd, rup, pos, (Vector3){ r * (1.0f - sq * 0.5f + breath), r * (1.0f - sq * 0.5f + breath), r * (1.0f + sq) });
        MaterialParams m = Mat((Vector3){ 0.4f, 1.6f, 1.0f }, 0.9f, 0.6f, 1.0f);
        float hurt = p->hurtFlash;
        m.emissive = (Vector3){ 0.06f + hurt * 0.8f, 0.22f + 0.1f * (float)p->stage, 0.12f };
        if (p->rhythmFlash > 0.1f) m.emissive = Vector3Add(m.emissive, (Vector3){ 0.2f * p->rhythmFlash, 0.8f * p->rhythmFlash, 0.4f * p->rhythmFlash });
        if (p->invuln > 0.0f && p->dashT <= 0.0f && fmodf(sTime, 0.2f) < 0.1f) m.emissive = Vector3Add(m.emissive, (Vector3){ 0.3f, 0.3f, 0.3f });
        m.alpha = 0.15f + 0.85f * nearFade;
        DrawLit(mPlayer, xf, &m, WHITE);
        if (nearFade < 0.5f) return;

        /* flagella: long helical filaments trailing behind, driven by a travelling wave */
        int n = 0;
        int flag = s->flagella;
        float sp = Vector3Length(p->vel);
        float freq = 7.0f + sp * 0.45f;
        Vector3 side = MU_AnyPerp(fwd);
        Vector3 side2 = Vector3CrossProduct(fwd, side);
        for (int f = 0; f < flag && n < SCRATCH - 16; f++) {
            float a = (float)f / (float)flag * 2.0f * PI + 0.4f;
            Vector3 spread = Vector3Add(Vector3Scale(side, cosf(a)), Vector3Scale(side2, sinf(a)));
            Vector3 base = Vector3Add(pos, Vector3Add(Vector3Scale(fwd, -r * 0.78f), Vector3Scale(spread, r * (flag > 1 ? 0.22f : 0.0f))));
            Vector3 dir = Vector3Normalize(Vector3Add(Vector3Negate(fwd), Vector3Scale(spread, 0.12f)));
            Vector3 waveA = Vector3Normalize(Vector3CrossProduct(dir, spread));
            Vector3 waveB = Vector3CrossProduct(dir, waveA);
            Vector3 cur = base;
            int segs = 14;
            float segLen = r * (0.26f + 0.03f * (float)p->stage);
            float thick = r * 0.055f;
            for (int k = 0; k < segs; k++) {
                float t = (float)k / (float)segs;
                float ph = p->animT * freq - (float)k * 0.75f + (float)f * 1.7f;
                float amp = 0.08f + 0.42f * t;
                Vector3 d = Vector3Normalize(Vector3Add(dir, Vector3Add(Vector3Scale(waveA, sinf(ph) * amp), Vector3Scale(waveB, cosf(ph) * amp * 0.6f))));
                Matrix mm = MU_BasisAxis(d, cur, 1.0f);
                float th = thick * (1.0f - 0.65f * t);
                mm.m0 *= th; mm.m1 *= th; mm.m2 *= th;
                mm.m8 *= th; mm.m9 *= th; mm.m10 *= th;
                mm = ScaleY(mm, segLen * 1.06f);
                sScratch[n++] = Inst_Make(mm, (Vector3){ 0.5f, 1.05f, 0.85f }, t);
                cur = Vector3Add(cur, Vector3Scale(d, segLen));
            }
        }
        MaterialParams mf = Mat((Vector3){ 0.4f, 1.4f, 1.0f }, 0.7f, 0.6f, 0.6f);
        mf.emissive = (Vector3){ 0.03f, 0.1f, 0.07f };
        Flush(mFlagSeg, SHD_INST, &mf, n);

        /* injection tendril */
        Agent *h = Agent_Get(p->attach);
        if (h) {
            n = 0;
            Vector3 a0 = Vector3Add(pos, Vector3Scale(fwd, r * 0.8f));
            Vector3 a1 = h->pos;
            for (int k = 0; k < 14; k++) {
                float t = (float)k / 13.0f;
                Vector3 q = Vector3Lerp(a0, a1, t * 0.85f);
                float w = 0.12f + 0.06f * sinf(sTime * 18.0f - t * 12.0f);
                sScratch[n++] = Inst_Make(MU_BasisAxis((Vector3){ 0, 1, 0 }, q, w * r), (Vector3){ 1.0f, 3.0f, 1.6f }, t);
            }
            MaterialParams mt = Mat((Vector3){ 1, 3, 2 }, 1.0f, 0.0f, 0.0f);
            mt.emissive = (Vector3){ 0.2f, 0.6f, 0.3f };
            Flush(mSphere, SHD_INST, &mt, n);
        }
    } else {
        int n = 0;
        if (p->cloaked) {
            Matrix xf = MU_Basis(fwd, up, pos, (Vector3){ r, r, r * (1.0f + sq) });
            sScratch[n++] = Inst_Make(xf, (Vector3){ 0.7f, 0.8f, 1.6f }, 0.0f);
            MaterialParams m = Mat((Vector3){ 0.6f, 0.8f, 2.0f }, 1.4f, 0.0f, 0.4f);
            m.alpha = cloakA * 0.4f;
            m.rimAlpha = 0.35f;
            m.wobble = 0.08f;
            m.wobbleFreq = 5.0f;
            Flush(mVirion, SHD_INST, &m, n);
            return;
        }
        /* lipid envelope / membrane layers and glowing nodes inside */
        int layers = nearFade < 0.5f ? 0 : s->membraneLayers - 1;
        n = 0;
        for (int l = 0; l < layers && n < SCRATCH; l++) {
            float sc = r * (1.35f + 0.18f * (float)l) * (1.0f + breath * 1.5f);
            Matrix xf = MU_Basis(fwd, up, pos, (Vector3){ sc * (1.0f - sq * 0.5f), sc * (1.0f - sq * 0.5f), sc * (1.0f + sq) });
            sScratch[n++] = Inst_Make(xf, (Vector3){ 0.6f, 1.2f, 0.9f }, (float)l * 0.3f);
        }
        MaterialParams mm = Mat((Vector3){ 0.4f, 1.6f, 1.1f }, 1.0f, 0.6f, 1.0f);
        mm.alpha = 0.07f;
        mm.rimAlpha = 0.45f;
        mm.rimPower = 2.6f;
        mm.wobble = 0.06f;
        mm.wobbleFreq = 2.4f;
        Flush(mSphereHi, SHD_INST, &mm, n);
        n = 0;
        for (int g = 0; g < s->glowNodes && n < SCRATCH; g++) {
            float a = p->animT * (0.7f + 0.13f * (float)g) + (float)g * 2.4f;
            Vector3 o = { cosf(a) * r * 1.05f, sinf(a * 1.3f) * r * 0.8f, sinf(a) * r * 1.05f };
            float pul = 0.7f + 0.3f * sinf(sTime * 5.0f + (float)g);
            sScratch[n++] = Inst_Make(MU_BasisAxis((Vector3){ 0, 1, 0 }, Vector3Add(pos, o), r * 0.09f * pul), (Vector3){ 1.2f, 3.0f, 1.8f }, (float)g);
        }
        MaterialParams mg = Mat((Vector3){ 1, 3, 2 }, 1.0f, 0.0f, 0.0f);
        mg.emissive = (Vector3){ 0.3f, 0.8f, 0.5f };
        Flush(mSphere, SHD_INST, &mg, n);
    }
}

/* ------------------------------------------------------------------------- */
/* Frame                                                                      */
/* ------------------------------------------------------------------------- */

static void SetFrameUniforms(Camera3D cam)
{
    const RegionDef *d = Region_Def();
    RegionState *rg = &G.region;
    Vector3 fog = d->fogColor;
    float dens = d->fogDensity;
    Vector3 amb = d->ambient;
    if (rg->fever > 0.01f) {
        fog = Vector3Lerp(fog, (Vector3){ 0.32f, 0.06f, 0.02f }, rg->fever * 0.6f);
        amb = Vector3Lerp(amb, (Vector3){ 0.8f, 0.35f, 0.2f }, rg->fever * 0.4f);
    }
    if (rg->event == EV_STORM) {
        float f = (GetRandomValue(0, 100) < 4) ? 1.0f : 0.0f;
        amb = Vector3Add(amb, (Vector3){ 0.3f * f, 0.4f * f, 1.2f * f });
    }
    if (G.state == GS_MUTATION) dens *= 0.8f;
    Shaders_SetFrame(cam.position, sTime, fog, dens, rg->lightDir, d->sunColor, amb);
}

void Render_World(Camera3D cam, float alpha)
{
    (void)alpha;
    sTime = (float)GetTime();
    sCamPos = cam.position;
    sCamFwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    SetFrameUniforms(cam);
    GatherLights();

    BeginMode3D(cam);
    rlSetClipPlanes(CAM_NEAR, CAM_FAR);
    rlDrawRenderBatchActive();
    float heart = Music_Heartbeat();
    Cave_Draw(cam, heart);
    DrawDecor();
    DrawAgentsOpaque();
    DrawHazardsOpaque();
    DrawPickupsAndProjectiles();
    DrawBoss(false);
    DrawPlayer(false);

    /* translucent pass: no depth writes, alpha blending */
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    DrawAgentsTranslucent();
    DrawHazardsTranslucent();
    DrawBoss(true);
    DrawPlayer(true);
    EndBlendMode();
    BeginBlendMode(BLEND_ADDITIVE);
    DrawPortal();
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();

    Particles_Draw(cam);

    if (G.debug.collision) {
        RegionState *rg = &G.region;
        for (int i = 0; i < rg->pathCount - 1; i++) DrawLine3D(rg->path[i], rg->path[i + 1], YELLOW);
        for (int i = 0; i < G.agentHigh; i++) {
            Agent *a = &G.agents[i];
            if (!a->alive || a->team != TEAM_IMMUNE) continue;
            Color c = a->state == AI_CHASE ? RED : (a->state == AI_INVESTIGATE ? ORANGE : (a->state == AI_PATROL ? GREEN : SKYBLUE));
            DrawLine3D(a->pos, Vector3Add(a->pos, Vector3Scale(a->dir, a->radius * 2.0f)), c);
            if (a->state == AI_INVESTIGATE || a->state == AI_PATROL) DrawLine3D(a->pos, a->goal, Fade(c, 0.4f));
        }
        if (G.run.active) DrawSphereWires(G.player.pos, G.player.radius, 6, 8, LIME);
    }
    EndMode3D();
}

void Render_Menu(Camera3D cam) { Render_World(cam, 1.0f); }

/* ------------------------------------------------------------------------- */
/* Codex specimen viewer                                                      */
/* ------------------------------------------------------------------------- */

void Render_DrawCodexModel(int cx, float yaw, float time)
{
    sTime = time;
    Mesh m = mSphere;
    Mesh inner = { 0 };
    Vector3 tint = { 1, 1, 1 };
    MaterialParams mp = Mat((Vector3){ 1, 1, 1 }, 0.5f, 0.6f, 0.6f);
    float scale = 1.0f, innerScale = 0.6f, flatY = 1.0f;
    bool shell = false;
    switch (cx) {
        case CX_PLAYER: m = mPlayerValid ? mPlayer : mVirion; mp.emissive = (Vector3){ 0.05f, 0.2f, 0.1f }; break;
        case CX_RBC: m = mDrifter[REGION_BLOOD]; break;
        case CX_PLATELET: m = mPlatelet; break;
        case CX_SURFACTANT: m = mDrifter[REGION_LUNGS]; tint = REGIONS[REGION_LUNGS].drifterColor; break;
        case CX_CHYME: m = mDrifter[REGION_GUT]; tint = REGIONS[REGION_GUT].drifterColor; break;
        case CX_LYMPHGLOB: m = mDrifter[REGION_LYMPH]; tint = REGIONS[REGION_LYMPH].drifterColor; break;
        case CX_VESICLE: m = mDrifter[REGION_BRAIN]; tint = REGIONS[REGION_BRAIN].drifterColor; break;
        case CX_ENDOTHELIAL: m = mHost[REGION_BLOOD]; tint = REGIONS[REGION_BLOOD].hostColor; break;
        case CX_PNEUMOCYTE: m = mHost[REGION_LUNGS]; tint = REGIONS[REGION_LUNGS].hostColor; break;
        case CX_ENTEROCYTE: m = mHost[REGION_GUT]; tint = REGIONS[REGION_GUT].hostColor; scale = 0.8f; break;
        case CX_LYMPHOCYTE: m = mHost[REGION_LYMPH]; tint = REGIONS[REGION_LYMPH].hostColor; break;
        case CX_NEURON: m = mHost[REGION_BRAIN]; tint = REGIONS[REGION_BRAIN].hostColor; scale = 0.55f; break;
        case CX_BACTERIA: m = mBacteria; tint = (Vector3){ 0.8f, 1.0f, 0.6f }; scale = 0.9f; break;
        case CX_NEUTROPHIL: m = mNeutroShell; inner = mNeutroNucleus; shell = true; tint = (Vector3){ 0.85f, 0.75f, 1.0f }; break;
        case CX_MACROPHAGE: m = mMacrophage; tint = (Vector3){ 1.0f, 0.85f, 0.6f }; scale = 0.75f; break;
        case CX_NK: m = mNKShell; inner = mNKGranules; innerScale = 0.85f; shell = true; tint = (Vector3){ 1.0f, 0.4f, 0.4f }; break;
        case CX_TCELL: m = mTCell; tint = (Vector3){ 0.5f, 0.8f, 1.0f }; break;
        case CX_BCELL: m = mBCell; tint = (Vector3){ 1.0f, 0.85f, 0.4f }; scale = 0.8f; break;
        case CX_ANTIBODY: m = mAntibody; tint = (Vector3){ 1.4f, 1.3f, 0.6f }; scale = 1.4f; break;
        case CX_BOSS_SENTINEL: case CX_BOSS_WARDEN: case CX_BOSS_LEVIATHAN: case CX_BOSS_CITADEL: case CX_BOSS_OVERMIND: {
            int bt = cx - CX_BOSS_SENTINEL;
            m = mBoss[bt];
            tint = BOSSES[bt].color;
            mp.emissive = Vector3Scale(BOSSES[bt].glow, 0.1f);
            scale = 0.8f;
        } break;
        default: break;
    }
    (void)flatY;
    Vector3 fwd = { sinf(yaw), 0.0f, cosf(yaw) };
    Matrix xf = MU_Basis(fwd, (Vector3){ 0, 1, 0 }, (Vector3){ 0, 0, 0 }, (Vector3){ scale, scale, scale });
    if (inner.vaoId) {
        Matrix xi = MU_Basis(fwd, (Vector3){ 0, 1, 0 }, (Vector3){ 0, 0, 0 }, (Vector3){ scale * innerScale, scale * innerScale, scale * innerScale });
        sScratch[0] = Inst_Make(xi, cx == CX_NK ? (Vector3){ 1.6f, 0.5f, 0.5f } : (Vector3){ 1, 1, 1 }, 0.0f);
        MaterialParams mi = Mat((Vector3){ 1, 1, 1 }, 0.3f, 0.5f, 0.5f);
        Flush(inner, SHD_INST, &mi, 1);
    }
    sScratch[0] = Inst_Make(xf, tint, 0.0f);
    if (shell) {
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ALPHA);
        mp.alpha = 0.5f;
        mp.rimAlpha = 0.5f;
        mp.rimStrength = 1.1f;
        mp.sss = 1.0f;
        Flush(m, SHD_INST, &mp, 1);
        EndBlendMode();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    } else {
        Flush(m, SHD_INST, &mp, 1);
    }
}

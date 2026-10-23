/*
 * postfx.c - HDR targets and the post-processing chain.
 *
 *   scene (RGBA16F + depth texture, render-scale)
 *     -> bright pass (half res)  -> 2x separable blur        = bloom A
 *     -> downsample (quarter)    -> 3x separable blur        = bloom B (added into A)
 *     -> downsample scene (half) -> 1x blur                   = DOF / haze source
 *   composite: lens pulse, shimmer, chromatic aberration, depth-of-field,
 *   volumetric haze, bloom, exposure, ACES, lift/gain/saturation/contrast,
 *   vignette, damage vignette, flash, film grain, letterbox.
 */
#include "gfx/postfx.h"
#include "gfx/shaders.h"
#include "core/settings.h"
#include <string.h>

typedef struct { RenderTexture2D rt; bool ok; } Target;

static Target sScene, sBright, sBloomA, sBloomA2, sBloomB, sBloomB2, sHalf, sHalf2;
static int sW = 0, sH = 0;
static bool sReady = false;

static Target MakeTarget(int w, int h, bool depth)
{
    Target t = { 0 };
    if (w < 8) w = 8;
    if (h < 8) h = 8;
    unsigned int fbo = rlLoadFramebuffer();
    if (!fbo) return t;
    rlEnableFramebuffer(fbo);
    unsigned int col = rlLoadTexture(NULL, w, h, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    rlFramebufferAttach(fbo, col, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    unsigned int dep = 0;
    if (depth) {
        dep = rlLoadTextureDepth(w, h, false);
        rlFramebufferAttach(fbo, dep, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    }
    t.ok = rlFramebufferComplete(fbo);
    rlDisableFramebuffer();
    t.rt.id = fbo;
    t.rt.texture = (Texture2D){ col, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16 };
    t.rt.depth = (Texture2D){ dep, w, h, 1, 19 };
    SetTextureFilter(t.rt.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t.rt.texture, TEXTURE_WRAP_CLAMP);
    if (!t.ok) TraceLog(LOG_WARNING, "PZ: framebuffer %dx%d incomplete", w, h);
    return t;
}

static void FreeTarget(Target *t)
{
    if (t->rt.id) rlUnloadFramebuffer(t->rt.id);
    if (t->rt.texture.id) rlUnloadTexture(t->rt.texture.id);
    if (t->rt.depth.id) rlUnloadTexture(t->rt.depth.id);
    memset(t, 0, sizeof(*t));
}

static void FreeAll(void)
{
    FreeTarget(&sScene); FreeTarget(&sBright); FreeTarget(&sBloomA); FreeTarget(&sBloomA2);
    FreeTarget(&sBloomB); FreeTarget(&sBloomB2); FreeTarget(&sHalf); FreeTarget(&sHalf2);
}

bool PostFx_Init(int w, int h)
{
    FreeAll();
    float rs = Clamp(G.settings.renderScale, 0.5f, 1.0f);
    sW = (int)((float)w * rs);
    sH = (int)((float)h * rs);
    sScene = MakeTarget(sW, sH, true);
    sBright = MakeTarget(sW / 2, sH / 2, false);
    sBloomA = MakeTarget(sW / 2, sH / 2, false);
    sBloomA2 = MakeTarget(sW / 2, sH / 2, false);
    sBloomB = MakeTarget(sW / 4, sH / 4, false);
    sBloomB2 = MakeTarget(sW / 4, sH / 4, false);
    sHalf = MakeTarget(sW / 2, sH / 2, false);
    sHalf2 = MakeTarget(sW / 2, sH / 2, false);
    sReady = sScene.ok && sBright.ok && sBloomA.ok && sBloomA2.ok && sBloomB.ok && sBloomB2.ok && sHalf.ok && sHalf2.ok;
    return sReady;
}

void PostFx_Shutdown(void) { FreeAll(); sReady = false; }
void PostFx_Resize(int w, int h) { PostFx_Init(w, h); }
bool PostFx_Ready(void) { return sReady && G.settings.postfx; }

PostParams PostFx_Defaults(void)
{
    PostParams p;
    memset(&p, 0, sizeof(p));
    p.bloom = 0.55f;
    p.dof = 0.6f;
    p.focus = 8.0f;
    p.chroma = 0.25f;
    p.vignette = 0.55f;
    p.grain = 0.035f;
    p.exposure = 1.0f;
    p.lift = (Vector3){ 0, 0, 0 };
    p.gain = (Vector3){ 1, 1, 1 };
    p.tint = (Vector3){ 1, 1, 1 };
    p.saturation = 1.0f;
    p.contrast = 1.0f;
    p.flashColor = (Vector3){ 1, 1, 1 };
    return p;
}

void PostFx_Begin(Color clear)
{
    if (!PostFx_Ready()) { ClearBackground(clear); return; }
    BeginTextureMode(sScene.rt);
    ClearBackground(clear);
}

/* Draw `src` stretched over the current target, Y-flipped (render textures are upside down). */
static void Blit(Texture2D src, int dw, int dh)
{
    DrawTexturePro(src, (Rectangle){ 0, 0, (float)src.width, -(float)src.height }, (Rectangle){ 0, 0, (float)dw, (float)dh }, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

static void BlurPass(Target *src, Target *dst, float dx, float dy)
{
    Shader s = Shaders_Get(SHD_BLUR);
    const ShaderLocs *L = Shaders_Locs(SHD_BLUR);
    Vector2 texel = { 1.0f / (float)src->rt.texture.width, 1.0f / (float)src->rt.texture.height };
    Vector2 dir = { dx, dy };
    BeginTextureMode(dst->rt);
    BeginShaderMode(s);
    SetShaderValue(s, L->texel, &texel, SHADER_UNIFORM_VEC2);
    SetShaderValue(s, L->dir, &dir, SHADER_UNIFORM_VEC2);
    Blit(src->rt.texture, dst->rt.texture.width, dst->rt.texture.height);
    EndShaderMode();
    EndTextureMode();
}

static void Copy(Target *src, Target *dst, BlendMode mode, bool clear)
{
    BeginTextureMode(dst->rt);
    if (clear) ClearBackground(BLACK);
    BeginBlendMode(mode);
    Blit(src->rt.texture, dst->rt.texture.width, dst->rt.texture.height);
    EndBlendMode();
    EndTextureMode();
}

void PostFx_End(const PostParams *p)
{
    if (!PostFx_Ready()) return;
    EndTextureMode();
    Settings *st = &G.settings;

    /* bright pass */
    {
        Shader s = Shaders_Get(SHD_BRIGHT);
        const ShaderLocs *L = Shaders_Locs(SHD_BRIGHT);
        float thr = BLOOM_THRESHOLD, knee = 0.8f;
        BeginTextureMode(sBright.rt);
        ClearBackground(BLACK);
        BeginShaderMode(s);
        SetShaderValue(s, L->threshold, &thr, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s, L->knee, &knee, SHADER_UNIFORM_FLOAT);
        Blit(sScene.rt.texture, sBright.rt.texture.width, sBright.rt.texture.height);
        EndShaderMode();
        EndTextureMode();
    }
    bool bloom = st->bloom && p->bloom > 0.0f;
    if (bloom) {
        BlurPass(&sBright, &sBloomA2, 1, 0);
        BlurPass(&sBloomA2, &sBloomA, 0, 1);
        BlurPass(&sBloomA, &sBloomA2, 2, 0);
        BlurPass(&sBloomA2, &sBloomA, 0, 2);
        Copy(&sBloomA, &sBloomB, BLEND_ALPHA, true);
        for (int i = 0; i < 3; i++) {
            BlurPass(&sBloomB, &sBloomB2, 1.5f, 0);
            BlurPass(&sBloomB2, &sBloomB, 0, 1.5f);
        }
        Copy(&sBloomB, &sBloomA, BLEND_ADDITIVE, false);
    } else {
        BeginTextureMode(sBloomA.rt); ClearBackground(BLACK); EndTextureMode();
    }
    /* DOF / haze source */
    Copy(&sScene, &sHalf, BLEND_ALPHA, true);
    BlurPass(&sHalf, &sHalf2, 1.5f, 0);
    BlurPass(&sHalf2, &sHalf, 0, 1.5f);

    /* composite */
    Shader s = Shaders_Get(SHD_COMPOSITE);
    const ShaderLocs *L = Shaders_Locs(SHD_COMPOSITE);
    float reduce = st->reduceFlashing ? 0.35f : 1.0f;
    float bloomS = bloom ? p->bloom : 0.0f;
    float dofS = st->dof ? p->dof : 0.0f;
    float dofEnabled = st->dof ? 1.0f : 0.0f;
    float chroma = st->chromatic ? p->chroma : 0.0f;
    float grain = st->grain ? p->grain : 0.0f;
    float flash = p->flash * reduce;
    float timeV = (float)GetTime();
    Vector2 nf = { (float)CAM_NEAR, (float)CAM_FAR };
    Vector2 res = { (float)GetScreenWidth(), (float)GetScreenHeight() };
    BeginShaderMode(s);
    SetShaderValueTexture(s, L->bloomTex, sBloomA.rt.texture);
    SetShaderValueTexture(s, L->blurTex, sHalf.rt.texture);
    SetShaderValueTexture(s, L->depthTex, sScene.rt.depth);
    SetShaderValue(s, L->bloomStrength, &bloomS, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->dofStrength, &dofS, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->dofEnabled, &dofEnabled, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->focus, &p->focus, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->nearFar, &nf, SHADER_UNIFORM_VEC2);
    SetShaderValue(s, L->chroma, &chroma, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->vignette, &p->vignette, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->grain, &grain, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->pulseFx, &p->pulse, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->lift, &p->lift, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->gain, &p->gain, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->saturation, &p->saturation, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->contrast, &p->contrast, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->tint, &p->tint, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->haze, &p->haze, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->hazeColor, &p->hazeColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->flash, &flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->flashColor, &p->flashColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->damage, &p->damage, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->letterbox, &p->letterbox, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->exposure, &p->exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->shimmer, &p->shimmer, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->time, &timeV, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->resolution, &res, SHADER_UNIFORM_VEC2);
    Blit(sScene.rt.texture, GetScreenWidth(), GetScreenHeight());
    EndShaderMode();
}

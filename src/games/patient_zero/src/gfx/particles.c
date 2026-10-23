/*
 * particles.c - simulation and instanced rendering of particles.
 * A free-index stack makes spawn O(1); flow-carried motes drift with the
 * bloodstream so the fluid always looks alive.
 */
#include "gfx/particles.h"
#include "gfx/render.h"
#include "gfx/shaders.h"
#include "world/flow_field.h"
#include "core/rng.h"
#include "core/settings.h"
#include <string.h>

static Particle sP[MAX_PARTICLES];
static int sFree[MAX_PARTICLES];
static int sFreeCount = 0;
static int sAlive = 0;
static int sAmbient = 0;
static InstData sInstA[MAX_PARTICLES], sInstB[MAX_PARTICLES];
static Rng sRng;

void Particles_Init(void)
{
    Rng_Seed(&sRng, 0xBEEFu);
    Particles_Clear();
}

void Particles_Clear(void)
{
    memset(sP, 0, sizeof(sP));
    sFreeCount = 0;
    for (int i = MAX_PARTICLES - 1; i >= 0; i--) sFree[sFreeCount++] = i;
    sAlive = 0;
    sAmbient = 0;
}

static Particle *Spawn(void)
{
    if (sFreeCount == 0) return NULL;
    if (sAlive >= Settings_ParticleBudget()) return NULL;
    int i = sFree[--sFreeCount];
    Particle *p = &sP[i];
    memset(p, 0, sizeof(*p));
    p->alive = true;
    p->soft = 0.6f;
    sAlive++;
    return p;
}

void Particles_Emit(Vector3 pos, Vector3 vel, Vector4 c0, Vector4 c1, float size0, float size1, float life, float drag, uint8_t flags)
{
    Particle *p = Spawn();
    if (!p) return;
    p->pos = pos; p->vel = vel; p->c0 = c0; p->c1 = c1;
    p->size0 = size0; p->size1 = size1;
    p->life = p->maxLife = life;
    p->drag = drag; p->flags = flags;
    p->rot = Rng_Range(&sRng, 0, 6.28f);
    p->rotVel = Rng_Range(&sRng, -2.0f, 2.0f);
}

void Particles_EmitStretch(Vector3 pos, Vector3 vel, Vector4 c0, Vector4 c1, float size, float life, float stretch, uint8_t flags)
{
    Particle *p = Spawn();
    if (!p) return;
    p->pos = pos; p->vel = vel; p->c0 = c0; p->c1 = c1;
    p->size0 = size; p->size1 = size * 0.4f;
    p->life = p->maxLife = life;
    p->drag = 2.0f; p->flags = flags;
    p->stretch = stretch;
    p->soft = 0.8f;
}

void Particles_Burst(Vector3 pos, int n, float speed, Vector4 c0, Vector4 c1, float size, float life, uint8_t flags)
{
    for (int i = 0; i < n; i++) {
        Vector3 v = Vector3Scale(Rng_UnitVec(&sRng), speed * Rng_Range(&sRng, 0.25f, 1.0f));
        Particles_Emit(pos, v, c0, c1, size * Rng_Range(&sRng, 0.6f, 1.3f), size * 0.15f, life * Rng_Range(&sRng, 0.6f, 1.0f), 2.5f, flags);
    }
}

void Particles_Ring(Vector3 pos, Vector3 axis, int n, float speed, Vector4 c0, Vector4 c1, float size, float life)
{
    Vector3 a = Vector3Normalize(axis);
    Vector3 u = (fabsf(a.y) < 0.9f) ? Vector3Normalize(Vector3CrossProduct(a, (Vector3){ 0, 1, 0 })) : (Vector3){ 1, 0, 0 };
    Vector3 v = Vector3CrossProduct(a, u);
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)n * 2.0f * PI;
        Vector3 d = Vector3Add(Vector3Scale(u, cosf(t)), Vector3Scale(v, sinf(t)));
        Particle *p = Spawn();
        if (!p) return;
        p->pos = pos; p->vel = Vector3Scale(d, speed);
        p->c0 = c0; p->c1 = c1; p->size0 = size; p->size1 = size * 0.3f;
        p->life = p->maxLife = life; p->drag = 3.0f; p->flags = PF_ADD; p->stretch = 0.06f; p->soft = 0.8f;
    }
}

void Particles_Ambient(Vector3 camPos, Vector3 colorA, Vector3 colorB, int target, float dt)
{
    (void)dt;
    int want = target - sAmbient;
    if (want > 40) want = 40;
    for (int i = 0; i < want; i++) {
        Particle *p = Spawn();
        if (!p) return;
        Vector3 off = Vector3Scale(Rng_InSphere(&sRng), 55.0f);
        p->pos = Vector3Add(camPos, off);
        p->vel = Vector3Scale(Rng_UnitVec(&sRng), 0.3f);
        float t = Rng_Float(&sRng);
        Vector3 c = Vector3Lerp(colorA, colorB, t);
        float a = Rng_Range(&sRng, 0.25f, 0.7f);
        p->c0 = V4(c.x, c.y, c.z, 0.0f);
        p->c1 = V4(c.x, c.y, c.z, a);
        p->size0 = p->size1 = Rng_Range(&sRng, 0.05f, 0.22f);
        p->life = p->maxLife = Rng_Range(&sRng, 6.0f, 14.0f);
        p->drag = 0.6f;
        p->flags = PF_ADD | PF_FLOW | PF_TURB | PF_AMBIENT;
        p->soft = 0.9f;
        sAmbient++;
    }
}

void Particles_Update(float dt)
{
    float t = (float)GetTime();
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &sP[i];
        if (!p->alive) continue;
        p->life -= dt;
        if (p->life <= 0.0f) {
            p->alive = false;
            sFree[sFreeCount++] = i;
            sAlive--;
            if (p->flags & PF_AMBIENT) sAmbient--;
            continue;
        }
        if (p->flags & PF_FLOW) {
            Vector3 f = Flow_Sample(p->pos);
            p->vel = Vector3Lerp(p->vel, f, 1.0f - expf(-1.5f * dt));
        }
        if (p->flags & PF_TURB) {
            p->vel.x += sinf(p->pos.y * 0.35f + t * 0.7f) * 0.6f * dt;
            p->vel.y += sinf(p->pos.z * 0.31f + t * 0.5f) * 0.6f * dt;
            p->vel.z += sinf(p->pos.x * 0.29f + t * 0.6f) * 0.6f * dt;
        }
        if (p->flags & PF_SINK) p->vel.y -= 2.0f * dt;
        p->vel = Vector3Scale(p->vel, expf(-p->drag * dt));
        p->pos = Vector3Add(p->pos, Vector3Scale(p->vel, dt));
        p->rot += p->rotVel * dt;
    }
}

void Particles_Draw(Camera3D cam)
{
    int na = 0, nb = 0;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &sP[i];
        if (!p->alive) continue;
        Vector3 to = Vector3Subtract(p->pos, cam.position);
        if (Vector3DotProduct(to, fwd) < -2.0f) continue;
        float k = 1.0f - p->life / p->maxLife;
        float size = Lerp(p->size0, p->size1, k);
        Vector4 c = { Lerp(p->c0.x, p->c1.x, k), Lerp(p->c0.y, p->c1.y, k), Lerp(p->c0.z, p->c1.z, k), Lerp(p->c0.w, p->c1.w, k) };
        if (p->flags & PF_AMBIENT) {
            float fade = Sat(p->life / 1.5f) * Sat((p->maxLife - p->life) / 1.5f);
            c.w = p->c1.w * fade;
        }
        InstData d = { { p->pos.x, p->pos.y, p->pos.z, size }, { c.x, c.y, c.z, c.w },
                       { p->vel.x, p->vel.y, p->vel.z, p->stretch }, { p->rot, p->soft, 0, 0 } };
        if (p->flags & PF_ADD) sInstB[nb++] = d; else sInstA[na++] = d;
    }
    Shader s = Shaders_Get(SHD_PARTICLE);
    const ShaderLocs *L = Shaders_Locs(SHD_PARTICLE);
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up));
    Vector3 up = Vector3CrossProduct(right, fwd);
    SetShaderValue(s, L->camRight, &right, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->camUp, &up, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->camPos, &cam.position, SHADER_UNIFORM_VEC3);
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    if (na > 0) { BeginBlendMode(BLEND_ALPHA); Render_DrawInstanced(Render_QuadMesh(), SHD_PARTICLE, sInstA, na); EndBlendMode(); }
    if (nb > 0) { BeginBlendMode(BLEND_ADDITIVE); Render_DrawInstanced(Render_QuadMesh(), SHD_PARTICLE, sInstB, nb); EndBlendMode(); }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

int Particles_Alive(void) { return sAlive; }

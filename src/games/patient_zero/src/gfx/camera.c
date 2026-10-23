/*
 * camera.c - the camera is what makes swimming feel fluid. Position follows
 * a critically damped spring, collision pulls it in front of tissue walls
 * (raymarching the cave SDF), and shake uses trauma^2 with smooth noise.
 */
#include "gfx/camera.h"
#include "core/math_utils.h"
#include "core/noise.h"
#include "world/sdf_cave.h"

void Cam_Init(void)
{
    CameraRig *r = &G.rig;
    r->cam.up = (Vector3){ 0, 1, 0 };
    r->cam.fovy = 70.0f;
    r->cam.projection = CAMERA_PERSPECTIVE;
    r->fov = 70.0f;
    r->yaw = 0.0f;
    r->pitch = -0.15f;
    r->dist = 7.0f;
    r->mode = CAM_FOLLOW;
}

Vector3 Cam_Forward(void)
{
    float cp = cosf(G.rig.pitch);
    return (Vector3){ sinf(G.rig.yaw) * cp, sinf(G.rig.pitch), cosf(G.rig.yaw) * cp };
}

Vector3 Cam_Right(void)
{
    return Vector3Normalize(Vector3CrossProduct(Cam_Forward(), (Vector3){ 0, 1, 0 }));
}

Vector3 Cam_Up(void) { return Vector3CrossProduct(Cam_Right(), Cam_Forward()); }

void Cam_Look(float dx, float dy)
{
    G.rig.yaw -= dx;
    G.rig.pitch -= dy;
    G.rig.pitch = Clamp(G.rig.pitch, -1.4f, 1.4f);
}

void Cam_AddTrauma(float t)
{
    G.rig.trauma = Sat(G.rig.trauma + t * G.settings.shake);
}

void Cam_Kick(float fovDelta) { G.rig.fovKick += fovDelta; }

static void ApplyShake(float dt)
{
    CameraRig *r = &G.rig;
    float s = r->trauma * r->trauma;
    float t = G.realTime * 18.0f;
    r->shakeOffset = (Vector3){ Noise_Value3(t, 0.0f, 3.1f) * s * 0.9f, Noise_Value3(0.0f, t, 7.7f) * s * 0.9f, Noise_Value3(5.3f, 1.9f, t) * s * 0.9f };
    r->trauma = fmaxf(0.0f, r->trauma - dt * 1.4f);
}

static void Finalize(float dt)
{
    CameraRig *r = &G.rig;
    ApplyShake(dt);
    r->fovKick = FDamp(r->fovKick, 0.0f, 4.0f, dt);
    r->cam.fovy = r->fov + r->fovKick;
    r->cam.position = Vector3Add(r->pos, r->shakeOffset);
    float roll = r->trauma * r->trauma * Noise_Value3(G.realTime * 9.0f, 4.4f, 0.0f) * 0.08f;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(r->cam.target, r->cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, (Vector3){ 0, 1, 0 }));
    Vector3 up = Vector3CrossProduct(right, fwd);
    r->cam.up = Vector3Normalize(Vector3Add(up, Vector3Scale(right, roll)));
}

void Cam_SnapTo(Vector3 focus, float dist)
{
    CameraRig *r = &G.rig;
    r->focus = focus;
    r->dist = dist;
    r->pos = Vector3Subtract(focus, Vector3Scale(Cam_Forward(), dist));
    r->velPos = (Vector3){ 0 };
    r->cam.position = r->pos;
    r->cam.target = focus;
}

void Cam_Update(float dt, Vector3 focus, float desiredDist)
{
    CameraRig *r = &G.rig;
    if (r->mode == CAM_CINEMATIC) {
        r->cineT += dt;
        float t = r->cineT / r->cineDur;
        float ang = r->yaw + t * 2.2f;
        Vector3 c = r->cineCenter;
        Vector3 want = { c.x + sinf(ang) * r->cineRadius, c.y + r->cineHeight * (1.0f - 0.4f * t), c.z + cosf(ang) * r->cineRadius };
        r->pos = MU_SpringDampV3(r->pos, want, &r->velPos, 3.5f, dt);
        r->cam.target = Vector3Lerp(r->cam.target, c, 1.0f - expf(-4.0f * dt));
        if (r->cineT >= r->cineDur) {
            r->mode = CAM_FOLLOW;
            /* face the cinematic subject when control returns */
            Vector3 d = Vector3Subtract(c, focus);
            if (Vector3Length(d) > 0.1f) { r->yaw = atan2f(d.x, d.z); r->pitch = Clamp(atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)), -0.6f, 0.6f); }
        }
        Finalize(dt);
        return;
    }
    r->targetDist = desiredDist;
    r->dist = FDamp(r->dist, r->targetDist, 3.0f, dt);
    r->focus = V3Damp(r->focus, focus, 22.0f, dt);
    Vector3 fwd = Cam_Forward();
    Vector3 dirBack = Vector3Negate(fwd);
    float d = r->dist;
    float hit;
    float pr = G.run.active ? G.player.radius : 0.5f;
    float minD = pr * 2.4f + 1.4f;
    float margin = Clamp(Cave_Sample(r->focus) * 0.7f, 0.15f, 0.8f);
    if (Cave_Raycast(r->focus, dirBack, d, margin, &hit)) d = fmaxf(minD, hit - 0.3f);
    Vector3 want = Vector3Add(r->focus, Vector3Scale(dirBack, d));
    want = Vector3Add(want, Vector3Scale(Cam_Up(), d * 0.12f));
    r->pos = MU_SpringDampV3(r->pos, want, &r->velPos, 16.0f, dt);
    /* hard constraint: never end up inside tissue */
    float o = Cave_Sample(r->pos);
    if (o < 0.6f) {
        Vector3 g = Cave_Gradient(r->pos);
        r->pos = Vector3Add(r->pos, Vector3Scale(g, 0.6f - o));
    }
    r->cam.target = Vector3Add(r->focus, Vector3Scale(fwd, 6.0f));
    Finalize(dt);
}

void Cam_StartCinematic(Vector3 center, float radius, float height, float duration)
{
    CameraRig *r = &G.rig;
    r->mode = CAM_CINEMATIC;
    r->cineCenter = center;
    r->cineRadius = radius;
    r->cineHeight = height;
    r->cineDur = duration;
    r->cineT = 0.0f;
}

void Cam_EnterFree(void)
{
    CameraRig *r = &G.rig;
    r->mode = CAM_FREE;
    r->freePos = r->cam.position;
    r->freeYaw = r->yaw;
    r->freePitch = r->pitch;
}

void Cam_UpdateFree(float dt)
{
    CameraRig *r = &G.rig;
    Vector2 md = GetMouseDelta();
    float sens = 0.0025f * G.settings.sensitivity;
    r->freeYaw -= md.x * sens;
    r->freePitch -= md.y * sens * (G.settings.invertY ? -1.0f : 1.0f);
    r->freePitch = Clamp(r->freePitch, -1.5f, 1.5f);
    float cp = cosf(r->freePitch);
    Vector3 f = { sinf(r->freeYaw) * cp, sinf(r->freePitch), cosf(r->freeYaw) * cp };
    Vector3 rt = Vector3Normalize(Vector3CrossProduct(f, (Vector3){ 0, 1, 0 }));
    float sp = IsKeyDown(KEY_LEFT_SHIFT) ? 40.0f : 12.0f;
    Vector3 mv = { 0 };
    if (IsKeyDown(KEY_W)) mv = Vector3Add(mv, f);
    if (IsKeyDown(KEY_S)) mv = Vector3Subtract(mv, f);
    if (IsKeyDown(KEY_D)) mv = Vector3Add(mv, rt);
    if (IsKeyDown(KEY_A)) mv = Vector3Subtract(mv, rt);
    if (IsKeyDown(KEY_SPACE)) mv.y += 1.0f;
    if (IsKeyDown(KEY_LEFT_CONTROL)) mv.y -= 1.0f;
    r->freePos = Vector3Add(r->freePos, Vector3Scale(mv, sp * dt));
    r->cam.position = r->freePos;
    r->cam.target = Vector3Add(r->freePos, f);
    r->cam.up = (Vector3){ 0, 1, 0 };
    r->cam.fovy = r->fov;
}

void Cam_UpdateMenu(float dt)
{
    /* drift slowly along the region's main vessel */
    CameraRig *r = &G.rig;
    RegionState *rg = &G.region;
    if (rg->pathCount < 4) return;
    float s = fmodf(G.realTime * 4.0f, rg->pathLength * 0.8f);
    int i = 0;
    while (i < rg->pathCount - 2 && rg->pathS[i + 1] < s) i++;
    float t = (s - rg->pathS[i]) / fmaxf(0.001f, rg->pathS[i + 1] - rg->pathS[i]);
    Vector3 p = Vector3Lerp(rg->path[i], rg->path[i + 1], t);
    Vector3 ahead = rg->path[(i + 3 < rg->pathCount) ? i + 3 : rg->pathCount - 1];
    Vector3 want = Vector3Add(p, (Vector3){ sinf(G.realTime * 0.13f) * 4.0f, 2.0f + sinf(G.realTime * 0.21f) * 2.0f, 0 });
    r->pos = MU_SpringDampV3(r->pos, want, &r->velPos, 1.2f, dt);
    r->cam.position = r->pos;
    r->cam.target = Vector3Lerp(r->cam.target, ahead, 1.0f - expf(-0.8f * dt));
    r->cam.up = (Vector3){ 0, 1, 0 };
    r->cam.fovy = 65.0f;
}

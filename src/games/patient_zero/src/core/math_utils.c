/*
 * math_utils.c - small vector/matrix helpers that raymath lacks.
 * Matrices follow raylib's convention: columns are the basis vectors.
 */
#include "core/math_utils.h"

Vector3 MU_AnyPerp(Vector3 v)
{
    Vector3 a = (fabsf(v.y) < 0.95f) ? (Vector3){ 0, 1, 0 } : (Vector3){ 1, 0, 0 };
    return Vector3Normalize(Vector3CrossProduct(v, a));
}

/* Right-handed basis with local +Z along fwd. */
Matrix MU_Basis(Vector3 fwd, Vector3 upHint, Vector3 pos, Vector3 scale)
{
    Vector3 f = Vector3Normalize(fwd);
    if (Vector3LengthSqr(f) < 1e-8f) f = (Vector3){ 0, 0, 1 };
    Vector3 u0 = (fabsf(Vector3DotProduct(f, upHint)) > 0.98f) ? MU_AnyPerp(f) : upHint;
    Vector3 r = Vector3Normalize(Vector3CrossProduct(u0, f));
    Vector3 u = Vector3CrossProduct(f, r);
    Matrix m = { 0 };
    m.m0 = r.x * scale.x; m.m1 = r.y * scale.x; m.m2 = r.z * scale.x;
    m.m4 = u.x * scale.y; m.m5 = u.y * scale.y; m.m6 = u.z * scale.y;
    m.m8 = f.x * scale.z; m.m9 = f.y * scale.z; m.m10 = f.z * scale.z;
    m.m12 = pos.x; m.m13 = pos.y; m.m14 = pos.z; m.m15 = 1.0f;
    return m;
}

/* Basis with local +Y along axisY (for wall-anchored cells and cilia). */
Matrix MU_BasisAxis(Vector3 axisY, Vector3 pos, float scale)
{
    Vector3 y = Vector3Normalize(axisY);
    Vector3 x = MU_AnyPerp(y);
    Vector3 z = Vector3CrossProduct(x, y);
    Matrix m = { 0 };
    m.m0 = x.x * scale; m.m1 = x.y * scale; m.m2 = x.z * scale;
    m.m4 = y.x * scale; m.m5 = y.y * scale; m.m6 = y.z * scale;
    m.m8 = z.x * scale; m.m9 = z.y * scale; m.m10 = z.z * scale;
    m.m12 = pos.x; m.m13 = pos.y; m.m14 = pos.z; m.m15 = 1.0f;
    return m;
}

Vector3 MU_ClosestOnSegment(Vector3 p, Vector3 a, Vector3 b, float *t)
{
    Vector3 ab = Vector3Subtract(b, a);
    float len2 = Vector3LengthSqr(ab);
    float tt = (len2 > 1e-8f) ? Vector3DotProduct(Vector3Subtract(p, a), ab) / len2 : 0.0f;
    tt = Sat(tt);
    if (t) *t = tt;
    return Vector3Add(a, Vector3Scale(ab, tt));
}

float MU_AngleWrap(float a)
{
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

float MU_EaseOutBack(float t) { const float c1 = 1.70158f, c3 = c1 + 1.0f; t -= 1.0f; return 1.0f + c3 * t * t * t + c1 * t * t; }
float MU_EaseOutCubic(float t) { t = 1.0f - Sat(t); return 1.0f - t * t * t; }
float MU_EaseInOut(float t) { t = Sat(t); return t * t * (3.0f - 2.0f * t); }

Vector3 MU_HSV(float h, float s, float v)
{
    h = fmodf(h, 1.0f); if (h < 0) h += 1.0f;
    float r = fabsf(h * 6.0f - 3.0f) - 1.0f;
    float g = 2.0f - fabsf(h * 6.0f - 2.0f);
    float b = 2.0f - fabsf(h * 6.0f - 4.0f);
    r = Sat(r); g = Sat(g); b = Sat(b);
    return (Vector3){ v * (1.0f + s * (r - 1.0f)), v * (1.0f + s * (g - 1.0f)), v * (1.0f + s * (b - 1.0f)) };
}

Vector3 MU_ColorToV3(Color c) { return (Vector3){ c.r / 255.0f, c.g / 255.0f, c.b / 255.0f }; }
Color MU_V3ToColor(Vector3 c, float a) { return ColorF(c.x, c.y, c.z, a); }

Vector3 MU_RotateAround(Vector3 v, Vector3 axis, float ang)
{
    return Vector3RotateByAxisAngle(v, Vector3Normalize(axis), ang);
}

bool MU_RaySphere(Vector3 ro, Vector3 rd, Vector3 c, float r, float *t)
{
    Vector3 oc = Vector3Subtract(ro, c);
    float b = Vector3DotProduct(oc, rd);
    float cc = Vector3DotProduct(oc, oc) - r * r;
    float h = b * b - cc;
    if (h < 0.0f) return false;
    float s = -b - sqrtf(h);
    if (s < 0.0f) s = -b + sqrtf(h);
    if (s < 0.0f) return false;
    *t = s;
    return true;
}

Vector3 MU_CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float t2 = t * t, t3 = t2 * t;
    Vector3 r;
    r.x = 0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
    r.y = 0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
    r.z = 0.5f * ((2 * p1.z) + (-p0.z + p2.z) * t + (2 * p0.z - 5 * p1.z + 4 * p2.z - p3.z) * t2 + (-p0.z + 3 * p1.z - 3 * p2.z + p3.z) * t3);
    return r;
}

/* Critically damped spring (no overshoot), frame-rate independent. */
float MU_SpringDamp(float x, float target, float *vel, float omega, float dt)
{
    float f = 1.0f + 2.0f * dt * omega;
    float oo = omega * omega;
    float hoo = dt * oo, hhoo = dt * hoo;
    float det = 1.0f / (f + hhoo);
    float xn = det * (f * x + dt * (*vel) + hhoo * target);
    *vel = det * ((*vel) + hoo * (target - x));
    return xn;
}

Vector3 MU_SpringDampV3(Vector3 x, Vector3 target, Vector3 *vel, float omega, float dt)
{
    Vector3 r;
    r.x = MU_SpringDamp(x.x, target.x, &vel->x, omega, dt);
    r.y = MU_SpringDamp(x.y, target.y, &vel->y, omega, dt);
    r.z = MU_SpringDamp(x.z, target.z, &vel->z, omega, dt);
    return r;
}

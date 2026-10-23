/*
 * flow_field.c - each coarse node stores the tangent of the nearest tube
 * (faster near the tube centre, like laminar flow) and the path distance
 * `s`, which lets peristalsis send a travelling wave down the gut.
 */
#include "world/flow_field.h"
#include "world/sdf_cave.h"
#include "core/math_utils.h"

#define FN (FLOW_NX * FLOW_NY * FLOW_NZ)
#define FIDX(i, j, k) (((k) * FLOW_NY + (j)) * FLOW_NX + (i))

static Vector3 *sVec = NULL;
static float *sS = NULL;
static float sMul = 1.0f, sWaveS = -1000.0f, sWaveAmp = 0.0f, sReverse = 0.0f;

bool Flow_Init(void)
{
    sVec = (Vector3 *)MemAlloc((unsigned int)(sizeof(Vector3) * FN));
    sS = (float *)MemAlloc((unsigned int)(sizeof(float) * FN));
    return sVec && sS;
}

void Flow_Shutdown(void) { MemFree(sVec); MemFree(sS); sVec = NULL; sS = NULL; }

void Flow_SetModifiers(float mul, float waveS, float waveAmp, float reverse)
{
    sMul = mul; sWaveS = waveS; sWaveAmp = waveAmp; sReverse = reverse;
}

void Flow_Build(const RegionState *r, float strength)
{
    for (int k = 0; k < FLOW_NZ; k++) for (int j = 0; j < FLOW_NY; j++) for (int i = 0; i < FLOW_NX; i++) {
        Vector3 p = { WORLD_MIN_X + i * FLOW_CELL, WORLD_MIN_Y + j * FLOW_CELL, WORLD_MIN_Z + k * FLOW_CELL };
        int idx = FIDX(i, j, k);
        sVec[idx] = (Vector3){ 0 };
        sS[idx] = 0.0f;
        if (Cave_Sample(p) < -4.0f) continue;
        float best = 1e9f;
        const CaveShape *bs = NULL;
        float bt = 0.0f;
        for (int s = 0; s < r->shapeCount; s++) {
            const CaveShape *sh = &r->shapes[s];
            if (sh->kind != 0) continue;
            float t;
            Vector3 c = MU_ClosestOnSegment(p, sh->a, sh->b, &t);
            float rr = Lerp(sh->r0, sh->r1, t);
            float d = Vector3Distance(p, c) / fmaxf(rr, 1.0f);
            if (d < best) { best = d; bs = sh; bt = t; }
        }
        if (!bs) continue;
        Vector3 tng = Vector3Normalize(Vector3Subtract(bs->b, bs->a));
        float prof = Clamp(1.0f - best * best * 0.6f, 0.15f, 1.0f);
        float branchMul = (bs->branch < 0) ? 1.0f : 0.45f;
        Vector3 v = Vector3Scale(tng, strength * prof * branchMul);
        /* near tissue, flow slides along the wall instead of into it (no stagnant pile-ups in funnels) */
        Vector3 g;
        float o = Cave_SampleGrad(p, &g);
        float gl = Vector3Length(g);
        if (o < 9.0f && gl > 1e-4f) {
            g = Vector3Scale(g, 1.0f / gl);
            float vn = Vector3DotProduct(v, g);
            if (vn < 0.0f) v = Vector3Subtract(v, Vector3Scale(g, vn * (1.0f + 0.3f * Sat(1.0f - o / 9.0f))));
        }
        sVec[idx] = v;
        sS[idx] = Lerp(bs->s0, bs->s1, bt);
    }
}

static void Sample(Vector3 p, Vector3 *v, float *s)
{
    float gx = (p.x - WORLD_MIN_X) / FLOW_CELL, gy = (p.y - WORLD_MIN_Y) / FLOW_CELL, gz = (p.z - WORLD_MIN_Z) / FLOW_CELL;
    gx = Clamp(gx, 0.0f, (float)FLOW_NX - 1.001f);
    gy = Clamp(gy, 0.0f, (float)FLOW_NY - 1.001f);
    gz = Clamp(gz, 0.0f, (float)FLOW_NZ - 1.001f);
    int i = (int)gx, j = (int)gy, k = (int)gz;
    float fx = gx - i, fy = gy - j, fz = gz - k;
    Vector3 acc = { 0 };
    float sacc = 0.0f;
    for (int c = 0; c < 8; c++) {
        int di = c & 1, dj = (c >> 1) & 1, dk = (c >> 2) & 1;
        float w = (di ? fx : 1 - fx) * (dj ? fy : 1 - fy) * (dk ? fz : 1 - fz);
        int idx = FIDX(i + di, j + dj, k + dk);
        acc = Vector3Add(acc, Vector3Scale(sVec[idx], w));
        sacc += sS[idx] * w;
    }
    *v = acc;
    if (s) *s = sacc;
}

Vector3 Flow_Sample(Vector3 p)
{
    if (!sVec) return (Vector3){ 0 };
    Vector3 v;
    float s;
    Sample(p, &v, &s);
    float m = sMul;
    if (sWaveAmp > 0.0f) {
        float d = (s - sWaveS) / 28.0f;
        m += sWaveAmp * expf(-d * d);
    }
    if (sReverse > 0.0f) m -= sReverse;
    return Vector3Scale(v, m);
}

float Flow_PathS(Vector3 p)
{
    if (!sVec) return 0.0f;
    Vector3 v;
    float s;
    Sample(p, &v, &s);
    return s;
}

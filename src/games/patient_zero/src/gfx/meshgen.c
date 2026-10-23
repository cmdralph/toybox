/*
 * meshgen.c - procedural geometry for every organism.
 *
 * Shapes are modelled loosely on microscopy references:
 *   - red blood cells use the Evans-Fung biconcave disc profile
 *   - neutrophils carry a 3-5 lobed nucleus
 *   - macrophages are ruffled blobs with pseudopod extensions
 *   - T cells bristle with microvilli (TCR clusters), B cells display
 *     membrane-bound Y-shaped antibodies
 *   - the pathogen is an icosahedral capsid, like most real viruses
 * Normals of displaced blobs come from finite differences on the sphere so
 * lighting stays smooth even with strong displacement.
 */
#include "gfx/meshgen.h"
#include "core/noise.h"
#include "core/math_utils.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Builder                                                                    */
/* ------------------------------------------------------------------------- */

void MB_Init(MeshBuilder *mb, int vcap, int icap)
{
    memset(mb, 0, sizeof(*mb));
    mb->vcap = vcap;
    mb->icap = icap;
    mb->pos = (float *)MemAlloc((unsigned int)(vcap * 3 * sizeof(float)));
    mb->nrm = (float *)MemAlloc((unsigned int)(vcap * 3 * sizeof(float)));
    mb->uv = (float *)MemAlloc((unsigned int)(vcap * 2 * sizeof(float)));
    mb->col = (unsigned char *)MemAlloc((unsigned int)(vcap * 4));
    mb->idx = (unsigned short *)MemAlloc((unsigned int)(icap * sizeof(unsigned short)));
}

void MB_Free(MeshBuilder *mb)
{
    MemFree(mb->pos); MemFree(mb->nrm); MemFree(mb->uv); MemFree(mb->col); MemFree(mb->idx);
    memset(mb, 0, sizeof(*mb));
}

int MB_Vertex(MeshBuilder *mb, Vector3 p, Vector3 n, Vector2 uv, Color c)
{
    if (mb->vcount >= mb->vcap || mb->vcount >= 65535) { mb->overflow = true; return mb->vcount > 0 ? mb->vcount - 1 : 0; }
    int i = mb->vcount++;
    mb->pos[i * 3] = p.x; mb->pos[i * 3 + 1] = p.y; mb->pos[i * 3 + 2] = p.z;
    mb->nrm[i * 3] = n.x; mb->nrm[i * 3 + 1] = n.y; mb->nrm[i * 3 + 2] = n.z;
    mb->uv[i * 2] = uv.x; mb->uv[i * 2 + 1] = uv.y;
    mb->col[i * 4] = c.r; mb->col[i * 4 + 1] = c.g; mb->col[i * 4 + 2] = c.b; mb->col[i * 4 + 3] = c.a;
    return i;
}

void MB_Tri(MeshBuilder *mb, int a, int b, int c)
{
    if (mb->icount + 3 > mb->icap) { mb->overflow = true; return; }
    mb->idx[mb->icount++] = (unsigned short)a;
    mb->idx[mb->icount++] = (unsigned short)b;
    mb->idx[mb->icount++] = (unsigned short)c;
}

Mesh MB_Upload(MeshBuilder *mb)
{
    if (mb->overflow) TraceLog(LOG_WARNING, "PZ: mesh builder overflow (v=%d i=%d)", mb->vcount, mb->icount);
    Mesh m = { 0 };
    m.vertexCount = mb->vcount;
    m.triangleCount = mb->icount / 3;
    m.vertices = (float *)MemAlloc((unsigned int)(mb->vcount * 3 * sizeof(float)));
    m.normals = (float *)MemAlloc((unsigned int)(mb->vcount * 3 * sizeof(float)));
    m.texcoords = (float *)MemAlloc((unsigned int)(mb->vcount * 2 * sizeof(float)));
    m.colors = (unsigned char *)MemAlloc((unsigned int)(mb->vcount * 4));
    m.indices = (unsigned short *)MemAlloc((unsigned int)(mb->icount * sizeof(unsigned short)));
    memcpy(m.vertices, mb->pos, (size_t)mb->vcount * 3 * sizeof(float));
    memcpy(m.normals, mb->nrm, (size_t)mb->vcount * 3 * sizeof(float));
    memcpy(m.texcoords, mb->uv, (size_t)mb->vcount * 2 * sizeof(float));
    memcpy(m.colors, mb->col, (size_t)mb->vcount * 4);
    memcpy(m.indices, mb->idx, (size_t)mb->icount * sizeof(unsigned short));
    MB_Free(mb);
    UploadMesh(&m, false);
    return m;
}

static Vector3 XP(Matrix m, Vector3 p) { return Vector3Transform(p, m); }

static Vector3 XN(Matrix nm, Vector3 n)
{
    Vector3 r = { nm.m0 * n.x + nm.m4 * n.y + nm.m8 * n.z, nm.m1 * n.x + nm.m5 * n.y + nm.m9 * n.z, nm.m2 * n.x + nm.m6 * n.y + nm.m10 * n.z };
    return Vector3Normalize(r);
}

static Matrix NormalMatrix(Matrix xf) { return MatrixTranspose(MatrixInvert(xf)); }

static Color Mix(Color a, Color b, float t)
{
    t = Sat(t);
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}

/* ------------------------------------------------------------------------- */
/* Primitives                                                                 */
/* ------------------------------------------------------------------------- */

static float BlobRadius(const BlobParams *bp, Vector3 d)
{
    float r = 1.0f;
    float o = (float)(bp->seed % 997) * 1.37f;
    if (bp->lowAmp != 0.0f) r += bp->lowAmp * Noise_Fbm3(d.x * bp->lowFreq + o, d.y * bp->lowFreq, d.z * bp->lowFreq - o, 3);
    if (bp->hiAmp != 0.0f) {
        float h = Noise_Fbm3(d.x * bp->hiFreq - o, d.y * bp->hiFreq + o, d.z * bp->hiFreq, 2);
        if (bp->ridged > 0.0f) h = Lerp(h, Noise_Ridged3(d.x * bp->hiFreq + o, d.y * bp->hiFreq, d.z * bp->hiFreq, 3) * 2.0f - 1.0f, bp->ridged);
        r += bp->hiAmp * h;
    }
    return r;
}

static Vector3 BlobPoint(const BlobParams *bp, float th, float ph)
{
    Vector3 d = { sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph) };
    float r = BlobRadius(bp, d);
    Vector3 p = Vector3Scale(d, r);
    p.y *= (1.0f - bp->flatten);
    return p;
}

void MB_AddBlob(MeshBuilder *mb, Matrix xf, int rings, int slices, const BlobParams *bp)
{
    Matrix nm = NormalMatrix(xf);
    int base = mb->vcount;
    for (int i = 0; i <= rings; i++) {
        float th = PI * (float)i / (float)rings;
        for (int j = 0; j <= slices; j++) {
            float ph = 2.0f * PI * (float)j / (float)slices;
            Vector3 p = BlobPoint(bp, th, ph);
            float e = 0.01f;
            float thA = Clamp(th, 0.02f, PI - 0.02f);
            Vector3 pt = BlobPoint(bp, thA + e, ph);
            Vector3 pm = BlobPoint(bp, thA - e, ph);
            Vector3 pp = BlobPoint(bp, thA, ph + e);
            Vector3 pq = BlobPoint(bp, thA, ph - e);
            Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(pp, pq), Vector3Subtract(pt, pm)));
            Vector3 radial = Vector3Normalize(p);
            if (Vector3DotProduct(n, radial) < 0.0f) n = Vector3Negate(n);
            if (i == 0 || i == rings) n = radial;
            float disp = Vector3Length(p) - 1.0f;
            float amp = fabsf(bp->lowAmp) + fabsf(bp->hiAmp) + 1e-4f;
            Color c = Mix(bp->colA, bp->colB, 0.5f + 0.5f * disp / amp);
            c.a = bp->alpha ? bp->alpha : 255;
            MB_Vertex(mb, XP(xf, p), XN(nm, n), (Vector2){ (float)j / slices, (float)i / rings }, c);
        }
    }
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < slices; j++) {
            int a = base + i * (slices + 1) + j, b = a + slices + 1;
            MB_Tri(mb, a, a + 1, b);
            MB_Tri(mb, a + 1, b + 1, b);
        }
    }
}

void MB_AddSphere(MeshBuilder *mb, Matrix xf, int rings, int slices, Color c)
{
    BlobParams bp = { 0 };
    bp.colA = bp.colB = c;
    bp.alpha = c.a;
    MB_AddBlob(mb, xf, rings, slices, &bp);
}

/* Cone along +Y from base (y=0, r=1) to tip (y=1). */
void MB_AddCone(MeshBuilder *mb, Matrix xf, int slices, Color base, Color tip)
{
    Matrix nm = NormalMatrix(xf);
    int b0 = mb->vcount;
    float slope = 1.0f / sqrtf(2.0f);
    for (int j = 0; j <= slices; j++) {
        float a = 2.0f * PI * (float)j / (float)slices;
        Vector3 d = { cosf(a), 0.0f, sinf(a) };
        Vector3 n = Vector3Normalize((Vector3){ d.x, slope, d.z });
        MB_Vertex(mb, XP(xf, d), XN(nm, n), (Vector2){ (float)j / slices, 0 }, base);
        MB_Vertex(mb, XP(xf, (Vector3){ 0, 1, 0 }), XN(nm, n), (Vector2){ (float)j / slices, 1 }, tip);
    }
    for (int j = 0; j < slices; j++) {
        int a = b0 + j * 2;
        MB_Tri(mb, a, a + 1, a + 2);
    }
}

/* Open cylinder along +Y, y in [0,1], radius r0 at bottom to r1 at top. */
void MB_AddCylinder(MeshBuilder *mb, Matrix xf, float r0, float r1, int slices, Color c0, Color c1)
{
    Matrix nm = NormalMatrix(xf);
    int b0 = mb->vcount;
    for (int j = 0; j <= slices; j++) {
        float a = 2.0f * PI * (float)j / (float)slices;
        Vector3 d = { cosf(a), 0.0f, sinf(a) };
        Vector3 n = Vector3Normalize((Vector3){ d.x, (r0 - r1), d.z });
        MB_Vertex(mb, XP(xf, Vector3Scale(d, r0)), XN(nm, n), (Vector2){ (float)j / slices, 0 }, c0);
        MB_Vertex(mb, XP(xf, (Vector3){ d.x * r1, 1.0f, d.z * r1 }), XN(nm, n), (Vector2){ (float)j / slices, 1 }, c1);
    }
    for (int j = 0; j < slices; j++) {
        int a = b0 + j * 2;
        MB_Tri(mb, a, a + 1, a + 2);
        MB_Tri(mb, a + 1, a + 3, a + 2);
    }
}

/* Capsule along local Y centred at origin. */
void MB_AddCapsule(MeshBuilder *mb, Matrix xf, float radius, float halfLen, int segs, Color c)
{
    Matrix nm = NormalMatrix(xf);
    int rings = segs, slices = segs * 2;
    int base = mb->vcount;
    int total = rings * 2 + 1;
    for (int i = 0; i <= total; i++) {
        float th;
        float yoff;
        if (i <= rings) { th = (PI * 0.5f) * (float)i / (float)rings; yoff = halfLen; }
        else { th = PI * 0.5f + (PI * 0.5f) * (float)(i - rings - 1) / (float)rings; yoff = -halfLen; }
        if (i == rings + 1) th = PI * 0.5f;
        for (int j = 0; j <= slices; j++) {
            float ph = 2.0f * PI * (float)j / (float)slices;
            Vector3 n = { sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph) };
            Vector3 p = Vector3Add(Vector3Scale(n, radius), (Vector3){ 0, yoff, 0 });
            MB_Vertex(mb, XP(xf, p), XN(nm, n), (Vector2){ (float)j / slices, (float)i / total }, c);
        }
    }
    for (int i = 0; i < total; i++) {
        for (int j = 0; j < slices; j++) {
            int a = base + i * (slices + 1) + j, b = a + slices + 1;
            MB_Tri(mb, a, a + 1, b);
            MB_Tri(mb, a + 1, b + 1, b);
        }
    }
}

void MB_AddTorus(MeshBuilder *mb, Matrix xf, float R, float r, int segR, int segr, Color c)
{
    Matrix nm = NormalMatrix(xf);
    int base = mb->vcount;
    for (int i = 0; i <= segR; i++) {
        float u = 2.0f * PI * (float)i / (float)segR;
        Vector3 cen = { cosf(u) * R, 0, sinf(u) * R };
        for (int j = 0; j <= segr; j++) {
            float v = 2.0f * PI * (float)j / (float)segr;
            Vector3 n = { cosf(u) * cosf(v), sinf(v), sinf(u) * cosf(v) };
            MB_Vertex(mb, XP(xf, Vector3Add(cen, Vector3Scale(n, r))), XN(nm, n), (Vector2){ (float)i / segR, (float)j / segr }, c);
        }
    }
    for (int i = 0; i < segR; i++) {
        for (int j = 0; j < segr; j++) {
            int a = base + i * (segr + 1) + j, b = a + segr + 1;
            MB_Tri(mb, a, a + 1, b);
            MB_Tri(mb, a + 1, b + 1, b);
        }
    }
}

/* Faceted (flat-shaded) icosphere: capsids and crystals. */
void MB_AddIcoFlat(MeshBuilder *mb, Matrix xf, int subdiv, Color a, Color b, uint32_t seed)
{
    const float t = 1.6180339887f;
    Vector3 v[12] = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
                      { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
    int f[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 },
                     { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 },
                     { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    Matrix nm = NormalMatrix(xf);
    for (int fi = 0; fi < 20; fi++) {
        Vector3 A = Vector3Normalize(v[f[fi][0]]), B = Vector3Normalize(v[f[fi][1]]), C = Vector3Normalize(v[f[fi][2]]);
        int n = 1 << subdiv;
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n - i; j++) {
                for (int k = 0; k < 2; k++) {
                    if (k == 1 && j == n - i - 1) continue;
                    float i0 = (float)i, j0 = (float)j;
                    Vector3 P[3];
                    float uvw[3][2];
                    if (k == 0) { uvw[0][0] = i0; uvw[0][1] = j0; uvw[1][0] = i0 + 1; uvw[1][1] = j0; uvw[2][0] = i0; uvw[2][1] = j0 + 1; }
                    else { uvw[0][0] = i0 + 1; uvw[0][1] = j0; uvw[1][0] = i0 + 1; uvw[1][1] = j0 + 1; uvw[2][0] = i0; uvw[2][1] = j0 + 1; }
                    for (int q = 0; q < 3; q++) {
                        float wa = uvw[q][0] / n, wb = uvw[q][1] / n, wc = 1.0f - wa - wb;
                        Vector3 p = Vector3Add(Vector3Add(Vector3Scale(B, wa), Vector3Scale(C, wb)), Vector3Scale(A, wc));
                        P[q] = Vector3Normalize(p);
                    }
                    Vector3 nn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(P[1], P[0]), Vector3Subtract(P[2], P[0])));
                    Vector3 cen = Vector3Scale(Vector3Add(Vector3Add(P[0], P[1]), P[2]), 1.0f / 3.0f);
                    if (Vector3DotProduct(nn, cen) < 0) { Vector3 tmp = P[1]; P[1] = P[2]; P[2] = tmp; nn = Vector3Negate(nn); }
                    uint32_t hs = seed ^ (uint32_t)(fi * 7919 + i * 131 + j * 31 + k);
                    hs ^= hs >> 13; hs *= 0x5bd1e995u; hs ^= hs >> 15;
                    Color c = Mix(a, b, (float)(hs & 255) / 255.0f);
                    int i1 = MB_Vertex(mb, XP(xf, P[0]), XN(nm, nn), (Vector2){ 0, 0 }, c);
                    int i2 = MB_Vertex(mb, XP(xf, P[1]), XN(nm, nn), (Vector2){ 1, 0 }, c);
                    int i3 = MB_Vertex(mb, XP(xf, P[2]), XN(nm, nn), (Vector2){ 0, 1 }, c);
                    MB_Tri(mb, i1, i2, i3);
                }
            }
        }
    }
}

/* Lathe around Y: profile points are (radius, y). */
void MB_AddLathe(MeshBuilder *mb, Matrix xf, const Vector2 *profile, int count, int slices, Color inner, Color outer)
{
    Matrix nm = NormalMatrix(xf);
    int base = mb->vcount;
    float maxR = 0.001f;
    for (int i = 0; i < count; i++) maxR = fmaxf(maxR, profile[i].x);
    for (int i = 0; i < count; i++) {
        Vector2 p = profile[i];
        Vector2 prev = profile[i > 0 ? i - 1 : i], next = profile[i < count - 1 ? i + 1 : i];
        Vector2 tng = { next.x - prev.x, next.y - prev.y };
        Vector2 n2 = { -tng.y, tng.x };
        float l = sqrtf(n2.x * n2.x + n2.y * n2.y);
        if (l > 1e-6f) { n2.x /= l; n2.y /= l; }
        Color c = Mix(inner, outer, p.x / maxR);
        for (int j = 0; j <= slices; j++) {
            float a = 2.0f * PI * (float)j / (float)slices;
            Vector3 pos = { p.x * cosf(a), p.y, p.x * sinf(a) };
            Vector3 n = { n2.x * cosf(a), n2.y, n2.x * sinf(a) };
            MB_Vertex(mb, XP(xf, pos), XN(nm, n), (Vector2){ (float)j / slices, (float)i / count }, c);
        }
    }
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < slices; j++) {
            int a = base + i * (slices + 1) + j, b = a + slices + 1;
            MB_Tri(mb, a, a + 1, b);
            MB_Tri(mb, a + 1, b + 1, b);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Organisms                                                                  */
/* ------------------------------------------------------------------------- */

Mesh MeshGen_RBC(void)
{
    /* Evans-Fung: h(r) = sqrt(1-x^2) * (C0 + C1 x^2 + C2 x^4), x = r/R */
    const float C0 = 0.207f, C1 = 2.003f, C2 = -1.123f;
    Vector2 prof[40];
    int n = 0;
    const int half = 19;
    for (int i = 0; i <= half; i++) {
        float x = (float)i / (float)half;
        x = sinf(x * PI * 0.5f);
        float h = sqrtf(fmaxf(0.0f, 1.0f - x * x)) * (C0 + C1 * x * x + C2 * x * x * x * x) * 0.5f;
        prof[n++] = (Vector2){ x * 0.999f, h + 0.02f };
    }
    for (int i = half; i >= 0; i--) {
        Vector2 p = prof[i];
        prof[n++] = (Vector2){ p.x, -p.y };
    }
    MeshBuilder mb;
    MB_Init(&mb, 4000, 12000);
    MB_AddLathe(&mb, MatrixIdentity(), prof, n, 28, (Color){ 225, 120, 110, 255 }, (Color){ 200, 30, 40, 255 });
    return MB_Upload(&mb);
}

Mesh MeshGen_Platelet(void)
{
    BlobParams bp = { .lowAmp = 0.28f, .lowFreq = 1.6f, .hiAmp = 0.06f, .hiFreq = 5.0f, .seed = 77,
                      .colA = { 240, 210, 150, 255 }, .colB = { 210, 160, 110, 255 }, .flatten = 0.62f };
    MeshBuilder mb;
    MB_Init(&mb, 2000, 8000);
    MB_AddBlob(&mb, MatrixIdentity(), 12, 18, &bp);
    return MB_Upload(&mb);
}

Mesh MeshGen_Blob(const BlobParams *bp, int rings, int slices)
{
    MeshBuilder mb;
    MB_Init(&mb, (rings + 1) * (slices + 1) + 8, rings * slices * 6 + 8);
    MB_AddBlob(&mb, MatrixIdentity(), rings, slices, bp);
    return MB_Upload(&mb);
}

Mesh MeshGen_Nucleus(int lobes, uint32_t seed, Color c)
{
    MeshBuilder mb;
    MB_Init(&mb, 6000, 30000);
    Color c2 = (Color){ (unsigned char)(c.r * 0.7f), (unsigned char)(c.g * 0.7f), (unsigned char)(c.b * 0.7f), 255 };
    for (int i = 0; i < lobes; i++) {
        float a = (float)i / (float)lobes * 2.0f * PI;
        float r = (lobes > 1) ? 0.33f : 0.0f;
        BlobParams bp = { .lowAmp = 0.12f, .lowFreq = 2.0f, .hiAmp = 0.05f, .hiFreq = 7.0f, .seed = seed + (uint32_t)i * 17,
                          .colA = c, .colB = c2 };
        float s = (lobes > 1) ? 0.36f : 0.55f;
        Matrix xf = MatrixMultiply(MatrixScale(s, s * 0.9f, s), MatrixTranslate(cosf(a) * r, sinf(a * 2.0f) * 0.08f, sinf(a) * r));
        MB_AddBlob(&mb, xf, 10, 14, &bp);
        if (lobes > 1 && i < lobes - 1) {
            float a2 = (float)(i + 1) / (float)lobes * 2.0f * PI;
            Vector3 p0 = { cosf(a) * r, 0, sinf(a) * r }, p1 = { cosf(a2) * r, 0, sinf(a2) * r };
            Vector3 mid = Vector3Scale(Vector3Add(p0, p1), 0.5f);
            Vector3 dir = Vector3Subtract(p1, p0);
            Matrix m = MU_BasisAxis(dir, mid, 1.0f);
            m = MatrixMultiply(MatrixScale(1.0f, Vector3Length(dir) * 0.5f, 1.0f), m);
            MB_AddCapsule(&mb, m, 0.07f, 0.6f, 4, c2);
        }
    }
    return MB_Upload(&mb);
}

Mesh MeshGen_Neuron(uint32_t seed)
{
    MeshBuilder mb;
    MB_Init(&mb, 20000, 60000);
    BlobParams bp = { .lowAmp = 0.15f, .lowFreq = 1.8f, .hiAmp = 0.04f, .hiFreq = 6.0f, .seed = seed,
                      .colA = { 190, 170, 230, 255 }, .colB = { 120, 100, 180, 255 } };
    MB_AddBlob(&mb, MatrixIdentity(), 14, 20, &bp);
    uint32_t h = seed * 2654435761u;
    for (int d = 0; d < 7; d++) {
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        float th = 0.3f + (float)(h & 255) / 255.0f * 2.4f;
        float ph = (float)((h >> 8) & 1023) / 1023.0f * 2.0f * PI;
        Vector3 dir = { sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph) };
        Vector3 p = Vector3Scale(dir, 0.8f);
        float r = 0.22f, len = 0.9f;
        for (int s = 0; s < 4; s++) {
            Vector3 jitter = { sinf((float)(s + d) * 2.1f) * 0.35f, cosf((float)(s * 3 + d)) * 0.3f, sinf((float)(s * 5 + d) * 1.3f) * 0.35f };
            Vector3 ndir = Vector3Normalize(Vector3Add(dir, jitter));
            Matrix m = MU_BasisAxis(ndir, p, 1.0f);
            m = MatrixMultiply(MatrixScale(r, len, r), m);
            MB_AddCylinder(&mb, m, 1.0f, 0.6f, 6, (Color){ 170, 150, 220, 255 }, (Color){ 140, 120, 200, 255 });
            p = Vector3Add(p, Vector3Scale(ndir, len));
            dir = ndir;
            r *= 0.6f;
            len *= 0.8f;
        }
        Matrix tip = MatrixMultiply(MatrixScale(0.1f, 0.1f, 0.1f), MatrixTranslate(p.x, p.y, p.z));
        MB_AddSphere(&mb, tip, 4, 6, (Color){ 200, 220, 255, 255 });
    }
    return MB_Upload(&mb);
}

Mesh MeshGen_Bacteria(void)
{
    MeshBuilder mb;
    MB_Init(&mb, 4000, 12000);
    MB_AddCapsule(&mb, MatrixMultiply(MatrixRotateX(PI * 0.5f), MatrixIdentity()), 0.45f, 0.8f, 8, (Color){ 120, 200, 110, 255 });
    for (int i = 0; i < 3; i++) {
        Matrix m = MatrixMultiply(MatrixScale(0.05f, 1.4f, 0.05f), MatrixMultiply(MatrixRotateX(-PI * 0.5f + 0.25f * (float)(i - 1)), MatrixTranslate(0, 0, -1.1f)));
        MB_AddCone(&mb, m, 5, (Color){ 140, 210, 120, 255 }, (Color){ 200, 240, 180, 255 });
    }
    return MB_Upload(&mb);
}

Mesh MeshGen_Virion(int spikes, Color body, Color spike)
{
    MeshBuilder mb;
    MB_Init(&mb, 6000, 12000);
    MB_AddIcoFlat(&mb, MatrixScale(0.7f, 0.7f, 0.7f), 1, body, (Color){ (unsigned char)(body.r * 0.75f), (unsigned char)(body.g * 0.75f), (unsigned char)(body.b * 0.75f), 255 }, 5);
    for (int i = 0; i < spikes; i++) {
        float y = 1.0f - 2.0f * ((float)i + 0.5f) / (float)spikes;
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        float a = (float)i * 2.39996f;
        Vector3 d = { r * cosf(a), y, r * sinf(a) };
        Matrix m = MU_BasisAxis(d, Vector3Scale(d, 0.6f), 1.0f);
        m = MatrixMultiply(MatrixScale(0.08f, 0.55f, 0.08f), m);
        MB_AddCylinder(&mb, m, 1.0f, 0.5f, 5, spike, spike);
        Matrix tip = MatrixMultiply(MatrixScale(0.09f, 0.09f, 0.09f), MatrixTranslate(d.x * 1.15f, d.y * 1.15f, d.z * 1.15f));
        MB_AddSphere(&mb, tip, 3, 5, spike);
    }
    return MB_Upload(&mb);
}

Mesh MeshGen_Macrophage(uint32_t seed, float detail)
{
    MeshBuilder mb;
    MB_Init(&mb, 24000, 90000);
    BlobParams bp = { .lowAmp = 0.32f, .lowFreq = 1.3f, .hiAmp = 0.09f * detail, .hiFreq = 6.0f, .ridged = 0.6f, .seed = seed,
                      .colA = { 210, 190, 160, 255 }, .colB = { 150, 120, 100, 255 } };
    MB_AddBlob(&mb, MatrixIdentity(), 22, 32, &bp);
    uint32_t h = seed * 747796405u + 1u;
    for (int i = 0; i < 9; i++) {
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        float y = ((float)(h & 1023) / 1023.0f) * 1.6f - 0.8f;
        float a = (float)((h >> 10) & 1023) / 1023.0f * 2.0f * PI;
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        Vector3 d = { r * cosf(a), y, r * sinf(a) };
        float len = 0.5f + (float)((h >> 20) & 255) / 255.0f * 0.7f;
        Matrix m = MU_BasisAxis(d, Vector3Scale(d, 0.8f), 1.0f);
        m = MatrixMultiply(MatrixScale(0.22f, len, 0.22f), m);
        MB_AddCone(&mb, m, 8, (Color){ 200, 180, 150, 255 }, (Color){ 230, 215, 190, 255 });
    }
    return MB_Upload(&mb);
}

Mesh MeshGen_Granules(uint32_t seed, int count, Color c)
{
    MeshBuilder mb;
    MB_Init(&mb, count * 60 + 100, count * 300 + 100);
    uint32_t h = seed | 1u;
    for (int i = 0; i < count; i++) {
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        Vector3 p = { ((float)(h & 255) / 255.0f - 0.5f), ((float)((h >> 8) & 255) / 255.0f - 0.5f), ((float)((h >> 16) & 255) / 255.0f - 0.5f) };
        p = Vector3Scale(p, 1.1f);
        float s = 0.08f + (float)((h >> 24) & 255) / 255.0f * 0.07f;
        MB_AddSphere(&mb, MatrixMultiply(MatrixScale(s, s, s), MatrixTranslate(p.x, p.y, p.z)), 4, 6, c);
    }
    return MB_Upload(&mb);
}

Mesh MeshGen_TCell(void)
{
    MeshBuilder mb;
    MB_Init(&mb, 20000, 50000);
    BlobParams bp = { .lowAmp = 0.06f, .lowFreq = 2.0f, .hiAmp = 0.03f, .hiFreq = 8.0f, .seed = 41,
                      .colA = { 150, 200, 235, 255 }, .colB = { 90, 140, 200, 255 } };
    MB_AddBlob(&mb, MatrixIdentity(), 16, 24, &bp);
    for (int i = 0; i < 60; i++) {
        float y = 1.0f - 2.0f * ((float)i + 0.5f) / 60.0f;
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        float a = (float)i * 2.39996f;
        Vector3 d = { r * cosf(a), y, r * sinf(a) };
        Matrix m = MU_BasisAxis(d, Vector3Scale(d, 0.95f), 1.0f);
        m = MatrixMultiply(MatrixScale(0.035f, 0.32f, 0.035f), m);
        MB_AddCylinder(&mb, m, 1.0f, 0.7f, 4, (Color){ 170, 220, 250, 255 }, (Color){ 210, 240, 255, 255 });
    }
    return MB_Upload(&mb);
}

static void AddY(MeshBuilder *mb, Matrix base, Color c)
{
    /* Immunoglobulin: Fc stem + two Fab arms at ~60 degrees */
    MB_AddCapsule(mb, MatrixMultiply(MatrixTranslate(0, -0.35f, 0), base), 0.12f, 0.3f, 4, c);
    Matrix armL = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, 0.35f, 0), MatrixRotateZ(0.55f)), MatrixTranslate(0, 0.05f, 0));
    Matrix armR = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, 0.35f, 0), MatrixRotateZ(-0.55f)), MatrixTranslate(0, 0.05f, 0));
    MB_AddCapsule(mb, MatrixMultiply(armL, base), 0.11f, 0.3f, 4, c);
    MB_AddCapsule(mb, MatrixMultiply(armR, base), 0.11f, 0.3f, 4, c);
}

Mesh MeshGen_BCell(void)
{
    MeshBuilder mb;
    MB_Init(&mb, 20000, 60000);
    BlobParams bp = { .lowAmp = 0.05f, .lowFreq = 2.0f, .hiAmp = 0.02f, .hiFreq = 9.0f, .seed = 59,
                      .colA = { 230, 200, 120, 255 }, .colB = { 200, 160, 90, 255 } };
    MB_AddBlob(&mb, MatrixIdentity(), 16, 24, &bp);
    for (int i = 0; i < 14; i++) {
        float y = 1.0f - 2.0f * ((float)i + 0.5f) / 14.0f;
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        float a = (float)i * 2.39996f;
        Vector3 d = { r * cosf(a), y, r * sinf(a) };
        Matrix m = MU_BasisAxis(d, Vector3Scale(d, 1.25f), 0.38f);
        AddY(&mb, m, (Color){ 255, 220, 140, 255 });
    }
    return MB_Upload(&mb);
}

Mesh MeshGen_Antibody(Color c)
{
    MeshBuilder mb;
    MB_Init(&mb, 2000, 6000);
    AddY(&mb, MatrixIdentity(), c);
    return MB_Upload(&mb);
}

Mesh MeshGen_Crystal(Color a, Color b)
{
    MeshBuilder mb;
    MB_Init(&mb, 2000, 2000);
    MB_AddIcoFlat(&mb, MatrixScale(0.8f, 1.4f, 0.8f), 0, a, b, 91);
    return MB_Upload(&mb);
}

Mesh MeshGen_Sphere(int rings, int slices)
{
    MeshBuilder mb;
    MB_Init(&mb, (rings + 1) * (slices + 1) + 8, rings * slices * 6 + 8);
    MB_AddSphere(&mb, MatrixIdentity(), rings, slices, WHITE);
    return MB_Upload(&mb);
}

Mesh MeshGen_Torus(float R, float r, int segR, int segr)
{
    MeshBuilder mb;
    MB_Init(&mb, (segR + 1) * (segr + 1) + 8, segR * segr * 6 + 8);
    MB_AddTorus(&mb, MatrixIdentity(), R, r, segR, segr, WHITE);
    return MB_Upload(&mb);
}

Mesh MeshGen_Cilium(void)
{
    MeshBuilder mb;
    MB_Init(&mb, 200, 400);
    MB_AddCylinder(&mb, MatrixIdentity(), 1.0f, 0.35f, 5, (Color){ 230, 210, 220, 255 }, (Color){ 255, 245, 250, 255 });
    return MB_Upload(&mb);
}

Mesh MeshGen_Quad(void)
{
    MeshBuilder mb;
    MB_Init(&mb, 4, 6);
    MB_Vertex(&mb, (Vector3){ -0.5f, -0.5f, 0 }, (Vector3){ 0, 0, 1 }, (Vector2){ 0, 0 }, WHITE);
    MB_Vertex(&mb, (Vector3){ 0.5f, -0.5f, 0 }, (Vector3){ 0, 0, 1 }, (Vector2){ 1, 0 }, WHITE);
    MB_Vertex(&mb, (Vector3){ 0.5f, 0.5f, 0 }, (Vector3){ 0, 0, 1 }, (Vector2){ 1, 1 }, WHITE);
    MB_Vertex(&mb, (Vector3){ -0.5f, 0.5f, 0 }, (Vector3){ 0, 0, 1 }, (Vector2){ 0, 1 }, WHITE);
    MB_Tri(&mb, 0, 1, 2);
    MB_Tri(&mb, 0, 2, 3);
    return MB_Upload(&mb);
}

/* The pathogen: capsid + spikes + internal organelles, rebuilt on mutation. */
Mesh MeshGen_PlayerCore(const PlayerStats *st, int stage)
{
    MeshBuilder mb;
    MB_Init(&mb, 40000, 90000);
    Vector3 hue = MU_HSV(0.38f - 0.06f * (float)stage, 0.75f, 1.0f);
    Color body = MU_V3ToColor(Vector3Scale(hue, 0.85f), 1.0f);
    Color body2 = MU_V3ToColor(Vector3Scale(hue, 0.55f), 1.0f);
    Color spikeC = MU_V3ToColor(Vector3Lerp(hue, (Vector3){ 1, 1, 1 }, 0.4f), 1.0f);
    int sub = stage >= 2 ? 2 : 1;
    MB_AddIcoFlat(&mb, MatrixScale(0.72f, 0.72f, 0.72f), sub, body, body2, 11u + (uint32_t)stage);

    /* receptor-binding spikes: base set grows with stage, mutations add more */
    int spikes = 12 + stage * 4 + st->spikes;
    for (int i = 0; i < spikes; i++) {
        float y = 1.0f - 2.0f * ((float)i + 0.5f) / (float)spikes;
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        float a = (float)i * 2.39996f;
        Vector3 d = { r * cosf(a), y, r * sinf(a) };
        bool barbed = i < st->spikes;
        float len = barbed ? 0.75f : 0.42f;
        float w = barbed ? 0.1f : 0.06f;
        Matrix m = MU_BasisAxis(d, Vector3Scale(d, 0.62f), 1.0f);
        m = MatrixMultiply(MatrixScale(w, len, w), m);
        if (barbed) MB_AddCone(&mb, m, 6, spikeC, (Color){ 255, 255, 255, 255 });
        else {
            MB_AddCylinder(&mb, m, 1.0f, 0.6f, 5, spikeC, spikeC);
            Matrix tip = MatrixMultiply(MatrixScale(0.07f, 0.07f, 0.07f), MatrixTranslate(d.x * (0.62f + len), d.y * (0.62f + len), d.z * (0.62f + len)));
            MB_AddSphere(&mb, tip, 3, 5, (Color){ 255, 255, 255, 255 });
        }
    }
    /* organelles visible through the membrane (vertex alpha 255 -> glow) */
    for (int i = 0; i < st->organelles; i++) {
        float a = (float)i * 2.1f;
        Vector3 p = { cosf(a) * 0.95f, sinf(a * 1.7f) * 0.5f, sinf(a) * 0.95f };
        Matrix m = MatrixMultiply(MatrixScale(0.22f, 0.12f, 0.12f), MatrixMultiply(MatrixRotateY(a), MatrixTranslate(p.x, p.y, p.z)));
        MB_AddSphere(&mb, m, 5, 8, (Color){ 255, 170, 90, 255 });
    }
    return MB_Upload(&mb);
}

/* Boss bodies are hand-composed from the same primitives. */
Mesh MeshGen_BossBody(int bossType)
{
    MeshBuilder mb;
    MB_Init(&mb, 60000, 200000);
    switch (bossType) {
        case BOSS_SENTINEL: {
            BlobParams bp = { .lowAmp = 0.35f, .lowFreq = 1.2f, .hiAmp = 0.1f, .hiFreq = 5.0f, .ridged = 0.7f, .seed = 501,
                              .colA = { 220, 200, 170, 255 }, .colB = { 140, 100, 90, 255 } };
            MB_AddBlob(&mb, MatrixIdentity(), 32, 48, &bp);
            for (int i = 0; i < 14; i++) {
                float y = 1.0f - 2.0f * ((float)i + 0.5f) / 14.0f;
                float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
                Vector3 d = { r * cosf((float)i * 2.4f), y, r * sinf((float)i * 2.4f) };
                Matrix m = MatrixMultiply(MatrixScale(0.2f, 0.9f, 0.2f), MU_BasisAxis(d, Vector3Scale(d, 0.85f), 1.0f));
                MB_AddCone(&mb, m, 8, (Color){ 200, 170, 150, 255 }, (Color){ 255, 230, 200, 255 });
            }
        } break;
        case BOSS_WARDEN: {
            BlobParams bp = { .lowAmp = 0.15f, .lowFreq = 1.5f, .hiAmp = 0.05f, .hiFreq = 7.0f, .seed = 502, .flatten = 0.25f,
                              .colA = { 240, 200, 210, 255 }, .colB = { 180, 120, 150, 255 } };
            MB_AddBlob(&mb, MatrixIdentity(), 28, 40, &bp);
            for (int i = 0; i < 40; i++) {
                float a = (float)i / 40.0f * 2.0f * PI;
                Vector3 d = Vector3Normalize((Vector3){ cosf(a), -0.2f + 0.2f * sinf(a * 3.0f), sinf(a) });
                Matrix m = MatrixMultiply(MatrixScale(0.05f, 0.9f, 0.05f), MU_BasisAxis(d, Vector3Scale(d, 0.95f), 1.0f));
                MB_AddCylinder(&mb, m, 1.0f, 0.3f, 4, (Color){ 250, 230, 240, 255 }, (Color){ 255, 255, 255, 255 });
            }
            MB_AddTorus(&mb, MatrixTranslate(0, -0.55f, 0), 0.55f, 0.12f, 24, 8, (Color){ 120, 40, 70, 255 });
        } break;
        case BOSS_LEVIATHAN: {
            BlobParams bp = { .lowAmp = 0.12f, .lowFreq = 2.0f, .hiAmp = 0.06f, .hiFreq = 6.0f, .ridged = 0.4f, .seed = 503,
                              .colA = { 200, 160, 90, 255 }, .colB = { 120, 80, 40, 255 } };
            MB_AddBlob(&mb, MatrixScale(1.0f, 0.85f, 1.25f), 24, 32, &bp);
            for (int i = 0; i < 6; i++) {
                float a = (float)i / 6.0f * 2.0f * PI;
                Vector3 base = { cosf(a) * 0.55f, sinf(a) * 0.5f, 1.05f };
                Vector3 d = Vector3Normalize((Vector3){ -cosf(a) * 0.4f, -sinf(a) * 0.4f, 1.0f });
                Matrix m = MatrixMultiply(MatrixScale(0.12f, 0.8f, 0.12f), MU_BasisAxis(d, base, 1.0f));
                MB_AddCone(&mb, m, 6, (Color){ 240, 220, 180, 255 }, (Color){ 255, 255, 230, 255 });
            }
        } break;
        case BOSS_CITADEL: {
            MB_AddIcoFlat(&mb, MatrixIdentity(), 2, (Color){ 200, 220, 160, 255 }, (Color){ 140, 170, 110, 255 }, 504);
            for (int i = 0; i < 20; i++) {
                float y = 1.0f - 2.0f * ((float)i + 0.5f) / 20.0f;
                float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
                Vector3 d = { r * cosf((float)i * 2.4f), y, r * sinf((float)i * 2.4f) };
                Matrix m = MatrixMultiply(MatrixScale(0.14f, 0.7f, 0.14f), MU_BasisAxis(d, Vector3Scale(d, 0.9f), 1.0f));
                MB_AddCone(&mb, m, 4, (Color){ 210, 230, 170, 255 }, (Color){ 255, 255, 220, 255 });
            }
        } break;
        default: {
            BlobParams bp = { .lowAmp = 0.12f, .lowFreq = 1.4f, .hiAmp = 0.16f, .hiFreq = 4.0f, .ridged = 1.0f, .seed = 505,
                              .colA = { 220, 190, 240, 255 }, .colB = { 110, 70, 150, 255 } };
            MB_AddBlob(&mb, MatrixIdentity(), 40, 56, &bp);
            for (int d = 0; d < 10; d++) {
                float a = (float)d / 10.0f * 2.0f * PI;
                Vector3 dir = Vector3Normalize((Vector3){ cosf(a), -0.6f, sinf(a) });
                Vector3 p = Vector3Scale(dir, 0.8f);
                float r = 0.12f, len = 0.6f;
                for (int s = 0; s < 4; s++) {
                    Vector3 nd = Vector3Normalize(Vector3Add(dir, (Vector3){ sinf((float)(d + s)) * 0.3f, -0.2f, cosf((float)(d * s)) * 0.3f }));
                    Matrix m = MatrixMultiply(MatrixScale(r, len, r), MU_BasisAxis(nd, p, 1.0f));
                    MB_AddCylinder(&mb, m, 1.0f, 0.7f, 6, (Color){ 180, 150, 220, 255 }, (Color){ 150, 120, 200, 255 });
                    p = Vector3Add(p, Vector3Scale(nd, len));
                    dir = nd; r *= 0.7f; len *= 0.85f;
                }
            }
        } break;
    }
    return MB_Upload(&mb);
}

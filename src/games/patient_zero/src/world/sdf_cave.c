/*
 * sdf_cave.c - field storage, stamping, surface nets and queries.
 *
 * Why surface nets instead of marching cubes: one vertex per surface cell
 * gives smooth, organic walls with shared vertices (good normals), needs no
 * 256-entry tables, and maps naturally onto 16-bit index chunks.
 */
#include "world/sdf_cave.h"
#include "gfx/shaders.h"
#include "core/noise.h"
#include "core/rng.h"
#include <string.h>

#define NXN (GRID_NX + 1)
#define NYN (GRID_NY + 1)
#define NZN (GRID_NZ + 1)
#define NODE(i, j, k) (((k) * NYN + (j)) * NXN + (i))
#define CELLI(i, j, k) (((k) * GRID_NY + (j)) * GRID_NX + (i))
#define MAX_CAVE_MESHES 400

static float *sGrid = NULL;
static int *sCellVert = NULL;
static Vector3 *sSurfPos = NULL, *sSurfNrm = NULL;
static Color *sSurfCol = NULL;
static int sSurfCount = 0;
static int *sRemap = NULL;

static Mesh sMeshes[MAX_CAVE_MESHES];
static BoundingBox sMeshBB[MAX_CAVE_MESHES];
static int sMeshCount = 0;
static int sTriCount = 0;
static int sDrawn = 0;
static Material sMat;
static bool sMatReady = false;
static CaveLook sLook;

/* chunk builder scratch (module-lifetime, no per-frame allocation) */
#define CB_MAXV 65000
#define CB_MAXI 240000
static float *sCbPos, *sCbNrm, *sCbUv;
static unsigned char *sCbCol;
static unsigned short *sCbIdx;
static int sCbV = 0, sCbI = 0;
static int *sTouched;
static int sTouchedCount = 0;

bool Cave_Init(void)
{
    sGrid = (float *)MemAlloc((unsigned int)(sizeof(float) * NXN * NYN * NZN));
    sCellVert = (int *)MemAlloc((unsigned int)(sizeof(int) * GRID_NX * GRID_NY * GRID_NZ));
    sSurfPos = (Vector3 *)MemAlloc((unsigned int)(sizeof(Vector3) * MAX_SURF_VERTS));
    sSurfNrm = (Vector3 *)MemAlloc((unsigned int)(sizeof(Vector3) * MAX_SURF_VERTS));
    sSurfCol = (Color *)MemAlloc((unsigned int)(sizeof(Color) * MAX_SURF_VERTS));
    sRemap = (int *)MemAlloc((unsigned int)(sizeof(int) * MAX_SURF_VERTS));
    sTouched = (int *)MemAlloc((unsigned int)(sizeof(int) * CB_MAXV));
    sCbPos = (float *)MemAlloc(sizeof(float) * 3 * CB_MAXV);
    sCbNrm = (float *)MemAlloc(sizeof(float) * 3 * CB_MAXV);
    sCbUv = (float *)MemAlloc(sizeof(float) * 2 * CB_MAXV);
    sCbCol = (unsigned char *)MemAlloc(4 * CB_MAXV);
    sCbIdx = (unsigned short *)MemAlloc(sizeof(unsigned short) * CB_MAXI);
    if (!sGrid || !sCellVert || !sSurfPos || !sSurfNrm || !sSurfCol || !sRemap || !sTouched || !sCbPos || !sCbNrm || !sCbUv || !sCbCol || !sCbIdx) return false;
    for (int i = 0; i < MAX_SURF_VERTS; i++) sRemap[i] = -1;
    Cave_Clear();
    return true;
}

void Cave_Shutdown(void)
{
    Cave_FreeMesh();
    if (sMatReady) {
        MemFree(sMat.maps);   /* shader is owned by the shaders module */
        sMat.maps = NULL;
        sMatReady = false;
    }
    MemFree(sGrid); MemFree(sCellVert); MemFree(sSurfPos); MemFree(sSurfNrm); MemFree(sSurfCol); MemFree(sRemap); MemFree(sTouched);
    MemFree(sCbPos); MemFree(sCbNrm); MemFree(sCbUv); MemFree(sCbCol); MemFree(sCbIdx);
    sGrid = NULL;
}

void Cave_Clear(void)
{
    int n = NXN * NYN * NZN;
    for (int i = 0; i < n; i++) sGrid[i] = -12.0f;
}

static inline Vector3 NodePos(int i, int j, int k)
{
    return (Vector3){ WORLD_MIN_X + (float)i * GRID_CELL, WORLD_MIN_Y + (float)j * GRID_CELL, WORLD_MIN_Z + (float)k * GRID_CELL };
}

static void Range(float lo, float hi, float origin, int maxN, int *a, int *b)
{
    *a = (int)floorf((lo - origin) / GRID_CELL);
    *b = (int)ceilf((hi - origin) / GRID_CELL);
    if (*a < 0) *a = 0;
    if (*b > maxN - 1) *b = maxN - 1;
}

void Cave_StampCapsule(Vector3 a, Vector3 b, float r0, float r1)
{
    float rm = fmaxf(r0, r1) + 6.0f;
    int i0, i1, j0, j1, k0, k1;
    Range(fminf(a.x, b.x) - rm, fmaxf(a.x, b.x) + rm, WORLD_MIN_X, NXN, &i0, &i1);
    Range(fminf(a.y, b.y) - rm, fmaxf(a.y, b.y) + rm, WORLD_MIN_Y, NYN, &j0, &j1);
    Range(fminf(a.z, b.z) - rm, fmaxf(a.z, b.z) + rm, WORLD_MIN_Z, NZN, &k0, &k1);
    Vector3 ab = Vector3Subtract(b, a);
    float len2 = fmaxf(Vector3LengthSqr(ab), 1e-6f);
    for (int k = k0; k <= k1; k++) for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        Vector3 p = NodePos(i, j, k);
        float t = Sat(Vector3DotProduct(Vector3Subtract(p, a), ab) / len2);
        Vector3 c = Vector3Add(a, Vector3Scale(ab, t));
        float r = r0 + (r1 - r0) * t;
        float v = r - Vector3Distance(p, c);
        float *g = &sGrid[NODE(i, j, k)];
        /* smooth union so junctions blend into organic fillets */
        float h = Sat(0.5f + 0.5f * (v - *g) / 4.0f);
        *g = Lerp(*g, v, h) + 4.0f * h * (1.0f - h);
    }
}

void Cave_StampSphere(Vector3 c, float r) { Cave_StampCapsule(c, c, r, r); }

void Cave_StampSolid(Vector3 c, float r)
{
    float rm = r + 4.0f;
    int i0, i1, j0, j1, k0, k1;
    Range(c.x - rm, c.x + rm, WORLD_MIN_X, NXN, &i0, &i1);
    Range(c.y - rm, c.y + rm, WORLD_MIN_Y, NYN, &j0, &j1);
    Range(c.z - rm, c.z + rm, WORLD_MIN_Z, NZN, &k0, &k1);
    for (int k = k0; k <= k1; k++) for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        float d = Vector3Distance(NodePos(i, j, k), c) - r;    /* >0 outside blob */
        float *g = &sGrid[NODE(i, j, k)];
        float h = Sat(0.5f - 0.5f * (d - *g) / 2.0f);
        *g = Lerp(*g, d, h) - 2.0f * h * (1.0f - h);
    }
}

void Cave_ApplyNoise(float amp, float freq, float ridged, float band)
{
    for (int k = 0; k < NZN; k++) for (int j = 0; j < NYN; j++) for (int i = 0; i < NXN; i++) {
        float *g = &sGrid[NODE(i, j, k)];
        if (fabsf(*g) > band) continue;
        Vector3 p = NodePos(i, j, k);
        float n = Noise_Fbm3(p.x * freq, p.y * freq, p.z * freq, 3);
        if (ridged > 0.0f) n = Lerp(n, Noise_Ridged3(p.x * freq * 1.7f, p.y * freq * 1.7f, p.z * freq * 1.7f, 3) * 2.0f - 1.0f, ridged);
        *g += n * amp;
    }
}

void Cave_SealBorders(void)
{
    for (int k = 0; k < NZN; k++) for (int j = 0; j < NYN; j++) for (int i = 0; i < NXN; i++) {
        int e = 2;
        if (i < e || j < e || k < e || i > GRID_NX - e || j > GRID_NY - e || k > GRID_NZ - e) {
            float *g = &sGrid[NODE(i, j, k)];
            if (*g > -3.0f) *g = -3.0f;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Queries                                                                    */
/* ------------------------------------------------------------------------- */

float Cave_SampleGrad(Vector3 p, Vector3 *grad)
{
    float gx = (p.x - WORLD_MIN_X) / GRID_CELL, gy = (p.y - WORLD_MIN_Y) / GRID_CELL, gz = (p.z - WORLD_MIN_Z) / GRID_CELL;
    if (gx < 0.0f || gy < 0.0f || gz < 0.0f || gx >= (float)GRID_NX || gy >= (float)GRID_NY || gz >= (float)GRID_NZ) {
        if (grad) {
            Vector3 c = { -p.x, -p.y, -p.z };
            *grad = Vector3Normalize(c);
        }
        return -10.0f;
    }
    int i = (int)gx, j = (int)gy, k = (int)gz;
    float fx = gx - (float)i, fy = gy - (float)j, fz = gz - (float)k;
    float c000 = sGrid[NODE(i, j, k)], c100 = sGrid[NODE(i + 1, j, k)];
    float c010 = sGrid[NODE(i, j + 1, k)], c110 = sGrid[NODE(i + 1, j + 1, k)];
    float c001 = sGrid[NODE(i, j, k + 1)], c101 = sGrid[NODE(i + 1, j, k + 1)];
    float c011 = sGrid[NODE(i, j + 1, k + 1)], c111 = sGrid[NODE(i + 1, j + 1, k + 1)];
    float x00 = c000 + (c100 - c000) * fx, x10 = c010 + (c110 - c010) * fx;
    float x01 = c001 + (c101 - c001) * fx, x11 = c011 + (c111 - c011) * fx;
    float y0 = x00 + (x10 - x00) * fy, y1 = x01 + (x11 - x01) * fy;
    float v = y0 + (y1 - y0) * fz;
    if (grad) {
        float dx = ((c100 - c000) * (1 - fy) * (1 - fz) + (c110 - c010) * fy * (1 - fz) + (c101 - c001) * (1 - fy) * fz + (c111 - c011) * fy * fz);
        float dy = ((c010 - c000) * (1 - fx) * (1 - fz) + (c110 - c100) * fx * (1 - fz) + (c011 - c001) * (1 - fx) * fz + (c111 - c101) * fx * fz);
        float dz = ((c001 - c000) * (1 - fx) * (1 - fy) + (c101 - c100) * fx * (1 - fy) + (c011 - c010) * (1 - fx) * fy + (c111 - c110) * fx * fy);
        Vector3 g = { dx, dy, dz };
        float l = Vector3Length(g);
        *grad = (l > 1e-6f) ? Vector3Scale(g, 1.0f / l) : (Vector3){ 0, 1, 0 };
    }
    return v;
}

float Cave_Sample(Vector3 p) { return Cave_SampleGrad(p, NULL); }

Vector3 Cave_Gradient(Vector3 p)
{
    Vector3 g;
    Cave_SampleGrad(p, &g);
    return g;
}

bool Cave_Raycast(Vector3 from, Vector3 dir, float maxDist, float margin, float *hit)
{
    Vector3 d = Vector3Normalize(dir);
    float t = 0.0f;
    for (int it = 0; it < 160 && t < maxDist; it++) {
        float o = Cave_Sample(Vector3Add(from, Vector3Scale(d, t)));
        if (o < margin) { *hit = t; return true; }
        t += Clamp((o - margin) * 0.7f, 0.35f, 4.0f);
    }
    return false;
}

bool Cave_LineOfSight(Vector3 a, Vector3 b)
{
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    float hit;
    if (len < 0.01f) return true;
    return !Cave_Raycast(a, d, len, 0.0f, &hit);
}

bool Cave_Collide(Vector3 *pos, Vector3 *vel, float radius, float bounce)
{
    Vector3 g;
    float o = Cave_SampleGrad(*pos, &g);
    if (o >= radius) return false;
    *pos = Vector3Add(*pos, Vector3Scale(g, radius - o));
    if (vel) {
        float vn = Vector3DotProduct(*vel, g);
        if (vn < 0.0f) *vel = Vector3Subtract(*vel, Vector3Scale(g, vn * (1.0f + bounce)));
    }
    return true;
}

bool Cave_RandomOpenPoint(Rng *r, float minOpen, Vector3 center, float range, Vector3 *out)
{
    for (int tries = 0; tries < 300; tries++) {
        Vector3 p;
        if (range > 0.0f) p = Vector3Add(center, Vector3Scale(Rng_InSphere(r), range));
        else p = (Vector3){ Rng_Range(r, WORLD_MIN_X + 6, -WORLD_MIN_X - 6), Rng_Range(r, WORLD_MIN_Y + 6, -WORLD_MIN_Y - 6), Rng_Range(r, WORLD_MIN_Z + 6, -WORLD_MIN_Z - 6) };
        if (Cave_Sample(p) > minOpen) { *out = p; return true; }
    }
    return false;
}

int Cave_SurfaceCount(void) { return sSurfCount; }
Vector3 Cave_SurfacePos(int i) { return sSurfPos[i]; }
Vector3 Cave_SurfaceNormal(int i) { return sSurfNrm[i]; }
int Cave_TriangleCount(void) { return sTriCount; }
int Cave_MeshCount(void) { return sMeshCount; }
int Cave_DrawnCount(void) { return sDrawn; }

/* ------------------------------------------------------------------------- */
/* Surface nets                                                               */
/* ------------------------------------------------------------------------- */

static const int EDGE_A[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3 };
static const int EDGE_B[12] = { 1, 3, 0, 2, 5, 7, 4, 6, 4, 5, 6, 7 };
/* corner c: bit0 = x, bit1 = y, bit2 = z */
static const int EDGE_C[12][2] = { { 0, 1 }, { 1, 3 }, { 2, 0 }, { 3, 2 }, { 4, 5 }, { 5, 7 }, { 6, 4 }, { 7, 6 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };

static Color SurfColor(Vector3 p, Vector3 n)
{
    float lo = Noise_Fbm3(p.x * 0.03f, p.y * 0.03f, p.z * 0.03f, 2) * 0.5f + 0.5f;
    float hi = Noise_Value3(p.x * 0.21f, p.y * 0.21f, p.z * 0.21f) * 0.5f + 0.5f;
    float t = Sat(lo * 0.8f + hi * 0.25f);
    Color c = sLook.base;
    c.r = (unsigned char)Lerp(sLook.base.r, sLook.accent.r, t);
    c.g = (unsigned char)Lerp(sLook.base.g, sLook.accent.g, t);
    c.b = (unsigned char)Lerp(sLook.base.b, sLook.accent.b, t);
    /* ambient occlusion: how open is the space in front of the wall? */
    float open = Cave_Sample(Vector3Add(p, Vector3Scale(n, 5.0f)));
    float ao = Clamp(open / 5.5f, 0.3f, 1.0f);
    c.a = (unsigned char)(ao * 255.0f);
    return c;
}

static void FlushChunk(void)
{
    if (sCbI == 0 || sMeshCount >= MAX_CAVE_MESHES) { sCbV = 0; sCbI = 0; return; }
    Mesh m = { 0 };
    m.vertexCount = sCbV;
    m.triangleCount = sCbI / 3;
    m.vertices = (float *)MemAlloc((unsigned int)(sCbV * 3 * sizeof(float)));
    m.normals = (float *)MemAlloc((unsigned int)(sCbV * 3 * sizeof(float)));
    m.texcoords = (float *)MemAlloc((unsigned int)(sCbV * 2 * sizeof(float)));
    m.colors = (unsigned char *)MemAlloc((unsigned int)(sCbV * 4));
    m.indices = (unsigned short *)MemAlloc((unsigned int)(sCbI * sizeof(unsigned short)));
    memcpy(m.vertices, sCbPos, (size_t)sCbV * 3 * sizeof(float));
    memcpy(m.normals, sCbNrm, (size_t)sCbV * 3 * sizeof(float));
    memcpy(m.texcoords, sCbUv, (size_t)sCbV * 2 * sizeof(float));
    memcpy(m.colors, sCbCol, (size_t)sCbV * 4);
    memcpy(m.indices, sCbIdx, (size_t)sCbI * sizeof(unsigned short));
    UploadMesh(&m, false);
    /* bounds */
    Vector3 mn = { 1e9f, 1e9f, 1e9f }, mx = { -1e9f, -1e9f, -1e9f };
    for (int i = 0; i < sCbV; i++) {
        Vector3 p = { sCbPos[i * 3], sCbPos[i * 3 + 1], sCbPos[i * 3 + 2] };
        mn = Vector3Min(mn, p); mx = Vector3Max(mx, p);
    }
    sMeshBB[sMeshCount] = (BoundingBox){ mn, mx };
    sMeshes[sMeshCount++] = m;
    sTriCount += sCbI / 3;
    for (int i = 0; i < sTouchedCount; i++) sRemap[sTouched[i]] = -1;
    sTouchedCount = 0;
    sCbV = 0;
    sCbI = 0;
}

static int LocalVert(int g)
{
    if (sRemap[g] >= 0) return sRemap[g];
    int l = sCbV++;
    Vector3 p = sSurfPos[g], n = sSurfNrm[g];
    sCbPos[l * 3] = p.x; sCbPos[l * 3 + 1] = p.y; sCbPos[l * 3 + 2] = p.z;
    sCbNrm[l * 3] = n.x; sCbNrm[l * 3 + 1] = n.y; sCbNrm[l * 3 + 2] = n.z;
    sCbUv[l * 2] = p.x * 0.05f; sCbUv[l * 2 + 1] = p.z * 0.05f;
    Color c = sSurfCol[g];
    sCbCol[l * 4] = c.r; sCbCol[l * 4 + 1] = c.g; sCbCol[l * 4 + 2] = c.b; sCbCol[l * 4 + 3] = c.a;
    sRemap[g] = l;
    sTouched[sTouchedCount++] = g;
    return l;
}

static void EmitQuad(int v0, int v1, int v2, int v3)
{
    if (sCbV + 4 >= CB_MAXV || sCbI + 6 >= CB_MAXI) FlushChunk();
    int a = LocalVert(v0), b = LocalVert(v1), c = LocalVert(v2), d = LocalVert(v3);
    Vector3 pa = sSurfPos[v0], pb = sSurfPos[v1], pc = sSurfPos[v2];
    Vector3 nrm = Vector3CrossProduct(Vector3Subtract(pb, pa), Vector3Subtract(pc, pa));
    Vector3 avg = Vector3Add(Vector3Add(sSurfNrm[v0], sSurfNrm[v1]), Vector3Add(sSurfNrm[v2], sSurfNrm[v3]));
    if (Vector3DotProduct(nrm, avg) >= 0.0f) {
        sCbIdx[sCbI++] = (unsigned short)a; sCbIdx[sCbI++] = (unsigned short)b; sCbIdx[sCbI++] = (unsigned short)c;
        sCbIdx[sCbI++] = (unsigned short)a; sCbIdx[sCbI++] = (unsigned short)c; sCbIdx[sCbI++] = (unsigned short)d;
    } else {
        sCbIdx[sCbI++] = (unsigned short)a; sCbIdx[sCbI++] = (unsigned short)c; sCbIdx[sCbI++] = (unsigned short)b;
        sCbIdx[sCbI++] = (unsigned short)a; sCbIdx[sCbI++] = (unsigned short)d; sCbIdx[sCbI++] = (unsigned short)c;
    }
}

void Cave_FreeMesh(void)
{
    for (int i = 0; i < sMeshCount; i++) UnloadMesh(sMeshes[i]);
    sMeshCount = 0;
    sTriCount = 0;
}

void Cave_SetLook(const CaveLook *look) { sLook = *look; }

void Cave_BuildMesh(const CaveLook *look)
{
    sLook = *look;
    Cave_FreeMesh();
    sSurfCount = 0;

    /* pass 1: one vertex per sign-changing cell */
    for (int k = 0; k < GRID_NZ; k++) for (int j = 0; j < GRID_NY; j++) for (int i = 0; i < GRID_NX; i++) {
        float v[8];
        int mask = 0;
        for (int c = 0; c < 8; c++) {
            v[c] = sGrid[NODE(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1))];
            if (v[c] > 0.0f) mask |= 1 << c;
        }
        int ci = CELLI(i, j, k);
        if (mask == 0 || mask == 255 || sSurfCount >= MAX_SURF_VERTS) { sCellVert[ci] = -1; continue; }
        Vector3 acc = { 0 };
        int n = 0;
        for (int e = 0; e < 12; e++) {
            int a = EDGE_C[e][0], b = EDGE_C[e][1];
            if ((v[a] > 0.0f) == (v[b] > 0.0f)) continue;
            float t = v[a] / (v[a] - v[b]);
            Vector3 pa = { (float)(a & 1), (float)((a >> 1) & 1), (float)((a >> 2) & 1) };
            Vector3 pb = { (float)(b & 1), (float)((b >> 1) & 1), (float)((b >> 2) & 1) };
            acc = Vector3Add(acc, Vector3Lerp(pa, pb, t));
            n++;
        }
        acc = Vector3Scale(acc, 1.0f / (float)n);
        Vector3 p = { WORLD_MIN_X + ((float)i + acc.x) * GRID_CELL, WORLD_MIN_Y + ((float)j + acc.y) * GRID_CELL, WORLD_MIN_Z + ((float)k + acc.z) * GRID_CELL };
        Vector3 g;
        Cave_SampleGrad(p, &g);
        sSurfPos[sSurfCount] = p;
        sSurfNrm[sSurfCount] = g;          /* points into the open fluid */
        sSurfCol[sSurfCount] = SurfColor(p, g);
        sCellVert[ci] = sSurfCount++;
    }
    (void)EDGE_A; (void)EDGE_B;

    /* pass 2: quads per sign-changing grid edge, grouped into spatial chunks */
    int cx = (GRID_NX + CHUNK_CELLS - 1) / CHUNK_CELLS, cy = (GRID_NY + CHUNK_CELLS - 1) / CHUNK_CELLS, cz = (GRID_NZ + CHUNK_CELLS - 1) / CHUNK_CELLS;
    for (int ck = 0; ck < cz; ck++) for (int cj = 0; cj < cy; cj++) for (int ci = 0; ci < cx; ci++) {
        int i0 = ci * CHUNK_CELLS, i1 = (ci + 1) * CHUNK_CELLS; if (i1 > GRID_NX) i1 = GRID_NX;
        int j0 = cj * CHUNK_CELLS, j1 = (cj + 1) * CHUNK_CELLS; if (j1 > GRID_NY) j1 = GRID_NY;
        int k0 = ck * CHUNK_CELLS, k1 = (ck + 1) * CHUNK_CELLS; if (k1 > GRID_NZ) k1 = GRID_NZ;
        for (int k = k0; k < k1; k++) for (int j = j0; j < j1; j++) for (int i = i0; i < i1; i++) {
            float v0 = sGrid[NODE(i, j, k)];
            /* x edge */
            if (j > 0 && k > 0) {
                float v1 = sGrid[NODE(i + 1, j, k)];
                if ((v0 > 0) != (v1 > 0)) {
                    int a = sCellVert[CELLI(i, j - 1, k - 1)], b = sCellVert[CELLI(i, j, k - 1)], c = sCellVert[CELLI(i, j, k)], d = sCellVert[CELLI(i, j - 1, k)];
                    if (a >= 0 && b >= 0 && c >= 0 && d >= 0) EmitQuad(a, b, c, d);
                }
            }
            /* y edge */
            if (i > 0 && k > 0) {
                float v1 = sGrid[NODE(i, j + 1, k)];
                if ((v0 > 0) != (v1 > 0)) {
                    int a = sCellVert[CELLI(i - 1, j, k - 1)], b = sCellVert[CELLI(i, j, k - 1)], c = sCellVert[CELLI(i, j, k)], d = sCellVert[CELLI(i - 1, j, k)];
                    if (a >= 0 && b >= 0 && c >= 0 && d >= 0) EmitQuad(a, b, c, d);
                }
            }
            /* z edge */
            if (i > 0 && j > 0) {
                float v1 = sGrid[NODE(i, j, k + 1)];
                if ((v0 > 0) != (v1 > 0)) {
                    int a = sCellVert[CELLI(i - 1, j - 1, k)], b = sCellVert[CELLI(i, j - 1, k)], c = sCellVert[CELLI(i, j, k)], d = sCellVert[CELLI(i - 1, j, k)];
                    if (a >= 0 && b >= 0 && c >= 0 && d >= 0) EmitQuad(a, b, c, d);
                }
            }
        }
        FlushChunk();
    }

    if (!sMatReady) {
        sMat = LoadMaterialDefault();
        sMatReady = true;
    }
    sMat.shader = Shaders_Get(SHD_CAVE);
}

void Cave_Draw(Camera3D cam, float heart)
{
    if (!sMatReady) return;
    Shader s = Shaders_Get(SHD_CAVE);
    const ShaderLocs *L = Shaders_Locs(SHD_CAVE);
    SetShaderValue(s, L->detailColor, &sLook.detail, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->veinColor, &sLook.vein, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->veinGlow, &sLook.veinGlow, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->causticColor, &sLook.caustic, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, L->wallPulse, &sLook.wallPulse, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->heart, &heart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->spec, &sLook.spec, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->rimStrength, &sLook.rimStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, L->rimColor, &sLook.rim, SHADER_UNIFORM_VEC3);
    sMat.shader = s;
    sMat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    sDrawn = 0;
    for (int i = 0; i < sMeshCount; i++) {
        BoundingBox bb = sMeshBB[i];
        Vector3 c = Vector3Scale(Vector3Add(bb.min, bb.max), 0.5f);
        float r = Vector3Distance(bb.min, bb.max) * 0.5f;
        Vector3 to = Vector3Subtract(c, cam.position);
        float d = Vector3Length(to);
        if (d - r > CAVE_DRAW_DIST) continue;
        if (d > r && Vector3DotProduct(to, fwd) < -r) continue;
        DrawMesh(sMeshes[i], sMat, MatrixIdentity());
        sDrawn++;
    }
    G.debug.drawCalls += sDrawn;
}

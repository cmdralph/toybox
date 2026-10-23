/*
 * spatial_hash.c - counting-sort uniform grid.
 */
#include "systems/spatial_hash.h"

#define HCELLS (HASH_NX * HASH_NY * HASH_NZ)

static int sStart[HCELLS + 1];
static int sItems[MAX_AGENTS];
static int sCellOf[MAX_AGENTS];

static inline int CellCoord(float v, float mn, int n)
{
    int c = (int)((v - mn) / HASH_CELL);
    return c < 0 ? 0 : (c >= n ? n - 1 : c);
}

void SH_Build(void)
{
    for (int i = 0; i <= HCELLS; i++) sStart[i] = 0;
    int high = G.agentHigh;
    for (int i = 0; i < high; i++) {
        Agent *a = &G.agents[i];
        if (!a->alive) { sCellOf[i] = -1; continue; }
        int cx = CellCoord(a->pos.x, WORLD_MIN_X, HASH_NX), cy = CellCoord(a->pos.y, WORLD_MIN_Y, HASH_NY), cz = CellCoord(a->pos.z, WORLD_MIN_Z, HASH_NZ);
        int c = (cz * HASH_NY + cy) * HASH_NX + cx;
        sCellOf[i] = c;
        sStart[c + 1]++;
    }
    for (int i = 0; i < HCELLS; i++) sStart[i + 1] += sStart[i];
    static int fill[HCELLS];
    for (int i = 0; i < HCELLS; i++) fill[i] = sStart[i];
    for (int i = 0; i < high; i++) if (sCellOf[i] >= 0) sItems[fill[sCellOf[i]]++] = i;
}

int SH_Query(Vector3 c, float r, int *out, int max, uint32_t mask)
{
    int x0 = CellCoord(c.x - r - 3.0f, WORLD_MIN_X, HASH_NX), x1 = CellCoord(c.x + r + 3.0f, WORLD_MIN_X, HASH_NX);
    int y0 = CellCoord(c.y - r - 3.0f, WORLD_MIN_Y, HASH_NY), y1 = CellCoord(c.y + r + 3.0f, WORLD_MIN_Y, HASH_NY);
    int z0 = CellCoord(c.z - r - 3.0f, WORLD_MIN_Z, HASH_NZ), z1 = CellCoord(c.z + r + 3.0f, WORLD_MIN_Z, HASH_NZ);
    int n = 0;
    for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        int cell = (z * HASH_NY + y) * HASH_NX + x;
        for (int k = sStart[cell]; k < sStart[cell + 1]; k++) {
            int i = sItems[k];
            Agent *a = &G.agents[i];
            if (!a->alive || !(mask & KMASK(a->kind))) continue;
            float rr = r + a->radius;
            if (Vector3DistanceSqr(a->pos, c) > rr * rr) continue;
            if (n < max) out[n++] = i;
            else return n;
        }
    }
    return n;
}

int SH_Nearest(Vector3 c, float r, uint32_t mask, int exclude)
{
    int ids[256];
    int n = SH_Query(c, r, ids, 256, mask);
    int best = -1;
    float bd = 1e18f;
    for (int k = 0; k < n; k++) {
        if (ids[k] == exclude) continue;
        float d = Vector3DistanceSqr(G.agents[ids[k]].pos, c);
        if (d < bd) { bd = d; best = ids[k]; }
    }
    return best;
}

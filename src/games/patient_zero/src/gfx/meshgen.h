/*
 * meshgen.h - procedural mesh construction.
 *
 * A MeshBuilder accumulates vertices (position, normal, uv, colour) and
 * 16-bit indices, then uploads a raylib Mesh. Primitive helpers append
 * transformed shapes so complex organisms are composed from parts.
 */
#ifndef PZ_MESHGEN_H
#define PZ_MESHGEN_H
#include "core/game.h"

typedef struct {
    float *pos, *nrm, *uv;
    unsigned char *col;
    unsigned short *idx;
    int vcount, icount, vcap, icap;
    bool overflow;
} MeshBuilder;

typedef struct {
    float lowAmp, lowFreq, hiAmp, hiFreq;   /* radial displacement fBm      */
    float ridged;                             /* 0..1 mix of ridged noise     */
    uint32_t seed;
    Color colA, colB;                         /* colour by displacement       */
    float flatten;                            /* squash along Y (0 = sphere)  */
    unsigned char alpha;                      /* vertex alpha (emissive mask) */
} BlobParams;

void MB_Init(MeshBuilder *mb, int vcap, int icap);
void MB_Free(MeshBuilder *mb);
int  MB_Vertex(MeshBuilder *mb, Vector3 p, Vector3 n, Vector2 uv, Color c);
void MB_Tri(MeshBuilder *mb, int a, int b, int c);
Mesh MB_Upload(MeshBuilder *mb);        /* uploads and frees the builder     */

void MB_AddBlob(MeshBuilder *mb, Matrix xf, int rings, int slices, const BlobParams *bp);
void MB_AddSphere(MeshBuilder *mb, Matrix xf, int rings, int slices, Color c);
void MB_AddCone(MeshBuilder *mb, Matrix xf, int slices, Color base, Color tip);
void MB_AddCylinder(MeshBuilder *mb, Matrix xf, float r0, float r1, int slices, Color c0, Color c1);
void MB_AddCapsule(MeshBuilder *mb, Matrix xf, float radius, float halfLen, int segs, Color c);
void MB_AddTorus(MeshBuilder *mb, Matrix xf, float R, float r, int segR, int segr, Color c);
void MB_AddIcoFlat(MeshBuilder *mb, Matrix xf, int subdiv, Color a, Color b, uint32_t seed);
void MB_AddLathe(MeshBuilder *mb, Matrix xf, const Vector2 *profile, int count, int slices, Color inner, Color outer);

/* Ready-made organisms */
Mesh MeshGen_RBC(void);
Mesh MeshGen_Platelet(void);
Mesh MeshGen_Blob(const BlobParams *bp, int rings, int slices);
Mesh MeshGen_Nucleus(int lobes, uint32_t seed, Color c);
Mesh MeshGen_Neuron(uint32_t seed);
Mesh MeshGen_Bacteria(void);
Mesh MeshGen_Virion(int spikes, Color body, Color spike);
Mesh MeshGen_Macrophage(uint32_t seed, float detail);
Mesh MeshGen_Granules(uint32_t seed, int count, Color c);
Mesh MeshGen_TCell(void);
Mesh MeshGen_BCell(void);
Mesh MeshGen_Antibody(Color c);
Mesh MeshGen_Crystal(Color a, Color b);
Mesh MeshGen_Sphere(int rings, int slices);
Mesh MeshGen_Torus(float R, float r, int segR, int segr);
Mesh MeshGen_Cilium(void);
Mesh MeshGen_Quad(void);
Mesh MeshGen_PlayerCore(const PlayerStats *st, int stage);
Mesh MeshGen_BossBody(int bossType);

#endif

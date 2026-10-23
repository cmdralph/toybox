/*
 * sdf_cave.h - the volumetric tissue cavern.
 *
 * Regions are authored as a scalar "openness" field on a 161x65x161 grid:
 * positive = fluid you can swim in, negative = tissue. Tubes and chambers
 * are stamped in, noise roughens the walls, a naive surface-nets mesher
 * extracts chunked meshes, and gameplay queries (collision, line of sight,
 * camera clipping) sample the same field with trilinear interpolation, so
 * what you see is exactly what you collide with.
 */
#ifndef PZ_SDF_CAVE_H
#define PZ_SDF_CAVE_H
#include "core/game.h"

typedef struct {
    Color base, accent;
    Vector3 detail, vein, caustic, rim;
    float veinGlow, wallPulse, spec, rimStrength;
} CaveLook;

bool  Cave_Init(void);
void  Cave_Shutdown(void);
void  Cave_Clear(void);
void  Cave_StampCapsule(Vector3 a, Vector3 b, float r0, float r1);
void  Cave_StampSphere(Vector3 c, float r);
void  Cave_StampSolid(Vector3 c, float r);
void  Cave_ApplyNoise(float amp, float freq, float ridged, float band);
void  Cave_SealBorders(void);
void  Cave_BuildMesh(const CaveLook *look);
void  Cave_FreeMesh(void);
void  Cave_Draw(Camera3D cam, float heart);
void  Cave_SetLook(const CaveLook *look);

float Cave_Sample(Vector3 p);
float Cave_SampleGrad(Vector3 p, Vector3 *grad);
Vector3 Cave_Gradient(Vector3 p);
bool  Cave_Raycast(Vector3 from, Vector3 dir, float maxDist, float margin, float *hit);
bool  Cave_LineOfSight(Vector3 a, Vector3 b);
bool  Cave_Collide(Vector3 *pos, Vector3 *vel, float radius, float bounce);
bool  Cave_RandomOpenPoint(Rng *r, float minOpen, Vector3 center, float range, Vector3 *out);
int   Cave_SurfaceCount(void);
Vector3 Cave_SurfacePos(int i);
Vector3 Cave_SurfaceNormal(int i);
int   Cave_TriangleCount(void);
int   Cave_MeshCount(void);
int   Cave_DrawnCount(void);

#endif

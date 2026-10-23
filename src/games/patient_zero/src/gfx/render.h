/*
 * render.h - scene rendering: instanced cells, organisms, effects, plus the
 * custom rlgl instancing path shared with the particle system.
 */
#ifndef PZ_RENDER_H
#define PZ_RENDER_H
#include "core/game.h"
#include "gfx/shaders.h"

typedef struct { float c0[4], c1[4], c2[4], c3[4]; } InstData;   /* one mat4 */

bool Render_Init(void);
void Render_Shutdown(void);
void Render_RebuildPlayer(void);
void Render_DrawInstanced(Mesh mesh, ShaderId shader, const InstData *data, int count);
Mesh Render_QuadMesh(void);
void Render_World(Camera3D cam, float alpha);
void Render_Menu(Camera3D cam);
void Render_DrawCodexModel(int codexId, float yaw, float time);
int  Render_DrawCalls(void);
int  Render_Instances(void);
void Render_ResetCounters(void);

static inline InstData Inst_Make(Matrix basis, Vector3 tint, float phase)
{
    /* basis is a TRS matrix (raylib column-major): m0..m2 = X axis, m4..m6 = Y, m8..m10 = Z, m12..m14 = T */
    InstData d = { { basis.m0, basis.m1, basis.m2, tint.x }, { basis.m4, basis.m5, basis.m6, tint.y },
                   { basis.m8, basis.m9, basis.m10, tint.z }, { basis.m12, basis.m13, basis.m14, phase } };
    return d;
}

#endif

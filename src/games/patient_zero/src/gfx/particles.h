/*
 * particles.h - pooled CPU particle system rendered with instanced soft
 * billboards (alpha and additive passes). 14k particles, zero allocations.
 */
#ifndef PZ_PARTICLES_H
#define PZ_PARTICLES_H
#include "core/game.h"

#define PF_ADD      0x01   /* additive blending                          */
#define PF_FLOW     0x02   /* carried by the fluid flow field            */
#define PF_TURB     0x04   /* curl-ish turbulence                        */
#define PF_AMBIENT  0x08   /* ambient mote (recycled around camera)      */
#define PF_SINK     0x10   /* slight gravity / buoyancy                  */

typedef struct {
    Vector3 pos, vel;
    float life, maxLife;
    float size0, size1;
    Vector4 c0, c1;        /* HDR rgb + alpha                              */
    float drag, rot, rotVel, stretch, soft;
    uint8_t flags;
    bool alive;
} Particle;

void Particles_Init(void);
void Particles_Clear(void);
void Particles_Emit(Vector3 pos, Vector3 vel, Vector4 c0, Vector4 c1, float size0, float size1, float life, float drag, uint8_t flags);
void Particles_EmitStretch(Vector3 pos, Vector3 vel, Vector4 c0, Vector4 c1, float size, float life, float stretch, uint8_t flags);
void Particles_Burst(Vector3 pos, int n, float speed, Vector4 c0, Vector4 c1, float size, float life, uint8_t flags);
void Particles_Ring(Vector3 pos, Vector3 axis, int n, float speed, Vector4 c0, Vector4 c1, float size, float life);
void Particles_Ambient(Vector3 camPos, Vector3 colorA, Vector3 colorB, int target, float dt);
void Particles_Update(float dt);
void Particles_Draw(Camera3D cam);
int  Particles_Alive(void);

static inline Vector4 V4(float x, float y, float z, float w) { return (Vector4){ x, y, z, w }; }

#endif

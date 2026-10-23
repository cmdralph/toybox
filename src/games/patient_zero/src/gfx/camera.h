/*
 * camera.h - third-person chase rig with spring damping, cave collision,
 * FOV kick, trauma shake, cinematic orbits and a free photo-mode camera.
 */
#ifndef PZ_CAMERA_H
#define PZ_CAMERA_H
#include "core/game.h"

void    Cam_Init(void);
void    Cam_Look(float dx, float dy);
void    Cam_Update(float dt, Vector3 focus, float desiredDist);
void    Cam_AddTrauma(float t);
void    Cam_Kick(float fovDelta);
void    Cam_StartCinematic(Vector3 center, float radius, float height, float duration);
void    Cam_EnterFree(void);
void    Cam_UpdateFree(float dt);
void    Cam_UpdateMenu(float dt);
void    Cam_SnapTo(Vector3 focus, float dist);
Vector3 Cam_Forward(void);
Vector3 Cam_Right(void);
Vector3 Cam_Up(void);

#endif

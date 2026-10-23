/*
 * math_utils.h - geometry helpers on top of raymath.
 */
#ifndef PZ_MATH_UTILS_H
#define PZ_MATH_UTILS_H
#include "core/game.h"

Vector3 MU_AnyPerp(Vector3 v);
Matrix  MU_Basis(Vector3 fwd, Vector3 upHint, Vector3 pos, Vector3 scale);
Matrix  MU_BasisAxis(Vector3 axisY, Vector3 pos, float scale);
Vector3 MU_ClosestOnSegment(Vector3 p, Vector3 a, Vector3 b, float *t);
float   MU_AngleWrap(float a);
float   MU_EaseOutBack(float t);
float   MU_EaseOutCubic(float t);
float   MU_EaseInOut(float t);
Vector3 MU_HSV(float h, float s, float v);
Vector3 MU_ColorToV3(Color c);
Color   MU_V3ToColor(Vector3 c, float a);
Vector3 MU_RotateAround(Vector3 v, Vector3 axis, float ang);
bool    MU_RaySphere(Vector3 ro, Vector3 rd, Vector3 c, float r, float *t);
Vector3 MU_CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);
float   MU_SpringDamp(float x, float target, float *vel, float omega, float dt);
Vector3 MU_SpringDampV3(Vector3 x, Vector3 target, Vector3 *vel, float omega, float dt);

#endif

/*
 * shaders.c - GLSL sources and loading.
 *
 * Lighting model (shared chunk):
 *   - wrap diffuse so membranes never go fully black (light scatters in tissue)
 *   - fake subsurface translucency: light behind a thin cell glows through it
 *   - wet Blinn-Phong highlights
 *   - up to 16 point lights with smooth windowed falloff
 *   - distance fog in the region's fluid colour
 * All outputs are HDR (scene renders into a half-float target) so emissive
 * values above 1.0 drive the bloom pass.
 */
#include "gfx/shaders.h"
#include <stdio.h>
#include <string.h>

static Shader sShaders[SHD_COUNT];
static ShaderLocs sLocs[SHD_COUNT];

/* ------------------------------------------------------------------------- */
/* GLSL chunks                                                                */
/* ------------------------------------------------------------------------- */

#define GLSL_VERSION "#version 330\n"

static const char *CHUNK_NOISE =
    "float hash13(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }\n"
    "float vnoise(vec3 p) {\n"
    "    vec3 i = floor(p); vec3 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "    float a = mix(mix(hash13(i), hash13(i + vec3(1,0,0)), f.x), mix(hash13(i + vec3(0,1,0)), hash13(i + vec3(1,1,0)), f.x), f.y);\n"
    "    float b = mix(mix(hash13(i + vec3(0,0,1)), hash13(i + vec3(1,0,1)), f.x), mix(hash13(i + vec3(0,1,1)), hash13(i + vec3(1,1,1)), f.x), f.y);\n"
    "    return mix(a, b, f.z);\n"
    "}\n";

static const char *CHUNK_LIGHTING =
    "uniform vec3 viewPos;\n"
    "uniform float uTime;\n"
    "uniform vec3 uFogColor;\n"
    "uniform float uFogDensity;\n"
    "uniform vec3 uSunDir;\n"
    "uniform vec3 uSunColor;\n"
    "uniform vec3 uAmbient;\n"
    "uniform int uLightCount;\n"
    "uniform vec3 uPLPos[16];\n"
    "uniform vec3 uPLCol[16];\n"
    "uniform float uPLRad[16];\n"
    "vec3 shade(vec3 P, vec3 N, vec3 V, vec3 albedo, float sss, float spec) {\n"
    "    vec3 L = normalize(-uSunDir);\n"
    "    float ndl = dot(N, L);\n"
    "    float wrap = clamp((ndl + 0.55) / 1.55, 0.0, 1.0);\n"
    "    float hemi = 0.65 + 0.35 * N.y;\n"
    "    vec3 col = albedo * (uAmbient * hemi + uSunColor * wrap);\n"
    "    float tr = pow(clamp(dot(V, -L) * 0.5 + 0.5, 0.0, 1.0), 3.0) * sss;\n"
    "    col += albedo * uSunColor * tr;\n"
    "    vec3 H = normalize(L + V);\n"
    "    col += uSunColor * pow(max(dot(N, H), 0.0), 56.0) * spec;\n"
    "    for (int i = 0; i < 16; i++) {\n"
    "        if (i >= uLightCount) break;\n"
    "        vec3 d = uPLPos[i] - P;\n"
    "        float dist = length(d);\n"
    "        float att = clamp(1.0 - dist / uPLRad[i], 0.0, 1.0);\n"
    "        att = att * att;\n"
    "        if (att <= 0.0) continue;\n"
    "        vec3 l = d / max(dist, 0.001);\n"
    "        float w = clamp((dot(N, l) + 0.4) / 1.4, 0.0, 1.0);\n"
    "        col += albedo * uPLCol[i] * att * (w + sss * 0.45);\n"
    "        col += uPLCol[i] * att * pow(max(dot(N, normalize(l + V)), 0.0), 40.0) * spec;\n"
    "    }\n"
    "    return col;\n"
    "}\n"
    "vec3 applyFog(vec3 c, float dist) {\n"
    "    float f = 1.0 - exp(-pow(dist * uFogDensity, 1.5));\n"
    "    return mix(c, uFogColor, clamp(f, 0.0, 1.0));\n"
    "}\n";

static const char *CHUNK_MATERIAL =
    "uniform vec3 uRimColor;\n"
    "uniform float uRimPower;\n"
    "uniform float uRimStrength;\n"
    "uniform float uRimAlpha;\n"
    "uniform vec3 uEmissive;\n"
    "uniform float uWobble;\n"
    "uniform float uWobbleFreq;\n"
    "uniform float uPulse;\n"
    "uniform float uPulseSpeed;\n"
    "uniform float uAlpha;\n"
    "uniform float uSSS;\n"
    "uniform float uSpec;\n"
    "vec3 wobble(vec3 p, vec3 n, float ph) {\n"
    "    float t = uTime + ph * 6.2831;\n"
    "    float w = sin(p.x * uWobbleFreq + t * 1.7) * sin(p.y * uWobbleFreq * 1.31 + t * 1.27) * sin(p.z * uWobbleFreq * 0.87 + t * 2.11);\n"
    "    p += n * w * uWobble;\n"
    "    p *= 1.0 + uPulse * sin(t * uPulseSpeed);\n"
    "    return p;\n"
    "}\n";

/* ---- LIT (single mesh, raylib DrawMesh) ---------------------------------- */

static const char *LIT_VS_BODY =
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec3 fragPos;\n"
    "out vec3 fragNormal;\n"
    "out vec4 fragColor;\n"
    "out vec2 fragUV;\n"
    "void main() {\n"
    "    vec3 p = wobble(vertexPosition, vertexNormal, vertexTexCoord.x * 0.0);\n"
    "    fragPos = vec3(matModel * vec4(p, 1.0));\n"
    "    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));\n"
    "    fragColor = vertexColor;\n"
    "    fragUV = vertexTexCoord;\n"
    "    gl_Position = mvp * vec4(p, 1.0);\n"
    "}\n";

static const char *LIT_FS_BODY =
    "in vec3 fragPos;\n"
    "in vec3 fragNormal;\n"
    "in vec4 fragColor;\n"
    "in vec2 fragUV;\n"
    "uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 N = normalize(fragNormal);\n"
    "    vec3 V = normalize(viewPos - fragPos);\n"
    "    if (!gl_FrontFacing) N = -N;\n"
    "    vec3 albedo = fragColor.rgb * colDiffuse.rgb;\n"
    "    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), uRimPower);\n"
    "    vec3 col = shade(fragPos, N, V, albedo, uSSS, uSpec);\n"
    "    col += uRimColor * fres * uRimStrength;\n"
    "    float glow = 0.75 + 0.25 * sin(uTime * 3.0 + fragPos.x * 0.7 + fragPos.y);\n"
    "    col += uEmissive * glow * fragColor.a;\n"
    "    col = applyFog(col, length(viewPos - fragPos));\n"
    "    float a = clamp(colDiffuse.a * uAlpha + fres * uRimAlpha, 0.0, 1.0);\n"
    "    finalColor = vec4(col, a);\n"
    "}\n";

/* ---- INST (instanced cells) ---------------------------------------------- */
/* instanceTransform columns: c0..c2 = basis*scale (w = rgb tint), c3 = pos (w = phase).
 * Tint values above 1.0 are emitted as glow (cheap per-instance emissive). */

static const char *INST_VS_BODY =
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 mvp;\n"
    "out vec3 fragPos;\n"
    "out vec3 fragNormal;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragTint;\n"
    "out float fragPhase;\n"
    "void main() {\n"
    "    vec4 c0 = instanceTransform[0], c1 = instanceTransform[1], c2 = instanceTransform[2], c3 = instanceTransform[3];\n"
    "    mat4 M = mat4(vec4(c0.xyz, 0.0), vec4(c1.xyz, 0.0), vec4(c2.xyz, 0.0), vec4(c3.xyz, 1.0));\n"
    "    vec3 p = wobble(vertexPosition, vertexNormal, c3.w);\n"
    "    vec4 wp = M * vec4(p, 1.0);\n"
    "    fragPos = wp.xyz;\n"
    "    fragNormal = normalize(mat3(M) * vertexNormal);\n"
    "    fragColor = vertexColor;\n"
    "    fragTint = vec3(c0.w, c1.w, c2.w);\n"
    "    fragPhase = c3.w;\n"
    "    gl_Position = mvp * wp;\n"
    "}\n";

static const char *INST_FS_BODY =
    "in vec3 fragPos;\n"
    "in vec3 fragNormal;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragTint;\n"
    "in float fragPhase;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 N = normalize(fragNormal);\n"
    "    vec3 V = normalize(viewPos - fragPos);\n"
    "    if (!gl_FrontFacing) N = -N;\n"
    "    vec3 tint = min(fragTint, vec3(1.0));\n"
    "    vec3 glowTint = max(fragTint - vec3(1.0), vec3(0.0));\n"
    "    vec3 albedo = fragColor.rgb * tint;\n"
    "    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), uRimPower);\n"
    "    vec3 col = shade(fragPos, N, V, albedo, uSSS, uSpec);\n"
    "    col += (uRimColor * tint + glowTint * 0.6) * fres * uRimStrength;\n"
    "    float pulse = 0.7 + 0.3 * sin(uTime * 4.0 + fragPhase * 6.2831);\n"
    "    col += (uEmissive * tint + glowTint) * pulse * fragColor.a;\n"
    "    col = applyFog(col, length(viewPos - fragPos));\n"
    "    float a = clamp(uAlpha + fres * uRimAlpha, 0.0, 1.0);\n"
    "    finalColor = vec4(col, a);\n"
    "}\n";

/* ---- CAVE (tissue walls) ------------------------------------------------- */

static const char *CAVE_VS_BODY =
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform float uWallPulse;\n"
    "uniform float uHeart;\n"
    "out vec3 fragPos;\n"
    "out vec3 fragNormal;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec3 p = vertexPosition;\n"
    "    float n = sin(p.x * 0.09 + p.z * 0.07 + uTime * 0.6) * 0.5 + 0.5;\n"
    "    p -= vertexNormal * uWallPulse * uHeart * (0.4 + 0.6 * n);\n"
    "    fragPos = vec3(matModel * vec4(p, 1.0));\n"
    "    fragNormal = vertexNormal;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp * vec4(p, 1.0);\n"
    "}\n";

static const char *CAVE_FS_BODY =
    "in vec3 fragPos;\n"
    "in vec3 fragNormal;\n"
    "in vec4 fragColor;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 uDetailColor;\n"
    "uniform vec3 uVeinColor;\n"
    "uniform float uVeinGlow;\n"
    "uniform vec3 uCausticColor;\n"
    "uniform float uSpec;\n"
    "uniform float uRimStrength;\n"
    "uniform vec3 uRimColor;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 P = fragPos;\n"
    "    vec3 N = normalize(fragNormal);\n"
    "    /* bump the normal with two octaves of noise (cheap finite differences) */\n"
    "    float e = 0.6;\n"
    "    float n0 = vnoise(P * 0.55);\n"
    "    vec3 g = vec3(vnoise((P + vec3(e,0,0)) * 0.55) - n0, vnoise((P + vec3(0,e,0)) * 0.55) - n0, vnoise((P + vec3(0,0,e)) * 0.55) - n0);\n"
    "    N = normalize(N - g * 1.6);\n"
    "    vec3 V = normalize(viewPos - P);\n"
    "    float d1 = vnoise(P * 0.18);\n"
    "    float d2 = vnoise(P * 1.4);\n"
    "    vec3 albedo = fragColor.rgb * mix(vec3(1.0), uDetailColor, d1 * 0.55) * (0.82 + 0.3 * d2);\n"
    "    float vein = 1.0 - abs(vnoise(P * 0.07 + vec3(3.1)) * 2.0 - 1.0);\n"
    "    vein = smoothstep(0.86, 0.99, vein);\n"
    "    albedo = mix(albedo, uVeinColor, vein * 0.75);\n"
    "    float ao = fragColor.a;\n"
    "    vec3 col = shade(P, N, V, albedo, 0.35, uSpec) * ao;\n"
    "    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0);\n"
    "    col += uRimColor * fres * uRimStrength * ao;\n"
    "    /* animated caustic filaments, like light refracting through plasma */\n"
    "    float c = vnoise(P * 0.16 + vec3(uTime * 0.21, uTime * 0.09, -uTime * 0.13));\n"
    "    float c2 = vnoise(P * 0.33 - vec3(uTime * 0.17, 0.0, uTime * 0.11));\n"
    "    float caust = pow(1.0 - abs(c * 2.0 - 1.0), 7.0) + 0.6 * pow(1.0 - abs(c2 * 2.0 - 1.0), 9.0);\n"
    "    col += uCausticColor * caust * ao * (0.25 + 0.3 * max(N.y, 0.0));\n"
    "    col += uVeinColor * vein * uVeinGlow * (0.45 + 0.35 * sin(uTime * 2.0 + P.x * 0.2));\n"
    "    col = applyFog(col, length(viewPos - P));\n"
    "    finalColor = vec4(col, 1.0);\n"
    "}\n";

/* ---- PARTICLE (instanced billboards) ------------------------------------- */
/* c0 = pos.xyz, size   c1 = rgba (HDR)   c2 = velocity.xyz, stretch   c3 = rotation, softness */

static const char *PART_VS_BODY =
    "in vec3 vertexPosition;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 mvp;\n"
    "uniform vec3 uCamRight;\n"
    "uniform vec3 uCamUp;\n"
    "uniform vec3 uCamPos;\n"
    "out vec2 fragUV;\n"
    "out vec4 fragColor;\n"
    "out float fragSoft;\n"
    "out float fragDist;\n"
    "void main() {\n"
    "    vec4 c0 = instanceTransform[0], c1 = instanceTransform[1], c2 = instanceTransform[2], c3 = instanceTransform[3];\n"
    "    vec3 pos = c0.xyz; float size = c0.w;\n"
    "    vec2 corner = vertexPosition.xy;\n"
    "    vec3 world;\n"
    "    vec3 vel = c2.xyz;\n"
    "    vec3 vd = normalize(pos - uCamPos);\n"
    "    vec3 v = vel - dot(vel, vd) * vd;\n"
    "    float vl = length(v);\n"
    "    if (c2.w > 0.0 && vl > 0.01) {\n"
    "        vec3 ax = v / vl;\n"
    "        vec3 ay = normalize(cross(vd, ax));\n"
    "        world = pos + ax * corner.x * (size + vl * c2.w) + ay * corner.y * size;\n"
    "    } else {\n"
    "        float cr = cos(c3.x), sr = sin(c3.x);\n"
    "        vec2 rc = vec2(corner.x * cr - corner.y * sr, corner.x * sr + corner.y * cr);\n"
    "        world = pos + uCamRight * rc.x * size + uCamUp * rc.y * size;\n"
    "    }\n"
    "    fragUV = corner * 2.0;\n"
    "    fragColor = c1;\n"
    "    fragSoft = c3.y;\n"
    "    fragDist = length(pos - uCamPos);\n"
    "    gl_Position = mvp * vec4(world, 1.0);\n"
    "}\n";

static const char *PART_FS_BODY =
    "in vec2 fragUV;\n"
    "in vec4 fragColor;\n"
    "in float fragSoft;\n"
    "in float fragDist;\n"
    "uniform vec3 uFogColor;\n"
    "uniform float uFogDensity;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    float d = length(fragUV);\n"
    "    if (d > 1.0) discard;\n"
    "    float a = 1.0 - smoothstep(1.0 - max(fragSoft, 0.02), 1.0, d);\n"
    "    float core = exp(-d * d * 3.0);\n"
    "    vec3 col = fragColor.rgb * (0.55 + 0.9 * core);\n"
    "    float fog = exp(-pow(fragDist * uFogDensity, 1.5));\n"
    "    finalColor = vec4(col, fragColor.a * a * fog);\n"
    "}\n";

/* ---- POST ---------------------------------------------------------------- */

static const char *POST_VS =
    GLSL_VERSION
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "out vec2 fragTexCoord;\n"
    "void main() { fragTexCoord = vertexTexCoord; gl_Position = mvp * vec4(vertexPosition, 1.0); }\n";

static const char *BRIGHT_FS =
    GLSL_VERSION
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform float uThreshold;\n"
    "uniform float uKnee;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 c = texture(texture0, fragTexCoord).rgb;\n"
    "    c = min(c, vec3(24.0));\n"
    "    float l = max(max(c.r, c.g), c.b);\n"
    "    float k = smoothstep(uThreshold, uThreshold + uKnee, l);\n"
    "    finalColor = vec4(c * k, 1.0);\n"
    "}\n";

static const char *BLUR_FS =
    GLSL_VERSION
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 uTexel;\n"
    "uniform vec2 uDir;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec2 o = uTexel * uDir;\n"
    "    vec3 c = texture(texture0, fragTexCoord).rgb * 0.2270270270;\n"
    "    c += texture(texture0, fragTexCoord + o * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture(texture0, fragTexCoord - o * 1.3846153846).rgb * 0.3162162162;\n"
    "    c += texture(texture0, fragTexCoord + o * 3.2307692308).rgb * 0.0702702703;\n"
    "    c += texture(texture0, fragTexCoord - o * 3.2307692308).rgb * 0.0702702703;\n"
    "    finalColor = vec4(c, 1.0);\n"
    "}\n";

static const char *COMPOSITE_FS =
    GLSL_VERSION
    "in vec2 fragTexCoord;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D uBloomTex;\n"
    "uniform sampler2D uBlurTex;\n"
    "uniform sampler2D uDepthTex;\n"
    "uniform float uBloomStrength;\n"
    "uniform float uDofStrength;\n"
    "uniform float uDofEnabled;\n"
    "uniform float uFocus;\n"
    "uniform vec2 uNearFar;\n"
    "uniform float uChroma;\n"
    "uniform float uVignette;\n"
    "uniform float uGrain;\n"
    "uniform float uPulseFx;\n"
    "uniform vec3 uLift;\n"
    "uniform vec3 uGain;\n"
    "uniform float uSaturation;\n"
    "uniform float uContrast;\n"
    "uniform vec3 uTint;\n"
    "uniform float uHaze;\n"
    "uniform vec3 uHazeColor;\n"
    "uniform float uFlash;\n"
    "uniform vec3 uFlashColor;\n"
    "uniform float uDamage;\n"
    "uniform float uLetterbox;\n"
    "uniform float uExposure;\n"
    "uniform float uShimmer;\n"
    "uniform float uTime;\n"
    "uniform vec2 uResolution;\n"
    "out vec4 finalColor;\n"
    "float h12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }\n"
    "float n2(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(h12(i), h12(i + vec2(1,0)), f.x), mix(h12(i + vec2(0,1)), h12(i + vec2(1,1)), f.x), f.y); }\n"
    "float linDepth(float d) { float z = d * 2.0 - 1.0; return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x)); }\n"
    "vec3 aces(vec3 x) { const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14; return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0); }\n"
    "void main() {\n"
    "    vec2 uv = fragTexCoord;\n"
    "    vec2 cc = uv - 0.5;\n"
    "    float r = length(cc * vec2(uResolution.x / uResolution.y, 1.0)) / 1.2;\n"
    "    uv += cc * uPulseFx * 0.014 * (1.0 - r);\n"
    "    uv += vec2(sin(uv.y * 38.0 + uTime * 5.0), cos(uv.x * 31.0 + uTime * 4.3)) * 0.0018 * uShimmer;\n"
    "    vec2 off = cc * uChroma * 0.010;\n"
    "    vec3 col;\n"
    "    col.r = texture(texture0, uv + off).r;\n"
    "    col.g = texture(texture0, uv).g;\n"
    "    col.b = texture(texture0, uv - off).b;\n"
    "    float lin = (uDofEnabled > 0.5) ? linDepth(texture(uDepthTex, uv).r) : mix(uFocus, uFocus * 3.0, smoothstep(0.15, 0.6, r));\n"
    "    float coc = clamp(abs(lin - uFocus) / (uFocus * 1.4 + 8.0), 0.0, 1.0);\n"
    "    coc = max(coc, smoothstep(140.0, 420.0, lin) * 0.8);\n"
    "    col = mix(col, texture(uBlurTex, uv).rgb, coc * uDofStrength);\n"
    "    float hz = n2(uv * vec2(uResolution.x / uResolution.y, 1.0) * 4.0 + vec2(uTime * 0.05, -uTime * 0.03));\n"
    "    hz = hz * n2(uv * 9.0 - vec2(uTime * 0.07, uTime * 0.02));\n"
    "    col += uHazeColor * hz * clamp(lin / 160.0, 0.0, 1.0) * uHaze;\n"
    "    col += texture(uBloomTex, uv).rgb * uBloomStrength;\n"
    "    col *= uExposure;\n"
    "    col = aces(col);\n"
    "    col = col * uGain + uLift * (1.0 - col);\n"
    "    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));\n"
    "    col = mix(vec3(luma), col, uSaturation);\n"
    "    col = (col - 0.5) * uContrast + 0.5;\n"
    "    col *= uTint;\n"
    "    col *= mix(1.0, smoothstep(1.05, 0.25, r), uVignette);\n"
    "    col = mix(col, vec3(0.55, 0.0, 0.02), uDamage * smoothstep(0.35, 0.95, r));\n"
    "    col = mix(col, uFlashColor, uFlash);\n"
    "    float g = h12(fragTexCoord * uResolution + fract(uTime * 7.31) * 413.0) - 0.5;\n"
    "    col += g * uGrain;\n"
    "    if (abs(fragTexCoord.y - 0.5) > 0.5 - uLetterbox) col = vec3(0.0);\n"
    "    finalColor = vec4(clamp(col, 0.0, 1.0), 1.0);\n"
    "}\n";

/* ------------------------------------------------------------------------- */
/* Assembly & loading                                                         */
/* ------------------------------------------------------------------------- */

static char sVsBuf[SHD_COUNT][12000];
static char sFsBuf[SHD_COUNT][12000];

static void Assemble(char *dst, size_t n, const char *a, const char *b, const char *c, const char *d)
{
    snprintf(dst, n, "%s%s%s%s%s", GLSL_VERSION, a ? a : "", b ? b : "", c ? c : "", d ? d : "");
}

static void CacheLocs(ShaderId id)
{
    Shader s = sShaders[id];
    ShaderLocs *L = &sLocs[id];
    memset(L, -1, sizeof(*L));
#define LOC(field, name) L->field = GetShaderLocation(s, name)
    LOC(mvp, "mvp");
    LOC(viewPos, "viewPos"); LOC(time, "uTime"); LOC(fogColor, "uFogColor"); LOC(fogDensity, "uFogDensity");
    LOC(sunDir, "uSunDir"); LOC(sunColor, "uSunColor"); LOC(ambient, "uAmbient");
    LOC(lightCount, "uLightCount"); LOC(plPos, "uPLPos"); LOC(plCol, "uPLCol"); LOC(plRad, "uPLRad");
    LOC(rimColor, "uRimColor"); LOC(rimPower, "uRimPower"); LOC(rimStrength, "uRimStrength"); LOC(rimAlpha, "uRimAlpha");
    LOC(emissive, "uEmissive"); LOC(wobble, "uWobble"); LOC(wobbleFreq, "uWobbleFreq"); LOC(pulse, "uPulse");
    LOC(pulseSpeed, "uPulseSpeed"); LOC(alpha, "uAlpha"); LOC(sss, "uSSS"); LOC(spec, "uSpec");
    LOC(camRight, "uCamRight"); LOC(camUp, "uCamUp"); LOC(camPos, "uCamPos");
    LOC(wallPulse, "uWallPulse"); LOC(heart, "uHeart"); LOC(detailColor, "uDetailColor"); LOC(veinColor, "uVeinColor");
    LOC(veinGlow, "uVeinGlow"); LOC(causticColor, "uCausticColor");
    LOC(threshold, "uThreshold"); LOC(knee, "uKnee"); LOC(texel, "uTexel"); LOC(dir, "uDir");
    LOC(bloomTex, "uBloomTex"); LOC(blurTex, "uBlurTex"); LOC(depthTex, "uDepthTex"); LOC(bloomStrength, "uBloomStrength");
    LOC(dofStrength, "uDofStrength"); LOC(focus, "uFocus"); LOC(nearFar, "uNearFar"); LOC(chroma, "uChroma");
    LOC(vignette, "uVignette"); LOC(grain, "uGrain"); LOC(pulseFx, "uPulseFx"); LOC(lift, "uLift"); LOC(gain, "uGain");
    LOC(saturation, "uSaturation"); LOC(contrast, "uContrast"); LOC(tint, "uTint"); LOC(haze, "uHaze");
    LOC(hazeColor, "uHazeColor"); LOC(flash, "uFlash"); LOC(flashColor, "uFlashColor"); LOC(damage, "uDamage");
    LOC(letterbox, "uLetterbox"); LOC(exposure, "uExposure"); LOC(shimmer, "uShimmer"); LOC(resolution, "uResolution");
    LOC(dofEnabled, "uDofEnabled");
#undef LOC
    L->instance = GetShaderLocationAttrib(s, "instanceTransform");
}

bool Shaders_Init(void)
{
    Assemble(sVsBuf[SHD_LIT], sizeof(sVsBuf[0]), CHUNK_LIGHTING, CHUNK_MATERIAL, LIT_VS_BODY, NULL);
    Assemble(sFsBuf[SHD_LIT], sizeof(sFsBuf[0]), CHUNK_LIGHTING, CHUNK_MATERIAL, LIT_FS_BODY, NULL);
    Assemble(sVsBuf[SHD_INST], sizeof(sVsBuf[0]), CHUNK_LIGHTING, CHUNK_MATERIAL, INST_VS_BODY, NULL);
    Assemble(sFsBuf[SHD_INST], sizeof(sFsBuf[0]), CHUNK_LIGHTING, CHUNK_MATERIAL, INST_FS_BODY, NULL);
    Assemble(sVsBuf[SHD_CAVE], sizeof(sVsBuf[0]), "uniform float uTime;\n", CAVE_VS_BODY, NULL, NULL);
    Assemble(sFsBuf[SHD_CAVE], sizeof(sFsBuf[0]), CHUNK_NOISE, CHUNK_LIGHTING, CAVE_FS_BODY, NULL);
    Assemble(sVsBuf[SHD_PARTICLE], sizeof(sVsBuf[0]), PART_VS_BODY, NULL, NULL, NULL);
    Assemble(sFsBuf[SHD_PARTICLE], sizeof(sFsBuf[0]), PART_FS_BODY, NULL, NULL, NULL);

    sShaders[SHD_LIT] = LoadShaderFromMemory(sVsBuf[SHD_LIT], sFsBuf[SHD_LIT]);
    sShaders[SHD_INST] = LoadShaderFromMemory(sVsBuf[SHD_INST], sFsBuf[SHD_INST]);
    sShaders[SHD_CAVE] = LoadShaderFromMemory(sVsBuf[SHD_CAVE], sFsBuf[SHD_CAVE]);
    sShaders[SHD_PARTICLE] = LoadShaderFromMemory(sVsBuf[SHD_PARTICLE], sFsBuf[SHD_PARTICLE]);
    sShaders[SHD_BRIGHT] = LoadShaderFromMemory(POST_VS, BRIGHT_FS);
    sShaders[SHD_BLUR] = LoadShaderFromMemory(POST_VS, BLUR_FS);
    sShaders[SHD_COMPOSITE] = LoadShaderFromMemory(POST_VS, COMPOSITE_FS);

    bool ok = true;
    for (int i = 0; i < SHD_COUNT; i++) {
        if (sShaders[i].id == 0 || sShaders[i].id == rlGetShaderIdDefault()) {
            TraceLog(LOG_WARNING, "PZ: shader %d failed to compile", i);
            ok = false;
        }
        CacheLocs((ShaderId)i);
    }
    return ok;
}

void Shaders_Unload(void)
{
    for (int i = 0; i < SHD_COUNT; i++) {
        if (sShaders[i].id && sShaders[i].id != rlGetShaderIdDefault()) UnloadShader(sShaders[i]);
        sShaders[i] = (Shader){ 0 };
    }
}

Shader Shaders_Get(ShaderId id) { return sShaders[id]; }
const ShaderLocs *Shaders_Locs(ShaderId id) { return &sLocs[id]; }

MaterialParams Shaders_DefaultMaterial(void)
{
    MaterialParams m = { 0 };
    m.rimColor = (Vector3){ 1, 1, 1 };
    m.rimPower = 3.0f;
    m.rimStrength = 0.5f;
    m.rimAlpha = 0.0f;
    m.wobble = 0.0f;
    m.wobbleFreq = 2.0f;
    m.pulse = 0.0f;
    m.pulseSpeed = 2.0f;
    m.alpha = 1.0f;
    m.sss = 0.4f;
    m.spec = 0.4f;
    return m;
}

static void SetF(Shader s, int loc, float v) { if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT); }
static void SetV3(Shader s, int loc, Vector3 v) { if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_VEC3); }

void Shaders_SetMaterial(ShaderId id, const MaterialParams *m)
{
    Shader s = sShaders[id];
    const ShaderLocs *L = &sLocs[id];
    SetV3(s, L->rimColor, m->rimColor);
    SetF(s, L->rimPower, m->rimPower);
    SetF(s, L->rimStrength, m->rimStrength);
    SetF(s, L->rimAlpha, m->rimAlpha);
    SetV3(s, L->emissive, m->emissive);
    SetF(s, L->wobble, m->wobble);
    SetF(s, L->wobbleFreq, m->wobbleFreq);
    SetF(s, L->pulse, m->pulse);
    SetF(s, L->pulseSpeed, m->pulseSpeed);
    SetF(s, L->alpha, m->alpha);
    SetF(s, L->sss, m->sss);
    SetF(s, L->spec, m->spec);
}

void Shaders_SetFrame(Vector3 viewPos, float time, Vector3 fogColor, float fogDensity, Vector3 sunDir, Vector3 sunColor, Vector3 ambient)
{
    ShaderId lit[] = { SHD_LIT, SHD_INST, SHD_CAVE, SHD_PARTICLE };
    for (int k = 0; k < 4; k++) {
        Shader s = sShaders[lit[k]];
        const ShaderLocs *L = &sLocs[lit[k]];
        SetV3(s, L->viewPos, viewPos);
        SetF(s, L->time, time);
        SetV3(s, L->fogColor, fogColor);
        SetF(s, L->fogDensity, fogDensity);
        SetV3(s, L->sunDir, sunDir);
        SetV3(s, L->sunColor, sunColor);
        SetV3(s, L->ambient, ambient);
    }
}

void Shaders_SetLights(const Vector3 *pos, const Vector3 *col, const float *rad, int count)
{
    ShaderId lit[] = { SHD_LIT, SHD_INST, SHD_CAVE };
    for (int k = 0; k < 3; k++) {
        Shader s = sShaders[lit[k]];
        const ShaderLocs *L = &sLocs[lit[k]];
        if (L->lightCount >= 0) SetShaderValue(s, L->lightCount, &count, SHADER_UNIFORM_INT);
        if (count > 0) {
            if (L->plPos >= 0) SetShaderValueV(s, L->plPos, pos, SHADER_UNIFORM_VEC3, count);
            if (L->plCol >= 0) SetShaderValueV(s, L->plCol, col, SHADER_UNIFORM_VEC3, count);
            if (L->plRad >= 0) SetShaderValueV(s, L->plRad, rad, SHADER_UNIFORM_FLOAT, count);
        }
    }
}

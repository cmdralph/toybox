/*
 * widgets.c - immediate-mode UI in a virtual 1280x720 space.
 * Visual language: dark translucent "cytoplasm" panels, soft rounded
 * corners, bioluminescent accent edges that brighten on hover.
 */
#include "ui/widgets.h"
#include "audio/sfx.h"
#include <stdio.h>
#include <string.h>

static float sScale = 1.0f, sVW = UI_W, sVH = UI_H;
static int sLastHover = -1, sHoverThis = -1;
static int sActiveSlider = -1;
static const char *sTipTitle = NULL, *sTipBody = NULL;
static Color sTipAccent;

void UI_Begin(void)
{
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    float ui = G.settings.uiScale > 0.5f ? G.settings.uiScale : 1.0f;
    sScale = fminf(sw / UI_W, sh / UI_H) * ui;
    if (sScale < 0.3f) sScale = 0.3f;
    sVW = sw / sScale;
    sVH = sh / sScale;
    if (sHoverThis < 0) sLastHover = -1;
    sHoverThis = -1;
    if (!UI_MouseDown()) sActiveSlider = -1;
    /* gamepad drives a virtual cursor in menus (left stick / d-pad, A = click) */
    if (IsGamepadAvailable(0) && !IsCursorHidden()) {
        float lx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X), ly = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) lx = 1.0f;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) lx = -1.0f;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) ly = 1.0f;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) ly = -1.0f;
        if (fabsf(lx) + fabsf(ly) > 0.25f) {
            Vector2 m = GetMousePosition();
            float sp = 900.0f * GetFrameTime();
            m.x = Clamp(m.x + lx * sp, 0.0f, sw - 1.0f);
            m.y = Clamp(m.y + ly * sp, 0.0f, sh - 1.0f);
            SetMousePosition((int)m.x, (int)m.y);
        }
    }
    for (int i = 0; i < 64; i++) G.ui.hover[i] = fmaxf(0.0f, G.ui.hover[i] - GetFrameTime() * 4.0f);
}

float UI_Scale(void) { return sScale; }
float UI_Width(void) { return sVW; }
float UI_Height(void) { return sVH; }
Vector2 UI_Mouse(void) { Vector2 m = GetMousePosition(); return (Vector2){ m.x / sScale, m.y / sScale }; }
Vector2 UI_ToScreen(Vector2 v) { return (Vector2){ v.x * sScale, v.y * sScale }; }
Rectangle UI_Rect(float x, float y, float w, float h) { return (Rectangle){ x, y, w, h }; }
static Rectangle S(Rectangle r) { return (Rectangle){ r.x * sScale, r.y * sScale, r.width * sScale, r.height * sScale }; }
static bool PadA(bool held) { return IsGamepadAvailable(0) && (held ? IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) : IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)); }
bool UI_MousePressed(void) { return (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || PadA(false)) && G.ui.clickCooldown <= 0.0f; }
bool UI_MouseDown(void) { return IsMouseButtonDown(MOUSE_BUTTON_LEFT) || PadA(true); }
float UI_Wheel(void) { return GetMouseWheelMove(); }
bool UI_Hover(Rectangle r) { return CheckCollisionPointRec(UI_Mouse(), r); }
Color UI_Accent(void) { return G.settings.highContrast ? (Color){ 255, 255, 120, 255 } : (Color){ 110, 255, 190, 255 }; }
Color UI_Fade(Color c, float a) { c.a = (unsigned char)((float)c.a * Sat(a)); return c; }

/* ---- text ---------------------------------------------------------------- */

static float Spacing(float size) { return size / 10.0f; }

void UI_Text(const char *t, float x, float y, float size, Color c)
{
    DrawTextEx(GetFontDefault(), t, (Vector2){ x * sScale, y * sScale }, size * sScale, Spacing(size) * sScale, c);
}

float UI_Measure(const char *t, float size) { return MeasureTextEx(GetFontDefault(), t, size, Spacing(size)).x; }

void UI_TextCentered(const char *t, float cx, float y, float size, Color c) { UI_Text(t, cx - UI_Measure(t, size) * 0.5f, y, size, c); }
void UI_TextRight(const char *t, float rx, float y, float size, Color c) { UI_Text(t, rx - UI_Measure(t, size), y, size, c); }

void UI_TextShadow(const char *t, float x, float y, float size, Color c)
{
    UI_Text(t, x + size * 0.08f, y + size * 0.08f, size, (Color){ 0, 0, 0, (unsigned char)(c.a * 0.7f) });
    UI_Text(t, x, y, size, c);
}

int UI_TextWrapped(const char *t, float x, float y, float w, float size, float lineH, Color c)
{
    char line[256];
    int lines = 0;
    const char *p = t;
    while (*p) {
        int len = 0, lastSpace = -1;
        while (p[len] && p[len] != '\n' && len < 255) {
            line[len] = p[len];
            line[len + 1] = 0;
            if (p[len] == ' ') lastSpace = len;
            if (UI_Measure(line, size) > w) break;
            len++;
        }
        int cut = len;
        if (p[len] && p[len] != '\n' && lastSpace > 0) cut = lastSpace;
        memcpy(line, p, (size_t)cut);
        line[cut] = 0;
        UI_Text(line, x, y + (float)lines * lineH, size, c);
        lines++;
        p += cut;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
    }
    return lines;
}

/* ---- shapes -------------------------------------------------------------- */

void UI_Rect2(Rectangle r, Color c) { DrawRectangleRec(S(r), c); }
void UI_RectLines(Rectangle r, float thick, Color c) { DrawRectangleLinesEx(S(r), thick * sScale, c); }
void UI_Line(Vector2 a, Vector2 b, float thick, Color c) { DrawLineEx(UI_ToScreen(a), UI_ToScreen(b), thick * sScale, c); }
void UI_Circle(Vector2 c, float r, Color col) { DrawCircleV(UI_ToScreen(c), r * sScale, col); }
void UI_Ring(Vector2 c, float r0, float r1, float a0, float a1, Color col) { DrawRing(UI_ToScreen(c), r0 * sScale, r1 * sScale, a0, a1, 48, col); }

void UI_Panel(Rectangle r, Color fill, Color edge)
{
    Rectangle s = S(r);
    float round = fminf(0.25f, 14.0f / fmaxf(1.0f, fminf(r.width, r.height)));
    DrawRectangleRounded(s, round, 8, fill);
    if (edge.a > 0) DrawRectangleRoundedLinesEx(s, round, 8, 1.5f * sScale, edge);
}

void UI_Bar(Rectangle r, float frac, Color fill, Color back, float ghost)
{
    frac = Sat(frac);
    Rectangle s = S(r);
    float round = 0.5f;
    DrawRectangleRounded(s, round, 6, back);
    if (ghost > frac) {
        Rectangle g = s;
        g.width = s.width * Sat(ghost);
        DrawRectangleRounded(g, round, 6, (Color){ 255, 255, 255, 70 });
    }
    if (frac > 0.0f) {
        Rectangle f = s;
        f.width = fmaxf(s.height, s.width * frac);
        DrawRectangleRounded(f, round, 6, fill);
        Rectangle hl = f;
        hl.height *= 0.4f;
        hl.y += s.height * 0.1f;
        hl.x += s.height * 0.3f;
        hl.width = fmaxf(0.0f, hl.width - s.height * 0.6f);
        DrawRectangleRounded(hl, round, 6, (Color){ 255, 255, 255, 40 });
    }
}

void UI_Icon(IconId id, Vector2 c, float sz, Color col)
{
    Vector2 p = UI_ToScreen(c);
    float s = sz * sScale;
    switch (id) {
        case ICON_DASH:
            for (int k = 0; k < 2; k++) {
                float o = (float)k * s * 0.35f - s * 0.25f;
                DrawLineEx((Vector2){ p.x + o - s * 0.2f, p.y - s * 0.3f }, (Vector2){ p.x + o + s * 0.15f, p.y }, s * 0.12f, col);
                DrawLineEx((Vector2){ p.x + o + s * 0.15f, p.y }, (Vector2){ p.x + o - s * 0.2f, p.y + s * 0.3f }, s * 0.12f, col);
            }
            break;
        case ICON_BURST:
            DrawCircleV(p, s * 0.16f, col);
            for (int k = 0; k < 8; k++) {
                float a = (float)k * PI / 4.0f;
                DrawLineEx((Vector2){ p.x + cosf(a) * s * 0.25f, p.y + sinf(a) * s * 0.25f }, (Vector2){ p.x + cosf(a) * s * 0.42f, p.y + sinf(a) * s * 0.42f }, s * 0.08f, col);
            }
            break;
        case ICON_SWARM:
            for (int k = 0; k < 6; k++) {
                float a = (float)k * PI / 3.0f + 0.3f;
                DrawCircleV((Vector2){ p.x + cosf(a) * s * 0.26f, p.y + sinf(a) * s * 0.26f }, s * 0.09f, col);
            }
            DrawCircleV(p, s * 0.12f, col);
            break;
        case ICON_CLOAK:
            DrawRing(p, s * 0.26f, s * 0.34f, 0, 360, 24, Fade(col, 0.5f));
            for (int k = 0; k < 6; k++) DrawCircleSector(p, s * 0.34f, (float)k * 60.0f, (float)k * 60.0f + 25.0f, 4, col);
            DrawCircleV(p, s * 0.2f, (Color){ 20, 20, 30, 255 });
            DrawCircleV(p, s * 0.1f, col);
            break;
        case ICON_EYE:
            DrawEllipse((int)p.x, (int)p.y, s * 0.42f, s * 0.24f, col);
            DrawEllipse((int)p.x, (int)p.y, s * 0.34f, s * 0.17f, (Color){ 20, 10, 15, 255 });
            DrawCircleV(p, s * 0.13f, col);
            break;
        case ICON_DNA:
            for (int k = 0; k < 10; k++) {
                float t0 = (float)k / 10.0f, t1 = (float)(k + 1) / 10.0f;
                float y0 = p.y - s * 0.42f + t0 * s * 0.84f, y1 = p.y - s * 0.42f + t1 * s * 0.84f;
                float x0 = sinf(t0 * 6.28f) * s * 0.22f, x1 = sinf(t1 * 6.28f) * s * 0.22f;
                DrawLineEx((Vector2){ p.x + x0, y0 }, (Vector2){ p.x + x1, y1 }, s * 0.07f, col);
                DrawLineEx((Vector2){ p.x - x0, y0 }, (Vector2){ p.x - x1, y1 }, s * 0.07f, Fade(col, 0.7f));
                if (k % 2 == 0) DrawLineEx((Vector2){ p.x + x0, y0 }, (Vector2){ p.x - x0, y0 }, s * 0.04f, Fade(col, 0.6f));
            }
            break;
        case ICON_ATP: {
            Vector2 a = { p.x + s * 0.08f, p.y - s * 0.42f }, b = { p.x - s * 0.2f, p.y + s * 0.04f }, c2 = { p.x + s * 0.02f, p.y + s * 0.04f },
                    d = { p.x - s * 0.08f, p.y + s * 0.42f }, e = { p.x + s * 0.22f, p.y - s * 0.06f }, f = { p.x, p.y - s * 0.06f };
            DrawTriangle(a, b, c2, col); DrawTriangle(a, c2, f, col);
            DrawTriangle(f, d, e, col); DrawTriangle(c2, d, f, col);
        } break;
        case ICON_HEART:
            DrawCircleV((Vector2){ p.x - s * 0.15f, p.y - s * 0.08f }, s * 0.18f, col);
            DrawCircleV((Vector2){ p.x + s * 0.15f, p.y - s * 0.08f }, s * 0.18f, col);
            DrawTriangle((Vector2){ p.x - s * 0.32f, p.y - s * 0.02f }, (Vector2){ p.x, p.y + s * 0.36f }, (Vector2){ p.x + s * 0.32f, p.y - s * 0.02f }, col);
            break;
        case ICON_LOCK:
            DrawRectangleRounded((Rectangle){ p.x - s * 0.25f, p.y - s * 0.05f, s * 0.5f, s * 0.38f }, 0.2f, 4, col);
            DrawRing((Vector2){ p.x, p.y - s * 0.05f }, s * 0.13f, s * 0.2f, 180, 360, 12, col);
            break;
        case ICON_STAR: DrawPoly(p, 5, s * 0.4f, -90, col); DrawPoly(p, 5, s * 0.4f, -54, col); break;
        case ICON_SKULL:
            DrawCircleV((Vector2){ p.x, p.y - s * 0.06f }, s * 0.3f, col);
            DrawRectangleRec((Rectangle){ p.x - s * 0.17f, p.y + s * 0.12f, s * 0.34f, s * 0.2f }, col);
            DrawCircleV((Vector2){ p.x - s * 0.11f, p.y - s * 0.06f }, s * 0.08f, (Color){ 10, 10, 10, 255 });
            DrawCircleV((Vector2){ p.x + s * 0.11f, p.y - s * 0.06f }, s * 0.08f, (Color){ 10, 10, 10, 255 });
            break;
        case ICON_SHIELD:
            DrawTriangle((Vector2){ p.x - s * 0.3f, p.y - s * 0.3f }, (Vector2){ p.x, p.y + s * 0.4f }, (Vector2){ p.x + s * 0.3f, p.y - s * 0.3f }, col);
            DrawRectangleRec((Rectangle){ p.x - s * 0.3f, p.y - s * 0.38f, s * 0.6f, s * 0.1f }, col);
            break;
        default: break;
    }
}

/* ---- interactive widgets ------------------------------------------------- */

static float HoverAnim(int id, bool hovered)
{
    if (id < 0 || id >= 64) return hovered ? 1.0f : 0.0f;
    if (hovered) {
        sHoverThis = id;
        if (sLastHover != id) { Sfx_Play(SFX_UI_HOVER, 0.35f, 1.0f); sLastHover = id; }
        G.ui.hover[id] = fminf(1.0f, G.ui.hover[id] + GetFrameTime() * 12.0f);
    }
    return G.ui.hover[id];
}

bool UI_Button(int id, Rectangle r, const char *label, bool enabled)
{
    bool hov = enabled && UI_Hover(r);
    float h = HoverAnim(id, hov);
    Color acc = UI_Accent();
    Color fill = enabled ? (Color){ (unsigned char)(18 + 20 * h), (unsigned char)(26 + 40 * h), (unsigned char)(30 + 30 * h), 220 } : (Color){ 20, 20, 24, 160 };
    Color edge = enabled ? UI_Fade(acc, 0.35f + 0.65f * h) : (Color){ 70, 70, 80, 160 };
    Rectangle rr = r;
    rr.x -= h * 3.0f; rr.width += h * 6.0f;
    UI_Panel(rr, fill, edge);
    float size = fminf(22.0f, r.height * 0.5f);
    Color tc = enabled ? (Color){ (unsigned char)(210 + 45 * h), 255, (unsigned char)(220 + 35 * h), 255 } : (Color){ 110, 110, 120, 255 };
    UI_TextCentered(label, r.x + r.width * 0.5f, r.y + (r.height - size) * 0.5f, size, tc);
    if (hov && UI_MousePressed()) { Sfx_Play(SFX_UI_CLICK, 0.6f, 1.0f); G.ui.clickCooldown = 0.12f; return true; }
    return false;
}

bool UI_ButtonBig(int id, Rectangle r, const char *label, const char *sub, bool enabled)
{
    bool hov = enabled && UI_Hover(r);
    float h = HoverAnim(id, hov);
    Color acc = UI_Accent();
    Rectangle rr = r;
    rr.x += h * 10.0f;
    UI_Panel(rr, (Color){ (unsigned char)(10 + 25 * h), (unsigned char)(16 + 40 * h), (unsigned char)(20 + 30 * h), (unsigned char)(150 + 70 * h) }, UI_Fade(acc, 0.15f + 0.8f * h));
    if (h > 0.01f) UI_Rect2((Rectangle){ rr.x + 6, rr.y + 8, 4, rr.height - 16 }, UI_Fade(acc, h));
    Color tc = enabled ? (Color){ (unsigned char)(200 + 55 * h), 255, (unsigned char)(215 + 40 * h), 255 } : (Color){ 100, 100, 110, 255 };
    UI_Text(label, rr.x + 22, rr.y + 10, 28, tc);
    if (sub) UI_Text(sub, rr.x + 24, rr.y + 42, 12, enabled ? (Color){ 150, 190, 180, 255 } : (Color){ 90, 90, 100, 255 });
    if (hov && UI_MousePressed()) { Sfx_Play(SFX_UI_CLICK, 0.7f, 1.0f); G.ui.clickCooldown = 0.12f; return true; }
    return false;
}

bool UI_Slider(int id, Rectangle r, const char *label, float *v, float lo, float hi, const char *fmt)
{
    bool hov = UI_Hover(r);
    HoverAnim(id, hov);
    UI_Text(label, r.x, r.y + (r.height - 16) * 0.5f, 16, (Color){ 210, 235, 225, 255 });
    Rectangle bar = { r.x + r.width * 0.45f, r.y + r.height * 0.5f - 4, r.width * 0.42f, 8 };
    float t = (*v - lo) / (hi - lo);
    UI_Bar(bar, t, UI_Accent(), (Color){ 40, 50, 55, 255 }, 0.0f);
    UI_Circle((Vector2){ bar.x + bar.width * Sat(t), bar.y + 4 }, 8.0f + (sActiveSlider == id ? 2.0f : 0.0f), (Color){ 230, 255, 245, 255 });
    char buf[32];
    snprintf(buf, sizeof(buf), fmt ? fmt : "%.2f", fmt && strstr(fmt, "%%") ? *v * 100.0f : *v);
    UI_TextRight(buf, r.x + r.width, r.y + (r.height - 16) * 0.5f, 16, (Color){ 180, 220, 210, 255 });
    Rectangle hit = { bar.x - 10, r.y, bar.width + 20, r.height };
    if (UI_Hover(hit) && UI_MousePressed()) sActiveSlider = id;
    if (sActiveSlider == id && UI_MouseDown()) {
        float nt = Sat((UI_Mouse().x - bar.x) / bar.width);
        float nv = lo + (hi - lo) * nt;
        bool changed = fabsf(nv - *v) > 1e-5f;
        *v = nv;
        return changed;
    }
    return false;
}

bool UI_Toggle(int id, Rectangle r, const char *label, bool *v)
{
    bool hov = UI_Hover(r);
    float h = HoverAnim(id, hov);
    UI_Text(label, r.x, r.y + (r.height - 16) * 0.5f, 16, (Color){ (unsigned char)(210 + 45 * h), 235, 225, 255 });
    Rectangle pill = { r.x + r.width - 54, r.y + r.height * 0.5f - 11, 50, 22 };
    Color acc = UI_Accent();
    DrawRectangleRounded(S(pill), 1.0f, 8, *v ? UI_Fade(acc, 0.85f) : (Color){ 50, 55, 60, 255 });
    UI_Circle((Vector2){ *v ? pill.x + 39 : pill.x + 11, pill.y + 11 }, 8.5f, (Color){ 240, 255, 250, 255 });
    if (hov && UI_MousePressed()) {
        *v = !*v;
        Sfx_Play(SFX_UI_CLICK, 0.5f, *v ? 1.1f : 0.9f);
        G.ui.clickCooldown = 0.12f;
        return true;
    }
    return false;
}

bool UI_Selector(int id, Rectangle r, const char *label, int *idx, const char **options, int count)
{
    bool hov = UI_Hover(r);
    float h = HoverAnim(id, hov);
    UI_Text(label, r.x, r.y + (r.height - 16) * 0.5f, 16, (Color){ (unsigned char)(210 + 45 * h), 235, 225, 255 });
    float w = r.width * 0.45f;
    Rectangle box = { r.x + r.width - w, r.y + 2, w, r.height - 4 };
    UI_Panel(box, (Color){ 20, 30, 34, 220 }, UI_Fade(UI_Accent(), 0.3f + 0.5f * h));
    UI_TextCentered(options[*idx], box.x + box.width * 0.5f, box.y + (box.height - 16) * 0.5f, 16, (Color){ 230, 255, 245, 255 });
    Rectangle left = { box.x, box.y, 26, box.height }, right = { box.x + box.width - 26, box.y, 26, box.height };
    UI_TextCentered("<", left.x + 13, box.y + (box.height - 18) * 0.5f, 18, UI_Hover(left) ? UI_Accent() : (Color){ 150, 170, 165, 255 });
    UI_TextCentered(">", right.x + 13, box.y + (box.height - 18) * 0.5f, 18, UI_Hover(right) ? UI_Accent() : (Color){ 150, 170, 165, 255 });
    if (UI_MousePressed()) {
        if (UI_Hover(left)) { *idx = (*idx + count - 1) % count; Sfx_Play(SFX_UI_CLICK, 0.5f, 0.95f); G.ui.clickCooldown = 0.1f; return true; }
        if (UI_Hover(right) || UI_Hover(box)) { *idx = (*idx + 1) % count; Sfx_Play(SFX_UI_CLICK, 0.5f, 1.05f); G.ui.clickCooldown = 0.1f; return true; }
    }
    return false;
}

void UI_Tooltip(const char *title, const char *body, Color accent)
{
    sTipTitle = title;
    sTipBody = body;
    sTipAccent = accent;
    /* drawn immediately so it layers above the widget that requested it */
    Vector2 m = UI_Mouse();
    float w = 300.0f;
    float lines = body ? (float)((int)(UI_Measure(body, 14) / (w - 24)) + 2) : 0.0f;
    float h = 40.0f + lines * 18.0f;
    float x = m.x + 18, y = m.y + 18;
    if (x + w > sVW - 8) x = m.x - w - 18;
    if (y + h > sVH - 8) y = sVH - h - 8;
    UI_Panel((Rectangle){ x, y, w, h }, (Color){ 8, 14, 16, 240 }, UI_Fade(accent, 0.8f));
    if (title) UI_Text(title, x + 12, y + 10, 18, accent);
    if (body) UI_TextWrapped(body, x + 12, y + 34, w - 24, 14, 18, (Color){ 210, 225, 220, 255 });
    sTipTitle = NULL;
    sTipBody = NULL;
}

void UI_Backdrop(float alpha)
{
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){ 2, 6, 8, (unsigned char)(alpha * 255.0f) });
}

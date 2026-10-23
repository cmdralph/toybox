/*
 * widgets.h - immediate-mode UI kit drawn with raylib 2D: scaled text,
 * panels, buttons, sliders, toggles, selectors and icons. Everything is
 * laid out in a 1280x720 virtual space and scaled to the window.
 */
#ifndef PZ_WIDGETS_H
#define PZ_WIDGETS_H
#include "core/game.h"

#define UI_W 1280.0f
#define UI_H 720.0f

typedef enum { ICON_DASH = 0, ICON_BURST, ICON_SWARM, ICON_CLOAK, ICON_EYE, ICON_DNA, ICON_ATP, ICON_HEART, ICON_LOCK, ICON_STAR, ICON_SKULL, ICON_SHIELD, ICON_COUNT } IconId;

void  UI_Begin(void);                     /* compute scale/offset for this frame */
float UI_Scale(void);
float UI_Width(void);                     /* virtual width (>= 1280 on wide screens) */
float UI_Height(void);
Vector2 UI_Mouse(void);
Vector2 UI_ToScreen(Vector2 v);
Rectangle UI_Rect(float x, float y, float w, float h);
bool  UI_MousePressed(void);
bool  UI_MouseDown(void);
float UI_Wheel(void);
bool  UI_Hover(Rectangle r);

void  UI_Text(const char *t, float x, float y, float size, Color c);
void  UI_TextCentered(const char *t, float cx, float y, float size, Color c);
void  UI_TextRight(const char *t, float rx, float y, float size, Color c);
void  UI_TextShadow(const char *t, float x, float y, float size, Color c);
float UI_Measure(const char *t, float size);
int   UI_TextWrapped(const char *t, float x, float y, float w, float size, float lineH, Color c);   /* returns lines */
void  UI_Panel(Rectangle r, Color fill, Color edge);
void  UI_Rect2(Rectangle r, Color c);
void  UI_RectLines(Rectangle r, float thick, Color c);
void  UI_Line(Vector2 a, Vector2 b, float thick, Color c);
void  UI_Circle(Vector2 c, float r, Color col);
void  UI_Ring(Vector2 c, float r0, float r1, float a0, float a1, Color col);
void  UI_Bar(Rectangle r, float frac, Color fill, Color back, float ghost);
void  UI_Icon(IconId id, Vector2 c, float size, Color col);

bool  UI_Button(int id, Rectangle r, const char *label, bool enabled);
bool  UI_ButtonBig(int id, Rectangle r, const char *label, const char *sub, bool enabled);
bool  UI_Slider(int id, Rectangle r, const char *label, float *v, float lo, float hi, const char *fmt);
bool  UI_Toggle(int id, Rectangle r, const char *label, bool *v);
bool  UI_Selector(int id, Rectangle r, const char *label, int *idx, const char **options, int count);
void  UI_Tooltip(const char *title, const char *body, Color accent);
void  UI_Backdrop(float alpha);

Color UI_Accent(void);
Color UI_Fade(Color c, float a);

#endif

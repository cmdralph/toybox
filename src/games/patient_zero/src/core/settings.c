/*
 * settings.c - defaults and presets. Quality presets trade drifter density,
 * particle budget, light count and render scale for frame time.
 */
#include "core/settings.h"
#include <stdio.h>

const int RES_PRESETS[RES_PRESET_COUNT][2] = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 1024, 640 } };

const DifficultyDef DIFFICULTIES[DIFF_COUNT] = {
    { "Cautious", 0.60f, 0.80f, 0.70f, 0.60f, 0.80f, 1.5f },
    { "Standard", 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.0f },
    { "Pandemic", 1.40f, 1.25f, 1.30f, 1.40f, 1.25f, 0.7f },
};

void Settings_Default(Settings *s)
{
    *s = (Settings){ 0 };
    s->resIndex = 0;
    s->fullscreen = false;
    s->vsync = true;
    s->msaa = true;
    s->postfx = true;
    s->bloom = true;
    s->dof = true;
    s->chromatic = true;
    s->grain = true;
    s->master = 0.8f;
    s->sfx = 0.9f;
    s->music = 0.6f;
    s->sensitivity = 1.0f;
    s->invertY = false;
    s->shake = 1.0f;
    s->reduceFlashing = false;
    s->uiScale = 1.0f;
    s->highContrast = false;
    s->difficulty = DIFF_STANDARD;
    s->keys[ACT_FORWARD] = KEY_W;
    s->keys[ACT_BACK] = KEY_S;
    s->keys[ACT_LEFT] = KEY_A;
    s->keys[ACT_RIGHT] = KEY_D;
    s->keys[ACT_UP] = KEY_SPACE;
    s->keys[ACT_DOWN] = KEY_LEFT_CONTROL;
    s->keys[ACT_DASH] = KEY_LEFT_SHIFT;
    s->keys[ACT_BURST] = KEY_Q;
    s->keys[ACT_SWARM] = KEY_E;
    s->keys[ACT_CLOAK] = KEY_R;
    s->keys[ACT_MUTATIONS] = KEY_TAB;
    Settings_ApplyQuality(s, 2);
}

void Settings_ApplyQuality(Settings *s, int quality)
{
    s->quality = quality;
    switch (quality) {
        case 0:  s->renderScale = 0.75f; s->dof = false; break;
        case 1:  s->renderScale = 1.0f;  break;
        default: s->renderScale = 1.0f;  break;
    }
}

float Settings_DrifterDensity(void) { return G.settings.quality == 0 ? 0.5f : G.settings.quality == 1 ? 0.75f : 1.0f; }
int Settings_LightBudget(void) { return G.settings.quality == 0 ? 8 : G.settings.quality == 1 ? 12 : MAX_LIGHTS; }
int Settings_ParticleBudget(void) { return G.settings.quality == 0 ? 5000 : G.settings.quality == 1 ? 9000 : MAX_PARTICLES; }

const char *Settings_ActionName(int a)
{
    static const char *names[ACT_COUNT] = { "Swim forward", "Swim back", "Strafe left", "Strafe right", "Ascend", "Descend",
                                            "Dash", "Lysis burst", "Hive call", "Cloak", "Mutation tree" };
    return (a >= 0 && a < ACT_COUNT) ? names[a] : "?";
}

const char *Settings_KeyName(int key)
{
    static char buf[16];
    if (key >= KEY_A && key <= KEY_Z) { snprintf(buf, sizeof(buf), "%c", 'A' + (key - KEY_A)); return buf; }
    if (key >= KEY_ZERO && key <= KEY_NINE) { snprintf(buf, sizeof(buf), "%c", '0' + (key - KEY_ZERO)); return buf; }
    if (key >= KEY_F1 && key <= KEY_F12) { snprintf(buf, sizeof(buf), "F%d", 1 + key - KEY_F1); return buf; }
    switch (key) {
        case KEY_SPACE: return "Space";
        case KEY_LEFT_SHIFT: return "L-Shift";
        case KEY_RIGHT_SHIFT: return "R-Shift";
        case KEY_LEFT_CONTROL: return "L-Ctrl";
        case KEY_RIGHT_CONTROL: return "R-Ctrl";
        case KEY_LEFT_ALT: return "L-Alt";
        case KEY_RIGHT_ALT: return "R-Alt";
        case KEY_TAB: return "Tab";
        case KEY_ENTER: return "Enter";
        case KEY_BACKSPACE: return "Backspace";
        case KEY_UP: return "Up";
        case KEY_DOWN: return "Down";
        case KEY_LEFT: return "Left";
        case KEY_RIGHT: return "Right";
        case KEY_CAPS_LOCK: return "Caps";
        case KEY_GRAVE: return "`";
        default: snprintf(buf, sizeof(buf), "Key %d", key); return buf;
    }
}

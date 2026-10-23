/*
 * settings_ui.c - tabbed settings screen. Every change is applied live and
 * the profile is saved when the screen closes.
 */
#include "ui/settings_ui.h"
#include "ui/widgets.h"
#include "ui/hud.h"
#include "gfx/postfx.h"
#include "core/settings.h"
#include "core/save.h"
#include "core/game_state.h"
#include "core/input.h"
#include "audio/sfx.h"
#include <stdio.h>
#include <string.h>

void SettingsUI_Apply(bool resolutionChanged)
{
    Settings *s = &G.settings;
    bool borderless = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    if (s->fullscreen != borderless) ToggleBorderlessWindowed();
    if (resolutionChanged && !s->fullscreen) {
        int w = RES_PRESETS[s->resIndex][0], h = RES_PRESETS[s->resIndex][1];
        int mon = GetCurrentMonitor();
        int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
        if (mw > 0 && w > mw) w = mw;
        if (mh > 0 && h > mh) h = mh;
        SetWindowSize(w, h);
        if (mw > 0 && mh > 0) SetWindowPosition((mw - w) / 2, (mh - h) / 2);
    }
    if (s->vsync) { SetWindowState(FLAG_VSYNC_HINT); SetTargetFPS(0); }
    else { ClearWindowState(FLAG_VSYNC_HINT); SetTargetFPS(240); }
    PostFx_Resize(GetScreenWidth(), GetScreenHeight());
}

static void Header(const char *t, float x, float y)
{
    UI_Text(t, x, y, 14, UI_Accent());
    UI_Rect2((Rectangle){ x, y + 18, 520, 1 }, UI_Fade(UI_Accent(), 0.3f));
}

void SettingsUI_Draw(void)
{
    Settings *s = &G.settings;
    float W = UI_Width(), H = UI_Height();
    bool wasRebinding = G.ui.rebindAction >= 0;
    UI_Backdrop(0.6f);
    float pw = 640, ph = 600;
    float x = W * 0.5f - pw * 0.5f, y = H * 0.5f - ph * 0.5f;
    UI_Panel((Rectangle){ x, y, pw, ph }, (Color){ 6, 12, 14, 240 }, UI_Fade(UI_Accent(), 0.5f));
    UI_Text("SETTINGS", x + 24, y + 18, 30, (Color){ 230, 255, 240, 255 });

    static const char *TABS[4] = { "VIDEO", "AUDIO", "CONTROLS", "GAMEPLAY" };
    for (int i = 0; i < 4; i++) {
        Rectangle r = { x + 24 + (float)i * 146, y + 60, 138, 32 };
        bool sel = G.ui.settingsTab == i;
        if (sel) UI_Panel(r, UI_Fade(UI_Accent(), 0.25f), UI_Accent());
        if (UI_Button(20 + i, r, TABS[i], true)) { G.ui.settingsTab = i; G.ui.rebindAction = -1; }
    }
    float cx = x + 40, cy = y + 112, rw = 560, rh = 30, gap = 36;
    int id = 0;
    switch (G.ui.settingsTab) {
        case 0: {
            Header("DISPLAY", cx, cy); cy += 28;
            static const char *res[RES_PRESET_COUNT] = { "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "1024 x 640" };
            if (UI_Selector(id++, (Rectangle){ cx, cy, rw, rh }, "Window size", &s->resIndex, res, RES_PRESET_COUNT)) SettingsUI_Apply(true);
            cy += gap;
            if (UI_Toggle(id++, (Rectangle){ cx, cy, rw, rh }, "Fullscreen (borderless)", &s->fullscreen)) SettingsUI_Apply(false);
            cy += gap;
            if (UI_Toggle(id++, (Rectangle){ cx, cy, rw, rh }, "V-Sync", &s->vsync)) SettingsUI_Apply(false);
            cy += gap;
            UI_Toggle(id++, (Rectangle){ cx, cy, rw, rh }, "MSAA 4x (UI & non-post path, restart)", &s->msaa);
            cy += gap + 6;
            Header("QUALITY", cx, cy); cy += 28;
            static const char *q[3] = { "Low", "Medium", "High" };
            int qi = s->quality;
            if (UI_Selector(id++, (Rectangle){ cx, cy, rw, rh }, "Preset (density, particles, lights)", &qi, q, 3)) { Settings_ApplyQuality(s, qi); SettingsUI_Apply(false); }
            cy += gap;
            static bool scaleDirty = false;
            if (UI_Slider(id++, (Rectangle){ cx, cy, rw, rh }, "Render scale", &s->renderScale, 0.5f, 1.0f, "%.0f%%")) scaleDirty = true;
            if (scaleDirty && !UI_MouseDown()) { scaleDirty = false; SettingsUI_Apply(false); }
            cy += gap;
            UI_Toggle(id++, (Rectangle){ cx, cy, rw * 0.48f, rh }, "Post-processing", &s->postfx);
            UI_Toggle(id++, (Rectangle){ cx + rw * 0.52f, cy, rw * 0.48f, rh }, "Bloom", &s->bloom);
            cy += gap;
            UI_Toggle(id++, (Rectangle){ cx, cy, rw * 0.48f, rh }, "Depth of field", &s->dof);
            UI_Toggle(id++, (Rectangle){ cx + rw * 0.52f, cy, rw * 0.48f, rh }, "Chromatic aberration", &s->chromatic);
            cy += gap;
            UI_Toggle(id++, (Rectangle){ cx, cy, rw * 0.48f, rh }, "Film grain", &s->grain);
        } break;
        case 1:
            Header("VOLUME", cx, cy); cy += 28;
            UI_Slider(id++, (Rectangle){ cx, cy, rw, rh }, "Master", &s->master, 0.0f, 1.0f, "%.0f%%"); cy += gap;
            UI_Slider(id++, (Rectangle){ cx, cy, rw, rh }, "Effects & ambience", &s->sfx, 0.0f, 1.0f, "%.0f%%"); cy += gap;
            UI_Slider(id++, (Rectangle){ cx, cy, rw, rh }, "Music", &s->music, 0.0f, 1.0f, "%.0f%%"); cy += gap;
            if (UI_Button(id++, (Rectangle){ cx, cy + 10, 200, 34 }, "Test sound", true)) Sfx_Play(SFX_INFECT, 1.0f, 1.0f);
            UI_TextWrapped("All audio - every effect, the adaptive score and the fluid ambience - is synthesised in real time. The music responds to Alert, combat and your evolution stage; the heartbeat speeds up as the immune system closes in.",
                           cx, cy + 70, rw, 12, 17, (Color){ 160, 190, 180, 220 });
            break;
        case 2: {
            Header("MOUSE", cx, cy); cy += 28;
            UI_Slider(id++, (Rectangle){ cx, cy, rw, rh }, "Look sensitivity", &s->sensitivity, 0.2f, 3.0f, "%.2f"); cy += gap;
            UI_Toggle(id++, (Rectangle){ cx, cy, rw, rh }, "Invert Y", &s->invertY); cy += gap + 4;
            Header("KEYBINDINGS  (click, then press a key - Esc cancels)", cx, cy); cy += 26;
            for (int a = 0; a < ACT_COUNT; a++) {
                float col = (a % 2) * (rw * 0.5f + 10);
                float ry = cy + (float)(a / 2) * 32;
                Rectangle r = { cx + col, ry, rw * 0.5f - 10, 28 };
                bool waiting = G.ui.rebindAction == a;
                char lbl[64];
                snprintf(lbl, sizeof(lbl), "%s:  %s", Settings_ActionName(a), waiting ? "..." : Settings_KeyName(s->keys[a]));
                if (UI_Button(30 + a, r, lbl, true)) G.ui.rebindAction = a;
                if (waiting) UI_RectLines(r, 2, (Color){ 255, 230, 120, (unsigned char)(150 + 100 * sinf(G.realTime * 8.0f)) });
            }
            if (G.ui.rebindAction >= 0) {
                int k = GetKeyPressed();
                if (k == KEY_ESCAPE) G.ui.rebindAction = -1;
                else if (k > 0) {
                    for (int a = 0; a < ACT_COUNT; a++) if (s->keys[a] == k && a != G.ui.rebindAction) s->keys[a] = s->keys[G.ui.rebindAction];   /* swap */
                    s->keys[G.ui.rebindAction] = k;
                    G.ui.rebindAction = -1;
                    Sfx_Play(SFX_UI_CLICK, 0.6f, 1.2f);
                }
            }
            UI_Text("Gamepad: L-stick swim, R-stick look, RT toxin, LT infect, A dash, X burst, Y hive, B cloak, LB/RB descend/ascend, Back genome", cx, y + ph - 96, 10, (Color){ 150, 180, 170, 220 });
        } break;
        default: {
            Header("GAMEPLAY", cx, cy); cy += 28;
            static const char *diff[3] = { "Cautious", "Standard", "Pandemic" };
            int d = (int)s->difficulty;
            if (UI_Selector(id++, (Rectangle){ cx, cy, rw, rh }, "Default difficulty", &d, diff, 3)) s->difficulty = (Difficulty)d;
            cy += gap;
            UI_Slider(id++, (Rectangle){ cx, cy, rw, rh }, "Screen shake", &s->shake, 0.0f, 1.5f, "%.0f%%"); cy += gap;
            UI_Slider(id++, (Rectangle){ cx, cy, rw, rh }, "UI scale", &s->uiScale, 0.75f, 1.35f, "%.0f%%"); cy += gap + 6;
            Header("ACCESSIBILITY", cx, cy); cy += 28;
            UI_Toggle(id++, (Rectangle){ cx, cy, rw, rh }, "Reduce flashing", &s->reduceFlashing); cy += gap;
            UI_Toggle(id++, (Rectangle){ cx, cy, rw, rh }, "High-contrast UI accent", &s->highContrast); cy += gap + 10;
            if (UI_Button(id++, (Rectangle){ cx, cy, 260, 34 }, "Replay tutorial tips", true)) {
                memset(G.profile.tutorials, 0, sizeof(G.profile.tutorials));
                HUD_FloatText("Tutorial tips will show again", UI_Accent());
            }
            if (UI_Button(id++, (Rectangle){ cx + 290, cy, 260, 34 }, "Restore defaults", true)) {
                Settings_Default(s);
                SettingsUI_Apply(true);
            }
        } break;
    }
    if (UI_Button(60, (Rectangle){ x + pw - 184, y + ph - 54, 160, 38 }, "BACK", true) || (Input_Back() && !wasRebinding)) {
        G.profile.settings = *s;
        Save_WriteProfile();
        Sfx_Play(SFX_UI_BACK, 0.6f, 1.0f);
        Game_CloseOverlay();
    }
}

/*
 * input.c - maps keyboard, mouse and gamepad onto one InputState.
 * Edge-triggered actions (dash, burst, ...) are latched until the next
 * simulation step consumes them, so a 144 Hz frame never loses a press
 * that happened between 60 Hz ticks.
 */
#include "core/input.h"
#include "gfx/camera.h"
#include "core/autotest.h"

#define PAD 0
#define DEADZONE 0.18f

static float Dz(float v) { return fabsf(v) < DEADZONE ? 0.0f : (v - (v > 0 ? DEADZONE : -DEADZONE)) / (1.0f - DEADZONE); }

void Input_Poll(InputState *in, bool gameplay)
{
    int *k = G.settings.keys;
    bool pad = IsGamepadAvailable(PAD);
    in->moveX = (float)(IsKeyDown(k[ACT_RIGHT]) - IsKeyDown(k[ACT_LEFT]));
    in->moveY = (float)(IsKeyDown(k[ACT_FORWARD]) - IsKeyDown(k[ACT_BACK]));
    in->moveZ = (float)(IsKeyDown(k[ACT_UP]) - IsKeyDown(k[ACT_DOWN]));
    in->fire = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in->infect = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    in->firePressed |= IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in->infectPressed |= IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    in->dash |= IsKeyPressed(k[ACT_DASH]);
    in->burst |= IsKeyPressed(k[ACT_BURST]);
    in->swarm |= IsKeyPressed(k[ACT_SWARM]);
    in->cloak |= IsKeyPressed(k[ACT_CLOAK]);
    in->mutations |= IsKeyPressed(k[ACT_MUTATIONS]);
    in->pause |= IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P);

    float sens = 0.0022f * G.settings.sensitivity;
    float inv = G.settings.invertY ? -1.0f : 1.0f;
    Vector2 md = GetMouseDelta();
    in->lookX = gameplay ? md.x * sens : 0.0f;
    in->lookY = gameplay ? md.y * sens * inv : 0.0f;
    if (fabsf(md.x) + fabsf(md.y) > 0.5f) in->usingGamepad = false;

    if (pad) {
        float lx = Dz(GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_LEFT_X));
        float ly = Dz(GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_LEFT_Y));
        float rx = Dz(GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_RIGHT_X));
        float ry = Dz(GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_RIGHT_Y));
        float lt = GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_LEFT_TRIGGER);
        float rt = GetGamepadAxisMovement(PAD, GAMEPAD_AXIS_RIGHT_TRIGGER);
        if (fabsf(lx) + fabsf(ly) + fabsf(rx) + fabsf(ry) > 0.0f) in->usingGamepad = true;
        in->moveX += lx;
        in->moveY -= ly;
        if (IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) in->moveZ += 1.0f;
        if (IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) in->moveZ -= 1.0f;
        if (gameplay) {
            float gs = 2.6f * G.settings.sensitivity * GetFrameTime();
            in->lookX += rx * fabsf(rx) * gs;
            in->lookY += ry * fabsf(ry) * gs * inv;
        }
        bool rtDown = rt > 0.3f || IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_RIGHT_TRIGGER_2);
        bool ltDown = lt > 0.3f || IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_TRIGGER_2);
        static bool rtPrev = false, ltPrev = false;
        if (rtDown && !rtPrev) in->firePressed = true;
        if (ltDown && !ltPrev) in->infectPressed = true;
        rtPrev = rtDown;
        ltPrev = ltDown;
        in->fire |= rtDown;
        in->infect |= ltDown;
        in->dash |= IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        in->burst |= IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
        in->swarm |= IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        in->cloak |= IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        in->mutations |= IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_MIDDLE_LEFT);
        in->pause |= IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    }
    if (G.debug.autotest && gameplay) Autotest_Input(in);
    in->moveX = Clamp(in->moveX, -1.0f, 1.0f);
    in->moveY = Clamp(in->moveY, -1.0f, 1.0f);
    in->moveZ = Clamp(in->moveZ, -1.0f, 1.0f);
}

void Input_ClearEdges(InputState *in)
{
    in->firePressed = in->infectPressed = false;
    in->dash = in->burst = in->swarm = in->cloak = false;
}

bool Input_Back(void)
{
    if (G.ui.clickCooldown > 0.0f) return false;   /* the key that opened this screen must not close it */
    return IsKeyPressed(KEY_ESCAPE) || (IsGamepadAvailable(PAD) && IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT));
}

bool Input_Confirm(void)
{
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || (IsGamepadAvailable(PAD) && IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
}

int Input_AnyKeyPressed(void) { return GetKeyPressed(); }

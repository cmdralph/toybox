/*
 * settings_ui.h - video / audio / controls / gameplay settings with
 * key rebinding. Changes apply immediately and persist to the profile.
 */
#ifndef PZ_SETTINGS_UI_H
#define PZ_SETTINGS_UI_H
#include "core/game.h"

void SettingsUI_Draw(void);
void SettingsUI_Apply(bool resolutionChanged);

#endif

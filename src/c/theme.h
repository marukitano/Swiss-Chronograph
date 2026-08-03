// Consistent Light/Dark/Shake theme handling for Swiss Chronograph.

#pragma once

#include <pebble.h>

#include "config_defs.h"

void theme_init(void);
void theme_set_mode(ThemeMode mode);
bool theme_toggle(void);

bool theme_is_light(void);
bool theme_shake_enabled(void);

GColor theme_background_color(void);
GColor theme_foreground_color(void);

// Compatibility helper for remaining legacy drawing code.
// It swaps exact black/white according to the active theme.
GColor theme_color(GColor color);

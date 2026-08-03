// Consistent Light/Dark/Shake theme handling for Swiss Chronograph.

#include "theme.h"

#include "persist_keys.h"

static ThemeMode s_theme_mode = ThemeMode_Shake;
static bool s_saved_shake_light;
static bool s_theme_light;

void theme_init(void) {
    s_saved_shake_light =
        persist_exists(PERSIST_KEY_THEME_LIGHT)
        && persist_read_int(PERSIST_KEY_THEME_LIGHT) != 0;

    s_theme_light = s_saved_shake_light;
}

void theme_set_mode(ThemeMode mode) {
    s_theme_mode = mode;

    switch (mode) {
    case ThemeMode_Light:
        s_theme_light = true;
        break;

    case ThemeMode_Dark:
        s_theme_light = false;
        break;

    case ThemeMode_Shake:
    default:
        s_theme_mode = ThemeMode_Shake;
        s_theme_light = s_saved_shake_light;
        break;
    }
}

bool theme_toggle(void) {
    if (s_theme_mode != ThemeMode_Shake) {
        return false;
    }

    s_theme_light = !s_theme_light;
    s_saved_shake_light = s_theme_light;

    const int result = persist_write_int(
        PERSIST_KEY_THEME_LIGHT,
        s_saved_shake_light ? 1 : 0
    );

    if (result < 0) {
        APP_LOG(
            APP_LOG_LEVEL_ERROR,
            "Failed to persist theme state: %d",
            result
        );
    }

    return true;
}

bool theme_is_light(void) {
    return s_theme_light;
}

bool theme_shake_enabled(void) {
    return s_theme_mode == ThemeMode_Shake;
}

GColor theme_background_color(void) {
    return s_theme_light ? GColorWhite : GColorBlack;
}

GColor theme_foreground_color(void) {
    return s_theme_light ? GColorBlack : GColorWhite;
}

GColor theme_color(GColor color) {
    if (!s_theme_light) {
        return color;
    }

    if (color.argb == GColorBlack.argb) {
        return GColorWhite;
    }

    if (color.argb == GColorWhite.argb) {
        return GColorBlack;
    }

    return color;
}

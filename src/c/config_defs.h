// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// App-specific definitions of config options, for inclusion by config.h

#pragma once

#include <pebble.h>

#include "macros.h"

/** The persist data version for dealing with backwards-incompatible changes.
    This doesn't need to change as long as you only append new fields
    and don't change the messageKeys or meaning of existing data.
    Note lowest allowed value is 1.
*/
#define PERSIST_CONFIG_VERSION (2)

/** Macro table defining all config data.

    - Conftype must match one of the RECEIVE_CONFIG_ options in config.c.
    - CType is the C-side storage type.
    - Message key must match config.json and package.json.
    - Default value must match config.json.
*/
#define X_CONFIG_OPTIONS(MACRO) \
    /* conftype, C type,              message key,                  default */ \
    MACRO(COLOR, GColor,              ringColorRemaining,           PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite)) \
    MACRO(ENUM,  ThemeMode,           themeMode,                    ThemeMode_Shake) \
    MACRO(ENUM,  AlarmVibePattern,    alarmVibePattern,             AlarmVibePattern_Double) \
    MACRO(INT,   uint8_t,             audioVolume,                  0) \
    MACRO(BOOL,  bool,                showCentiseconds,             false) \
/* end of X_CONFIG_OPTIONS */

// Overall display behaviour.
typedef enum ThemeMode {
    ThemeMode_Light = 0,
    ThemeMode_Dark = 1,
    ThemeMode_Shake = 2,
} ThemeMode;

// Alarm vibration pattern.
typedef enum AlarmVibePattern {
    AlarmVibePattern_Double = 0,
    AlarmVibePattern_Short = 1,
    AlarmVibePattern_Long = 2,
    AlarmVibePattern_None = 3,
} AlarmVibePattern;

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

    - Conftype must match one of the RECEIVE_CONFIG_ options in config.c
    - CType is the C-side storage type.
    - Message key must match config.json and package.json, and be a valid C identifier.
    - Default value must match config.json.
*/
#define X_CONFIG_OPTIONS(MACRO) \
    /* conftype, C type,              message key,                  default */ \
    MACRO(COLOR, GColor,              ringColorRemaining,           PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite)) \
    MACRO(ENUM,  ThemeMode,           themeMode,                    ThemeMode_Shake) \
/*how long you have to start the second touch before it cancels*/ \
/*minimum duration for touches on the outer ring to register*/ \
    MACRO(ENUM,  AlarmVibePattern,    alarmVibePattern,             AlarmVibePattern_Double) \
/*minimum duration for lifts to register setting hours*/\
    MACRO(INT,   uint8_t,             audioVolume,                  0) \
    MACRO(BOOL,  bool,                showCentiseconds,             false) \
/* end of X_CONFIG_OPTIONS */

// The maximum value of shortAlarmMinutes and shortStopwatchMinutes, in config.json
#define MAX_POWER_SAVING_THRESHOLD (16)

// Overall display behaviour.
typedef enum ThemeMode {
    ThemeMode_Light = 0,
    ThemeMode_Dark = 1,
    ThemeMode_Shake = 2,
} ThemeMode;

// The meaning of the inner/outer ring for the initial touch

// What values the touch timer affects

// Alarm vibe pattern
typedef enum AlarmVibePattern {
    AlarmVibePattern_Double = 0,
    AlarmVibePattern_Short = 1,
    AlarmVibePattern_Long = 2,
    AlarmVibePattern_None = 3,
} AlarmVibePattern;

// How many touches are required when setting the timer duration

// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include <pebble.h>

#if PBL_TOUCH


typedef void (*TouchSelectionCallback)(bool is_duration, uint8_t hours, uint8_t minutes, uint8_t seconds);
typedef void (*TouchAutoStartCallback)(void);
typedef void (*TouchRunActionCallback)(void);

void touch_create(Layer* parent, TouchSelectionCallback callback, TouchServiceHandler handler, TouchAutoStartCallback auto_start_callback);
void touch_destroy(void);
void touch_enable(bool enable);
bool touch_in_progress(void);
void touch_adjust_minutes(int delta);
void touch_start_running(uint32_t duration_seconds);
void touch_restore_running(
    uint32_t remaining_seconds,
    bool paused
);
bool touch_running_screen_active(void);
void touch_set_paused(bool paused);
bool touch_run_action_press(
    TouchRunActionCallback callback
);
void touch_run_action_release(void);
bool touch_minimize_action_press(void);
void touch_minimize_action_release(
    TouchRunActionCallback callback
);
void touch_reset_idle(void);


#endif // PBL_TOUCH

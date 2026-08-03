// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).
// Copyright (c) 2026 Maru Kitano. Modifications for Swiss Chronograph.
// SPDX-License-Identifier: GPL-3.0-only

// Swiss Chronograph - Emery-only countdown controller.
//
// The original Instant Timer project contained a stopwatch mode, several
// duration-editing modes, a hidden legacy text UI and multi-platform action
// bars. Swiss Chronograph uses none of those. This file contains only the controller
// required by the Pebble Time 2 touch interface.

#include <pebble.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define DEBUG (0 || PBL_DEBUG)

#include "config.h"
#include "macros.h"
#include "persist_keys.h"
#include "theme.h"
#include "touch.h"

#define TIMER_STATE_VERSION 4

#define ALARM_ACTIVE_SECONDS 60
#define ALARM_VIBE_INTERVAL_MS 2000

#define ALARM_AUDIO_BUFFER_SIZE 1024
#define ALARM_AUDIO_PUMP_INTERVAL_MS 10
#define ALARM_AUDIO_MAX_WRITES_PER_PUMP 8

#define SIDE_BUTTON_HOLD_DELAY_MS 700
#define SIDE_BUTTON_REPEAT_MS 143

#define THEME_SHAKE_DEBOUNCE_MS 700

typedef struct TimerState {
    time_t duration_seconds;
    time_t start_time;
    time_t elapsed_seconds;
    bool is_running;
    bool alarm_done;
    WakeupId wakeup_id;
} TimerState;

STATIC_ASSERT(sizeof(TimerState) <= PERSIST_DATA_MAX_LENGTH);

static Window *s_main_window;
static Layer *s_background_layer;

static TimerState s_state = {
    .duration_seconds = 0,
    .start_time = 0,
    .elapsed_seconds = 0,
    .is_running = false,
    .alarm_done = false,
    .wakeup_id = E_DOES_NOT_EXIST,
};

static bool s_should_persist;

static bool s_alarm_active;
static time_t s_alarm_stop_time;
static AppTimer *s_alarm_pulse_timer;

static AppTimer *s_alarm_audio_pump_timer;
static ResHandle s_alarm_audio_resource;
static size_t s_alarm_audio_resource_size;
static size_t s_alarm_audio_resource_offset;
static uint8_t s_alarm_audio_buffer[ALARM_AUDIO_BUFFER_SIZE];
static size_t s_alarm_audio_buffer_size;
static size_t s_alarm_audio_buffer_offset;
static bool s_alarm_audio_active;

static AppTimer *s_side_button_hold_timer;
static int s_side_button_hold_delta;

static AppTimer *s_theme_shake_debounce_timer;

static bool s_select_run_screen_press;


/******************************************************************************
 * Timer state and persistence
 ******************************************************************************/

static void timer_state_clear(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.wakeup_id = E_DOES_NOT_EXIST;
}

static void timer_update_elapsed(void) {
    if (!s_state.is_running) {
        return;
    }

    const time_t now = time(NULL);

    if (now >= s_state.start_time) {
        s_state.elapsed_seconds =
            now - s_state.start_time;
    }
}

static time_t timer_remaining_seconds(void) {
    if (s_state.elapsed_seconds >= s_state.duration_seconds) {
        return 0;
    }

    return (
        s_state.duration_seconds
        - s_state.elapsed_seconds
    );
}

static bool timer_state_load(void) {
    if (
        persist_read_int(PERSIST_KEY_TIMER_VERSION)
        != TIMER_STATE_VERSION
    ) {
        return false;
    }

    const int result = persist_read_data(
        PERSIST_KEY_TIMER_STATE,
        &s_state,
        sizeof(s_state)
    );

    return result == (int)sizeof(s_state);
}

static void timer_state_save(void) {
    const int state_result = persist_write_data(
        PERSIST_KEY_TIMER_STATE,
        &s_state,
        sizeof(s_state)
    );

    if (state_result != (int)sizeof(s_state)) {
        LOG(
            "Could not persist timer state: %d",
            state_result
        );
        return;
    }

    const int version_result = persist_write_int(
        PERSIST_KEY_TIMER_VERSION,
        TIMER_STATE_VERSION
    );

    if (version_result != (int)sizeof(int32_t)) {
        LOG(
            "Could not persist timer version: %d",
            version_result
        );
    }
}

static void timer_state_delete(void) {
    (void)persist_delete(PERSIST_KEY_TIMER_STATE);
    (void)persist_delete(PERSIST_KEY_TIMER_VERSION);
}


/******************************************************************************
 * Wakeup handling
 ******************************************************************************/

static void timer_cancel_wakeup(void) {
    if (s_state.wakeup_id >= 0) {
        wakeup_cancel(s_state.wakeup_id);
    }

    s_state.wakeup_id = E_DOES_NOT_EXIST;
}

static void timer_schedule_wakeup(void) {
    timer_cancel_wakeup();

    if (
        !s_state.is_running
        || s_state.duration_seconds <= 0
    ) {
        return;
    }

    time_t wakeup_time =
        s_state.start_time + s_state.duration_seconds;

    const time_t now = time(NULL);

    if (wakeup_time <= now) {
        return;
    }

    WakeupId wakeup_id;

    do {
        wakeup_id = wakeup_schedule(
            wakeup_time,
            0,
            true
        );

        if (wakeup_id == E_RANGE) {
            wakeup_time--;
        }
    } while (
        wakeup_id == E_RANGE
        && wakeup_time > now
    );

    if (wakeup_id >= 0) {
        s_state.wakeup_id = wakeup_id;
    } else {
        LOG(
            "Could not schedule timer wakeup: %ld",
            (long)wakeup_id
        );
    }
}


/******************************************************************************
 * Streamed alarm audio
 ******************************************************************************/

static void alarm_audio_reset_state(void) {
    s_alarm_audio_resource = NULL;
    s_alarm_audio_resource_size = 0;
    s_alarm_audio_resource_offset = 0;
    s_alarm_audio_buffer_size = 0;
    s_alarm_audio_buffer_offset = 0;
    s_alarm_audio_active = false;
}

static void alarm_audio_stop(void) {
    if (s_alarm_audio_pump_timer != NULL) {
        app_timer_cancel(s_alarm_audio_pump_timer);
        s_alarm_audio_pump_timer = NULL;
    }

    if (s_alarm_audio_active) {
        speaker_stop();
    }

    alarm_audio_reset_state();
}

static bool alarm_audio_load_next_chunk(void) {
    if (
        s_alarm_audio_resource == NULL
        || s_alarm_audio_resource_size == 0
    ) {
        return false;
    }

    if (
        s_alarm_audio_resource_offset
        >= s_alarm_audio_resource_size
    ) {
        s_alarm_audio_resource_offset = 0;
    }

    const size_t bytes_remaining =
        s_alarm_audio_resource_size
        - s_alarm_audio_resource_offset;

    const size_t bytes_requested =
        MIN(
            bytes_remaining,
            sizeof(s_alarm_audio_buffer)
        );

    const size_t bytes_loaded =
        resource_load_byte_range(
            s_alarm_audio_resource,
            (uint32_t)s_alarm_audio_resource_offset,
            s_alarm_audio_buffer,
            bytes_requested
        );

    if (bytes_loaded == 0) {
        return false;
    }

    s_alarm_audio_buffer_size = bytes_loaded;
    s_alarm_audio_buffer_offset = 0;
    return true;
}

static void alarm_audio_pump(void *context);

static void alarm_audio_schedule_pump(void) {
    if (
        !s_alarm_audio_active
        || s_alarm_audio_pump_timer != NULL
    ) {
        return;
    }

    s_alarm_audio_pump_timer = app_timer_register(
        ALARM_AUDIO_PUMP_INTERVAL_MS,
        alarm_audio_pump,
        NULL
    );

    if (s_alarm_audio_pump_timer == NULL) {
        alarm_audio_stop();
    }
}

static void alarm_audio_pump(void *context) {
    UNUSED(context);
    s_alarm_audio_pump_timer = NULL;

    if (!s_alarm_audio_active) {
        return;
    }

    for (
        uint8_t attempt = 0;
        attempt < ALARM_AUDIO_MAX_WRITES_PER_PUMP;
        attempt++
    ) {
        if (
            s_alarm_audio_buffer_offset
            >= s_alarm_audio_buffer_size
        ) {
            if (!alarm_audio_load_next_chunk()) {
                alarm_audio_stop();
                return;
            }
        }

        const size_t bytes_available =
            s_alarm_audio_buffer_size
            - s_alarm_audio_buffer_offset;

        const uint32_t bytes_written =
            speaker_stream_write(
                s_alarm_audio_buffer
                    + s_alarm_audio_buffer_offset,
                (uint32_t)bytes_available
            );

        if (bytes_written == 0) {
            break;
        }

        s_alarm_audio_buffer_offset += bytes_written;

        if (
            s_alarm_audio_buffer_offset
            >= s_alarm_audio_buffer_size
        ) {
            s_alarm_audio_resource_offset +=
                s_alarm_audio_buffer_size;

            s_alarm_audio_buffer_size = 0;
            s_alarm_audio_buffer_offset = 0;

            // alarm.pcm contains 10 seconds of sound followed by
            // 2 seconds of silence. Rewinding here creates the loop.
            if (
                s_alarm_audio_resource_offset
                >= s_alarm_audio_resource_size
            ) {
                s_alarm_audio_resource_offset = 0;
            }
        }
    }

    alarm_audio_schedule_pump();
}

static bool alarm_audio_start(void) {
    alarm_audio_stop();

    const uint8_t volume =
        config_get()->audioVolume;

    if (
        volume == 0
        || speaker_is_muted()
    ) {
        return false;
    }

    s_alarm_audio_resource =
        resource_get_handle(RESOURCE_ID_ALARM_PCM);

    s_alarm_audio_resource_size =
        resource_size(s_alarm_audio_resource);

    if (
        s_alarm_audio_resource == NULL
        || s_alarm_audio_resource_size == 0
    ) {
        alarm_audio_reset_state();
        return false;
    }

    if (
        !speaker_stream_open(
            SpeakerPcmFormat_16kHz_8bit,
            volume
        )
    ) {
        alarm_audio_reset_state();
        return false;
    }

    s_alarm_audio_active = true;
    alarm_audio_pump(NULL);

    return s_alarm_audio_active;
}


/******************************************************************************
 * Alarm lifecycle
 ******************************************************************************/

static bool alarm_screen_active(void) {
    return (
        s_alarm_active
        || (
            s_state.duration_seconds > 0
            && s_state.elapsed_seconds
                >= s_state.duration_seconds
        )
    );
}

static void alarm_stop(bool mark_done) {
    if (s_alarm_pulse_timer != NULL) {
        app_timer_cancel(s_alarm_pulse_timer);
        s_alarm_pulse_timer = NULL;
    }

    vibes_cancel();
    alarm_audio_stop();

    s_alarm_active = false;
    s_alarm_stop_time = 0;

    if (mark_done) {
        s_state.alarm_done = true;
    }
}

static void alarm_vibrate(void) {
    switch (config_get()->alarmVibePattern) {
    case AlarmVibePattern_Double:
        vibes_double_pulse();
        break;

    case AlarmVibePattern_Short:
        vibes_short_pulse();
        break;

    case AlarmVibePattern_Long:
        vibes_long_pulse();
        break;

    case AlarmVibePattern_None:
        break;

    default:
        vibes_double_pulse();
        break;
    }
}

static void alarm_pulse_timer_handler(void *context) {
    UNUSED(context);
    s_alarm_pulse_timer = NULL;

    if (!s_alarm_active) {
        return;
    }

    alarm_vibrate();

    if (!s_alarm_audio_active) {
        (void)alarm_audio_start();
    }

    if (time(NULL) < s_alarm_stop_time) {
        s_alarm_pulse_timer = app_timer_register(
            ALARM_VIBE_INTERVAL_MS,
            alarm_pulse_timer_handler,
            NULL
        );

        if (s_alarm_pulse_timer == NULL) {
            alarm_stop(true);
        }
    } else {
        alarm_stop(true);
    }
}

static bool alarm_should_start(void) {
    return (
        s_state.duration_seconds > 0
        && !s_alarm_active
        && !s_state.alarm_done
        && s_state.elapsed_seconds
            >= s_state.duration_seconds
    );
}

static void alarm_start(void) {
    if (!alarm_should_start()) {
        return;
    }

    s_alarm_active = true;
    s_alarm_stop_time =
        time(NULL) + ALARM_ACTIVE_SECONDS;

    touch_show_alarm(
        (uint32_t)s_state.duration_seconds
    );

    alarm_pulse_timer_handler(NULL);
}


/******************************************************************************
 * User-visible timer actions
 ******************************************************************************/

static void button_vibe(void) {
    static const uint32_t segments[] = {100};

    const VibePattern pattern = {
        .durations = segments,
        .num_segments = ARRAY_LENGTH(segments),
    };

    vibes_enqueue_custom_pattern(pattern);
}

static void timer_reset_idle(void) {
    alarm_stop(false);
    timer_cancel_wakeup();
    timer_state_delete();
    timer_state_clear();

    s_should_persist = false;

    touch_reset_idle();
    touch_enable(true);
}

static void timer_select_duration(
    bool is_duration,
    uint8_t hours,
    uint8_t minutes,
    uint8_t seconds
) {
    if (!is_duration) {
        return;
    }

    alarm_stop(false);
    timer_cancel_wakeup();
    timer_state_delete();

    timer_state_clear();

    s_state.duration_seconds =
        (hours * SECONDS_PER_HOUR)
        + (minutes * SECONDS_PER_MINUTE)
        + seconds;

    s_should_persist = false;
}

static void timer_start(void) {
    if (
        s_state.is_running
        || s_state.duration_seconds <= 0
        || alarm_screen_active()
    ) {
        return;
    }

    button_vibe();

    s_state.start_time = time(NULL);
    s_state.elapsed_seconds = 0;
    s_state.is_running = true;
    s_state.alarm_done = false;

    s_should_persist = true;

    touch_start_running(
        (uint32_t)s_state.duration_seconds
    );
}

static void timer_toggle_pause(void) {
    if (
        !touch_running_screen_active()
        || alarm_screen_active()
    ) {
        return;
    }

    timer_update_elapsed();

    if (s_state.is_running) {
        s_state.is_running = false;
    } else if (
        s_state.elapsed_seconds
        < s_state.duration_seconds
    ) {
        s_state.start_time =
            time(NULL) - s_state.elapsed_seconds;

        s_state.is_running = true;
    }

    button_vibe();
    touch_set_paused(!s_state.is_running);
}

static void timer_add_minute(void) {
    if (
        !touch_running_screen_active()
        || s_state.is_running
        || alarm_screen_active()
    ) {
        return;
    }

    if (
        s_state.duration_seconds
        > TIME_MAX - SECONDS_PER_MINUTE
    ) {
        return;
    }

    s_state.duration_seconds +=
        SECONDS_PER_MINUTE;

    button_vibe();
    touch_add_running_seconds(
        SECONDS_PER_MINUTE
    );
}

static void timer_delete(void) {
    if (
        !touch_running_screen_active()
        || s_state.is_running
        || alarm_screen_active()
    ) {
        return;
    }

    button_vibe();
    timer_reset_idle();
}

static void timer_exit(void) {
    timer_update_elapsed();

    if (alarm_screen_active()) {
        timer_reset_idle();
    }

    window_stack_pop(true);
}


/******************************************************************************
 * Timer and theme services
 ******************************************************************************/

static void tick_handler(
    struct tm *tick_time,
    TimeUnits units_changed
) {
    UNUSED(tick_time);
    UNUSED(units_changed);

    timer_update_elapsed();

    if (alarm_should_start()) {
        alarm_start();
    }
}

static void theme_shake_debounce_clear(
    void *context
) {
    UNUSED(context);
    s_theme_shake_debounce_timer = NULL;
}

static void apply_config(const Config *config) {
    theme_set_mode(config->themeMode);

    if (s_background_layer != NULL) {
        layer_mark_dirty(s_background_layer);
    }

    touch_refresh();
}

static void accel_tap_handler(
    AccelAxisType axis,
    int32_t direction
) {
    UNUSED(axis);
    UNUSED(direction);

    if (!theme_shake_enabled()) {
        return;
    }

    if (s_theme_shake_debounce_timer != NULL) {
        return;
    }

    if (theme_toggle()) {
        apply_config(config_get());
    }

    s_theme_shake_debounce_timer =
        app_timer_register(
            THEME_SHAKE_DEBOUNCE_MS,
            theme_shake_debounce_clear,
            NULL
        );
}


/******************************************************************************
 * Touch and hardware buttons
 ******************************************************************************/

static void handle_touch_event(
    const TouchEvent *event,
    void *context
) {
    UNUSED(event);
    UNUSED(context);
}

static void touch_auto_start(void) {
    timer_start();
}

static void side_button_hold_cancel(void) {
    if (s_side_button_hold_timer != NULL) {
        app_timer_cancel(s_side_button_hold_timer);
        s_side_button_hold_timer = NULL;
    }

    s_side_button_hold_delta = 0;
}

static void side_button_hold_tick(void *context) {
    UNUSED(context);
    s_side_button_hold_timer = NULL;

    if (
        s_side_button_hold_delta == 0
        || touch_running_screen_active()
    ) {
        side_button_hold_cancel();
        return;
    }

    touch_adjust_minutes(
        s_side_button_hold_delta
    );

    s_side_button_hold_timer =
        app_timer_register(
            SIDE_BUTTON_REPEAT_MS,
            side_button_hold_tick,
            NULL
        );

    if (s_side_button_hold_timer == NULL) {
        s_side_button_hold_delta = 0;
    }
}

static void side_button_hold_start(int delta) {
    side_button_hold_cancel();

    if (
        delta == 0
        || touch_running_screen_active()
    ) {
        return;
    }

    s_side_button_hold_delta = delta;

    s_side_button_hold_timer =
        app_timer_register(
            SIDE_BUTTON_HOLD_DELAY_MS,
            side_button_hold_tick,
            NULL
        );

    if (s_side_button_hold_timer == NULL) {
        s_side_button_hold_delta = 0;
    }
}

static void up_button_down(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

    if (touch_running_screen_active()) {
        if (
            !s_state.is_running
            && !alarm_screen_active()
        ) {
            timer_add_minute();
        }

        touch_enable(true);
        return;
    }

    touch_adjust_minutes(1);
    side_button_hold_start(1);
    touch_enable(true);
}

static void down_button_down(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

    if (touch_running_screen_active()) {
        if (
            !s_state.is_running
            && !alarm_screen_active()
        ) {
            timer_delete();
        }

        touch_enable(true);
        return;
    }

    touch_adjust_minutes(-1);
    side_button_hold_start(-1);
    touch_enable(true);
}

static void side_button_up(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

    side_button_hold_cancel();
}

static void select_button_down(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

    if (
        touch_running_screen_active()
        && !alarm_screen_active()
    ) {
        s_select_run_screen_press = true;

        (void)touch_run_action_press(
            timer_toggle_pause
        );

        return;
    }

    s_select_run_screen_press = false;
}

static void select_button_up(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

    if (s_select_run_screen_press) {
        s_select_run_screen_press = false;
        touch_run_action_release();
        touch_enable(true);
        return;
    }

    if (alarm_screen_active()) {
        timer_reset_idle();
    } else if (!touch_running_screen_active()) {
        timer_start();
    }

    touch_enable(true);
}

static void back_button_released(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

    if (
        touch_running_screen_active()
        && !alarm_screen_active()
    ) {
        if (touch_minimize_action_press()) {
            touch_minimize_action_release(
                timer_exit
            );
            touch_enable(true);
            return;
        }
    }

    timer_exit();
}

static void click_config_provider(void *context) {
    UNUSED(context);

    window_raw_click_subscribe(
        BUTTON_ID_UP,
        up_button_down,
        side_button_up,
        NULL
    );

    window_raw_click_subscribe(
        BUTTON_ID_DOWN,
        down_button_down,
        side_button_up,
        NULL
    );

    window_raw_click_subscribe(
        BUTTON_ID_SELECT,
        select_button_down,
        select_button_up,
        NULL
    );

    window_multi_click_subscribe(
        BUTTON_ID_BACK,
        1,
        1,
        0,
        true,
        back_button_released
    );
}


/******************************************************************************
 * Window lifecycle
 ******************************************************************************/

static void draw_background(
    Layer *layer,
    GContext *context
) {
    graphics_context_set_fill_color(
        context,
        theme_background_color()
    );

    graphics_fill_rect(
        context,
        layer_get_bounds(layer),
        0,
        GCornerNone
    );
}

static void restore_saved_timer(void) {
    const bool loaded = timer_state_load();

    timer_state_delete();

    if (!loaded) {
        timer_state_clear();
        s_should_persist = false;
        return;
    }

    timer_update_elapsed();
    timer_cancel_wakeup();

    const bool wakeup_alarm_due =
        launch_reason() == APP_LAUNCH_WAKEUP
        && alarm_should_start();

    if (wakeup_alarm_due) {
        s_should_persist = false;

        touch_restore_running(
            0,
            false
        );

        alarm_start();
        return;
    }

    const time_t remaining =
        timer_remaining_seconds();

    if (
        s_state.duration_seconds > 0
        && remaining > 0
    ) {
        s_should_persist = true;

        touch_restore_running(
            (uint32_t)remaining,
            !s_state.is_running
        );

        return;
    }

    timer_state_clear();
    s_should_persist = false;
}

static void persist_timer_before_exit(void) {
    timer_update_elapsed();

    const bool unfinished =
        s_should_persist
        && s_state.duration_seconds > 0
        && s_state.elapsed_seconds
            < s_state.duration_seconds;

    if (!unfinished) {
        timer_cancel_wakeup();
        timer_state_delete();
        return;
    }

    if (s_state.is_running) {
        timer_schedule_wakeup();
    } else {
        timer_cancel_wakeup();
    }

    timer_state_save();
}

static void main_window_load(Window *window) {
    config_init(apply_config);

    Layer *root =
        window_get_root_layer(window);

    s_background_layer =
        layer_create(layer_get_bounds(root));

    layer_set_update_proc(
        s_background_layer,
        draw_background
    );

    layer_add_child(
        root,
        s_background_layer
    );

    touch_create(
        root,
        timer_select_duration,
        handle_touch_event,
        touch_auto_start
    );

    restore_saved_timer();

    tick_timer_service_subscribe(
        SECOND_UNIT,
        tick_handler
    );

    accel_tap_service_subscribe(
        accel_tap_handler
    );

    apply_config(config_get());
}

static void main_window_unload(Window *window) {
    UNUSED(window);

    side_button_hold_cancel();

    if (s_theme_shake_debounce_timer != NULL) {
        app_timer_cancel(
            s_theme_shake_debounce_timer
        );

        s_theme_shake_debounce_timer = NULL;
    }

    tick_timer_service_unsubscribe();
    accel_tap_service_unsubscribe();

    persist_timer_before_exit();
    alarm_stop(false);

    touch_destroy();

    if (s_background_layer != NULL) {
        layer_destroy(s_background_layer);
        s_background_layer = NULL;
    }

    config_deinit();
}

static void init(void) {
    theme_init();

    s_main_window = window_create();

    window_set_click_config_provider(
        s_main_window,
        click_config_provider
    );

    window_set_window_handlers(
        s_main_window,
        (WindowHandlers) {
            .load = main_window_load,
            .unload = main_window_unload,
        }
    );

    window_stack_push(
        s_main_window,
        true
    );
}

static void deinit(void) {
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}

// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).
// Vertical timer-ruler interaction by Maru Kitano, 2026.

#include "touch.h"

#include <pebble.h>

#if PBL_TOUCH

#include "config.h"
#include "macros.h"
#include "theme.h"

#define PIXELS_PER_MINUTE 10

#define MINUTE_DETENT_THRESHOLD_PX (PIXELS_PER_MINUTE / 2)
#define RULER_ROLL_FRAME_MS 16
#define RULER_ROLL_STRENGTH_NUM 48
#define RULER_ROLL_STRENGTH_DEN 100

#define MIN_MINUTES 0
#define MAX_MINUTES 180
#define RULER_WIDTH 58
#define TICK_SPACING 12
#define VISIBLE_TICKS 16

#define READOUT_Y 36

#define ARROW_COUNT 4
#define ARROW_STEP_Y 16
#define ARROW_HALF_HEIGHT 7
#define ARROW_ANIM_MS 120
#define ARROW_BOUNCE_MS 28
#define ARROW_BOUNCE_MAX_TICKS 36
#define ARROW_BOUNCE_DAMPING_NUM 72
#define ARROW_BOUNCE_DAMPING_DEN 100
#define ARROW_FILL_MS 120
#define RUNNING_FRAME_MS 50
#define ALARM_INVERT_INTERVAL_MS 1000
#define RUNNING_TRANSITION_MS 24
#define RUNNING_TRANSITION_STEPS 12
#define RUN_ACTION_MARKER_WIDTH 5
#define RUN_ACTION_MARKER_HEIGHT 52
#define RUN_ACTION_ANIM_MS 28
#define RUN_ACTION_PRESS_OFFSET 2
#define RUN_ACTION_PRESSED_WIDTH (RUN_ACTION_MARKER_WIDTH + RUN_ACTION_PRESS_OFFSET)
#define RUN_MINIMIZE_CENTER_Y 36
#define RUN_MINIMIZE_ANIM_MS 55
#define RUN_MINIMIZE_EXIT_DELAY_MS 90
#define RUN_PAUSE_CONTROL_TOP_CENTER_Y 36
#define RUN_PAUSE_CONTROL_BOTTOM_MARGIN 36
#define RUN_CONTROLS_ENTRY_DELAY_MS 500
#define RUN_CONTROLS_BOUNCE_MS 42
#define RUN_CONTROLS_HIDDEN_OFFSET 30
#define PPF_DIGIT_COUNT 10
#define PPF_DIGIT_WIDTH 24
#define PPF_DIGIT_HEIGHT 21
#define PPF_DIGIT_STRIDE 26
#define PPF_CHARACTER_GAP 2
#define PPF_COLON_WIDTH 3
#define PPF_COLON_DOT_SIZE 3
#define PPF_COLON_TOP_Y 5
#define PPF_COLON_BOTTOM_Y 13

#define BRANDING_GAP_Y 3
#define TINY_GLYPH_WIDTH 3
#define TINY_GLYPH_HEIGHT 5
#define TINY_GLYPH_ROTATED_WIDTH TINY_GLYPH_HEIGHT
#define TINY_GLYPH_ADVANCE_Y 4
#define EMBLEM_WIDTH 14
#define EMBLEM_HEIGHT 11

static Layer *s_layer;
static GBitmap *s_ppf_digit_sheet;
static GBitmap *s_ppf_digits[PPF_DIGIT_COUNT];
static GBitmap *s_ppf_digit_sheet_white;
static GBitmap *s_ppf_digits_white[PPF_DIGIT_COUNT];
static GBitmap *s_ppf_digit_sheet_alarm;
static GBitmap *s_ppf_digits_alarm[PPF_DIGIT_COUNT];
static TouchSelectionCallback s_callback;
static TouchServiceHandler s_parent_handler;
static TouchAutoStartCallback s_auto_start_callback;

static bool s_touch_is_enabled;
static bool s_touching;
static int16_t s_last_y;
static int16_t s_drag_accumulator;
static int16_t s_selected_minutes;

static AppTimer *s_ruler_roll_timer;
static int16_t s_ruler_roll_offset;
static AppTimer *s_arrow_anim_timer;
static bool s_arrow_animation_started;
static void start_arrow_animation(void);
static void stop_arrow_animation(void);
static void cancel_arrow_fill(void);
static uint8_t s_arrow_anim_phase;
static AppTimer *s_arrow_bounce_timer;
static int16_t s_arrow_drag_offset;
static int16_t s_arrow_bounce_velocity;
static uint8_t s_arrow_bounce_ticks;
static AppTimer *s_arrow_fill_timer;
static uint8_t s_arrow_fill_count;
static int8_t s_arrow_fill_wave_index;
static bool s_running_screen;
static bool s_running_alarm_display;
static bool s_running_transition;
static uint8_t s_running_transition_step;
static uint32_t s_running_duration_ms;
static uint32_t s_running_started_ms;
static bool s_running_paused;
static uint32_t s_running_paused_remaining_ms;
static AppTimer *s_run_action_anim_timer;
static uint8_t s_run_action_draw_width = RUN_ACTION_MARKER_WIDTH;
static bool s_run_action_button_down;
static bool s_run_action_action_fired;
static TouchRunActionCallback s_run_action_callback;
static AppTimer *s_minimize_action_anim_timer;
static uint8_t s_minimize_action_draw_width = RUN_ACTION_MARKER_WIDTH;
static bool s_minimize_action_button_down;
static bool s_minimize_action_pressed;
static TouchRunActionCallback s_minimize_action_release_callback;
static AppTimer *s_pause_controls_anim_timer;
static uint8_t s_pause_controls_anim_step;
static int16_t s_pause_controls_offset =
    RUN_CONTROLS_HIDDEN_OFFSET;
static bool s_pause_controls_visible;
static AppTimer *s_run_controls_entry_timer;
static uint8_t s_run_controls_entry_step;
static int16_t s_run_controls_entry_offset =
    RUN_CONTROLS_HIDDEN_OFFSET;
static bool s_run_controls_entry_visible;
static const int8_t s_control_bounce_offsets[] = {
    RUN_CONTROLS_HIDDEN_OFFSET,
    18,
    8,
    -3,
    2,
    -1,
    0
};
static AppTimer *s_running_frame_timer;
static AppTimer *s_running_transition_timer;

static int16_t clamp_minutes(int16_t minutes) {
    return MIN(MAX(minutes, MIN_MINUTES), MAX_MINUTES);
}

static void publish_selection(void) {
    if (s_callback != NULL) {
        s_callback(true, 0, (uint8_t)s_selected_minutes, 0);
    }
}

static void set_minutes(int16_t minutes, bool publish) {
    const int16_t clamped = clamp_minutes(minutes);

    if (clamped == s_selected_minutes) {
        return;
    }

    s_selected_minutes = clamped;

    if (publish) {
        publish_selection();
    }

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

static void cancel_ruler_roll(void) {
    if (s_ruler_roll_timer != NULL) {
        app_timer_cancel(s_ruler_roll_timer);
        s_ruler_roll_timer = NULL;
    }

    s_ruler_roll_offset = 0;
}


static void ruler_roll_tick(void *context) {
    UNUSED(context);
    s_ruler_roll_timer = NULL;

    if (s_ruler_roll_offset == 0) {
        return;
    }

    int16_t movement =
        (
            -s_ruler_roll_offset
            * RULER_ROLL_STRENGTH_NUM
        )
        / RULER_ROLL_STRENGTH_DEN;

    if (movement == 0) {
        movement =
            s_ruler_roll_offset > 0
                ? -1
                : 1;
    }

    s_ruler_roll_offset += movement;

    if (ABS(s_ruler_roll_offset) <= 1) {
        s_ruler_roll_offset = 0;
    }

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }

    if (s_ruler_roll_offset != 0) {
        s_ruler_roll_timer = app_timer_register(
            RULER_ROLL_FRAME_MS,
            ruler_roll_tick,
            NULL
        );

        if (s_ruler_roll_timer == NULL) {
            s_ruler_roll_offset = 0;

            if (s_layer != NULL) {
                layer_mark_dirty(s_layer);
            }
        }
    }
}


static void start_ruler_roll(int direction) {
    if (direction > 0) {
        // selected minute increased: keep the old tick visually in
        // place first, then roll upward into the new minute valley.
        s_ruler_roll_offset -= TICK_SPACING;
    } else {
        // selected minute decreased: mirrored movement.
        s_ruler_roll_offset += TICK_SPACING;
    }

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }

    if (s_ruler_roll_timer == NULL) {
        s_ruler_roll_timer = app_timer_register(
            RULER_ROLL_FRAME_MS,
            ruler_roll_tick,
            NULL
        );

        if (s_ruler_roll_timer == NULL) {
            s_ruler_roll_offset = 0;

            if (s_layer != NULL) {
                layer_mark_dirty(s_layer);
            }
        }
    }
}

void touch_adjust_minutes(int delta) {
    if (s_running_screen || delta == 0) {
        return;
    }

    // Hardware Up/Down changes only the minute detent. It must not start
    // the decorative chevron wave that belongs to the touch-release
    // auto-start sequence.
    cancel_arrow_fill();
    stop_arrow_animation();

    const int16_t previous_minutes =
        s_selected_minutes;

    set_minutes(
        s_selected_minutes + delta,
        true
    );

    if (s_selected_minutes > previous_minutes) {
        start_ruler_roll(1);
    } else if (
        s_selected_minutes < previous_minutes
    ) {
        start_ruler_roll(-1);
    }
}

void touch_refresh(void) {
    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

static void arrow_anim_tick(void *context) {
    UNUSED(context);

    s_arrow_anim_phase = (s_arrow_anim_phase + 1) % ARROW_COUNT;

    if (s_arrow_animation_started
        && s_layer != NULL) {
        layer_mark_dirty(s_layer);
        s_arrow_anim_timer = app_timer_register(
            ARROW_ANIM_MS,
            arrow_anim_tick,
            NULL
        );
    } else {
        s_arrow_anim_timer = NULL;
    }
}

static void stop_arrow_animation(void) {
    if (s_arrow_anim_timer != NULL) {
        app_timer_cancel(s_arrow_anim_timer);
        s_arrow_anim_timer = NULL;
    }

    s_arrow_animation_started = false;
    s_arrow_anim_phase = 0;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

static void start_arrow_animation(void) {
    if (s_arrow_animation_started || s_layer == NULL) {
        return;
    }

    s_arrow_animation_started = true;
    s_arrow_anim_phase = 0;
    layer_mark_dirty(s_layer);

    s_arrow_anim_timer = app_timer_register(
        ARROW_ANIM_MS,
        arrow_anim_tick,
        NULL
    );

    if (s_arrow_anim_timer == NULL) {
        s_arrow_animation_started = false;
    }
}

static void draw_chevron_down(
    GContext *ctx,
    int16_t cx,
    int16_t cy,
    int16_t half_width,
    GColor color
) {
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 3);

    graphics_draw_line(
        ctx,
        GPoint(cx - half_width, cy - ARROW_HALF_HEIGHT),
        GPoint(cx, cy + ARROW_HALF_HEIGHT)
    );

    graphics_draw_line(
        ctx,
        GPoint(cx, cy + ARROW_HALF_HEIGHT),
        GPoint(cx + half_width, cy - ARROW_HALF_HEIGHT)
    );
}

static void start_arrow_fill(void);

static void finish_arrow_bounce(void) {
    s_arrow_drag_offset = 0;
    s_arrow_bounce_velocity = 0;
    s_arrow_bounce_ticks = 0;
    s_arrow_bounce_timer = NULL;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }

    start_arrow_fill();
}

static void arrow_bounce_tick(void *context) {
    UNUSED(context);

    s_arrow_bounce_ticks++;

    s_arrow_bounce_velocity += (-s_arrow_drag_offset * 34) / 100;
    s_arrow_bounce_velocity =
        (s_arrow_bounce_velocity * ARROW_BOUNCE_DAMPING_NUM) /
        ARROW_BOUNCE_DAMPING_DEN;

    // Integer rounding can reduce the velocity to zero while the arrow is
    // still 1-3 pixels away from the centre. Never let it visibly stop there.
    if (s_arrow_drag_offset != 0 &&
        s_arrow_bounce_velocity == 0) {
        s_arrow_bounce_velocity =
            s_arrow_drag_offset > 0 ? -1 : 1;
    }

    const int16_t previous_offset = s_arrow_drag_offset;
    s_arrow_drag_offset += s_arrow_bounce_velocity;

    // If this step crossed the centre, land exactly on zero immediately.
    // This happens during motion, so there is no later visible snap.
    const bool crossed_centre =
        (previous_offset > 0 && s_arrow_drag_offset <= 0) ||
        (previous_offset < 0 && s_arrow_drag_offset >= 0);

    if (crossed_centre) {
        s_arrow_drag_offset = 0;
        s_arrow_bounce_velocity = 0;
        finish_arrow_bounce();
        return;
    }

    if (s_arrow_drag_offset == 0) {
        s_arrow_bounce_velocity = 0;
        finish_arrow_bounce();
        return;
    }

    // Keep the safety timeout, but continue toward zero rather than snapping.
    if (s_arrow_bounce_ticks >= ARROW_BOUNCE_MAX_TICKS) {
        s_arrow_bounce_velocity =
            s_arrow_drag_offset > 0 ? -1 : 1;
    }

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
        s_arrow_bounce_timer = app_timer_register(
            ARROW_BOUNCE_MS,
            arrow_bounce_tick,
            NULL
        );
    } else {
        s_arrow_bounce_timer = NULL;
    }
}

static void start_arrow_bounce(void) {
    if (s_arrow_bounce_timer != NULL) {
        app_timer_cancel(s_arrow_bounce_timer);
        s_arrow_bounce_timer = NULL;
    }

    cancel_arrow_fill();

    s_arrow_bounce_ticks = 0;
    s_arrow_bounce_velocity = (-s_arrow_drag_offset * 24) / 100;

    if (ABS(s_arrow_drag_offset) <= 1) {
        finish_arrow_bounce();
        return;
    }

    s_arrow_bounce_timer = app_timer_register(
        ARROW_BOUNCE_MS,
        arrow_bounce_tick,
        NULL
    );
}

static void cancel_arrow_fill(void) {
    if (s_arrow_fill_timer != NULL) {
        app_timer_cancel(s_arrow_fill_timer);
        s_arrow_fill_timer = NULL;
    }

    s_arrow_fill_count = 0;
    s_arrow_fill_wave_index = -1;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

static void arrow_fill_tick(void *context) {
    UNUSED(context);
    s_arrow_fill_timer = NULL;

    if (s_touching || s_arrow_drag_offset != 0) {
        cancel_arrow_fill();
        return;
    }

    // Fill every chevron except the final one. When the final
    // chevron would start, transition into the running screen.
    if (s_arrow_fill_count >= ARROW_COUNT - 1) {
        if (s_selected_minutes > 0 &&
            s_auto_start_callback != NULL) {
            s_auto_start_callback();
        }
        return;
    }

    const int target_index =
        ARROW_COUNT - 1 - s_arrow_fill_count;

    if (s_arrow_fill_wave_index < 0) {
        s_arrow_fill_wave_index = 0;
    } else if (s_arrow_fill_wave_index < target_index) {
        s_arrow_fill_wave_index++;
    } else {
        s_arrow_fill_count++;
        s_arrow_fill_wave_index = -1;
    }

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }

    s_arrow_fill_timer = app_timer_register(
        ARROW_FILL_MS,
        arrow_fill_tick,
        NULL
    );
}

static void start_arrow_fill(void) {
    cancel_arrow_fill();

    if (s_selected_minutes <= 0 ||
        s_auto_start_callback == NULL) {
        return;
    }

    s_arrow_fill_wave_index = -1;
    s_arrow_fill_timer = app_timer_register(
        ARROW_FILL_MS,
        arrow_fill_tick,
        NULL
    );
}

static GColor arrow_color_for_index(int index) {
#if PBL_COLOR
    const GColor dim = GColorDarkGray;
    const GColor mid = GColorLightGray;
#else
    const GColor dim = theme_color(GColorBlack);
    const GColor mid = theme_foreground_color();
#endif

    if (s_arrow_fill_count > 0 ||
        s_arrow_fill_wave_index >= 0) {
        const int first_completed =
            ARROW_COUNT - s_arrow_fill_count;

        if (index >= first_completed) {
            return theme_foreground_color();
        }

        if (index == s_arrow_fill_wave_index) {
            return theme_foreground_color();
        }

        if (s_arrow_fill_wave_index > 0 &&
            index == s_arrow_fill_wave_index - 1) {
            return mid;
        }

        return dim;
    }

    if (!s_arrow_animation_started) {
        // Idle selector: gray until the start countdown begins.
        return dim;
    }

    if (index == s_arrow_anim_phase) {
        return theme_foreground_color();
    }

    if (index == ((s_arrow_anim_phase + ARROW_COUNT - 1) % ARROW_COUNT)) {
        return mid;
    }

    return dim;
}

static uint32_t monotonic_ms(void) {
    time_t seconds;
    uint16_t milliseconds;
    time_ms(&seconds, &milliseconds);
    return ((uint32_t)seconds * 1000U) + milliseconds;
}

static uint32_t running_remaining_ms(void) {
    if (!s_running_screen) {
        return 0;
    }

    if (s_running_paused) {
        return s_running_paused_remaining_ms;
    }

    const uint32_t elapsed_ms =
        monotonic_ms() - s_running_started_ms;

    return elapsed_ms >= s_running_duration_ms
        ? 0
        : s_running_duration_ms - elapsed_ms;
}

static void cancel_running_timers(void) {
    if (s_running_frame_timer != NULL) {
        app_timer_cancel(s_running_frame_timer);
        s_running_frame_timer = NULL;
    }

    if (s_running_transition_timer != NULL) {
        app_timer_cancel(s_running_transition_timer);
        s_running_transition_timer = NULL;
    }
}

static void cancel_run_action_animation(void) {
    if (s_run_action_anim_timer != NULL) {
        app_timer_cancel(s_run_action_anim_timer);
        s_run_action_anim_timer = NULL;
    }

    s_run_action_draw_width = RUN_ACTION_MARKER_WIDTH;
    s_run_action_button_down = false;
    s_run_action_action_fired = false;
    s_run_action_callback = NULL;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

static void run_action_animation_tick(void *context) {
    UNUSED(context);
    s_run_action_anim_timer = NULL;

    if (!s_running_screen || s_layer == NULL) {
        cancel_run_action_animation();
        return;
    }

    if (!s_run_action_action_fired) {
        if (s_run_action_draw_width < RUN_ACTION_PRESSED_WIDTH) {
            s_run_action_draw_width++;
        }

        if (s_run_action_draw_width >= RUN_ACTION_PRESSED_WIDTH) {
            s_run_action_action_fired = true;

            TouchRunActionCallback callback = s_run_action_callback;
            s_run_action_callback = NULL;

            if (callback != NULL) {
                callback();
            }
        }
    } else if (!s_run_action_button_down) {
        if (s_run_action_draw_width > RUN_ACTION_MARKER_WIDTH) {
            s_run_action_draw_width--;
        }
    }

    layer_mark_dirty(s_layer);

    const bool moving_out =
        !s_run_action_action_fired
        && s_run_action_draw_width < RUN_ACTION_PRESSED_WIDTH;

    const bool returning =
        s_run_action_action_fired
        && !s_run_action_button_down
        && s_run_action_draw_width > RUN_ACTION_MARKER_WIDTH;

    if (moving_out || returning) {
        s_run_action_anim_timer = app_timer_register(
            RUN_ACTION_ANIM_MS,
            run_action_animation_tick,
            NULL
        );
    }
}

static void cancel_minimize_action_animation(void) {
    if (s_minimize_action_anim_timer != NULL) {
        app_timer_cancel(s_minimize_action_anim_timer);
        s_minimize_action_anim_timer = NULL;
    }

    s_minimize_action_draw_width = RUN_ACTION_MARKER_WIDTH;
    s_minimize_action_button_down = false;
    s_minimize_action_pressed = false;
    s_minimize_action_release_callback = NULL;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

static void minimize_action_finish_tick(void *context) {
    UNUSED(context);
    s_minimize_action_anim_timer = NULL;

    TouchRunActionCallback callback =
        s_minimize_action_release_callback;

    s_minimize_action_release_callback = NULL;
    s_minimize_action_pressed = false;
    s_minimize_action_draw_width = RUN_ACTION_MARKER_WIDTH;

    if (callback != NULL) {
        callback();
    }
}

static void minimize_action_animation_tick(void *context) {
    UNUSED(context);
    s_minimize_action_anim_timer = NULL;

    if (!s_running_screen || s_layer == NULL) {
        cancel_minimize_action_animation();
        return;
    }

    if (!s_minimize_action_pressed) {
        if (s_minimize_action_draw_width
            < RUN_ACTION_PRESSED_WIDTH) {
            s_minimize_action_draw_width++;
        }

        if (s_minimize_action_draw_width
            >= RUN_ACTION_PRESSED_WIDTH) {
            s_minimize_action_pressed = true;
        }
    } else if (!s_minimize_action_button_down
        && s_minimize_action_draw_width
            > RUN_ACTION_MARKER_WIDTH) {
        s_minimize_action_draw_width--;
    }

    layer_mark_dirty(s_layer);

    if (!s_minimize_action_button_down
        && s_minimize_action_pressed
        && s_minimize_action_draw_width
            <= RUN_ACTION_MARKER_WIDTH) {
        s_minimize_action_draw_width =
            RUN_ACTION_MARKER_WIDTH;

        s_minimize_action_anim_timer = app_timer_register(
            RUN_MINIMIZE_EXIT_DELAY_MS,
            minimize_action_finish_tick,
            NULL
        );

        if (s_minimize_action_anim_timer == NULL) {
            minimize_action_finish_tick(NULL);
        }
        return;
    }

    const bool moving_in =
        !s_minimize_action_pressed
        && s_minimize_action_draw_width
            < RUN_ACTION_PRESSED_WIDTH;

    const bool moving_out =
        s_minimize_action_pressed
        && !s_minimize_action_button_down
        && s_minimize_action_draw_width
            > RUN_ACTION_MARKER_WIDTH;

    if (moving_in || moving_out) {
        s_minimize_action_anim_timer = app_timer_register(
            RUN_MINIMIZE_ANIM_MS,
            minimize_action_animation_tick,
            NULL
        );
    }
}

static void hide_pause_controls(void) {
    if (s_pause_controls_anim_timer != NULL) {
        app_timer_cancel(s_pause_controls_anim_timer);
        s_pause_controls_anim_timer = NULL;
    }

    s_pause_controls_anim_step = 0;
    s_pause_controls_offset =
        RUN_CONTROLS_HIDDEN_OFFSET;
    s_pause_controls_visible = false;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

static void pause_controls_animation_tick(void *context) {
    UNUSED(context);
    s_pause_controls_anim_timer = NULL;

    if (!s_running_screen
        || !s_running_paused
        || s_layer == NULL) {
        hide_pause_controls();
        return;
    }

    if (s_pause_controls_anim_step
        >= ARRAY_LENGTH(s_control_bounce_offsets)) {
        s_pause_controls_offset = 0;
        layer_mark_dirty(s_layer);
        return;
    }

    s_pause_controls_offset =
        s_control_bounce_offsets[
            s_pause_controls_anim_step
        ];
    s_pause_controls_anim_step++;

    layer_mark_dirty(s_layer);

    if (s_pause_controls_anim_step
        < ARRAY_LENGTH(s_control_bounce_offsets)) {
        s_pause_controls_anim_timer = app_timer_register(
            RUN_CONTROLS_BOUNCE_MS,
            pause_controls_animation_tick,
            NULL
        );
    } else {
        s_pause_controls_offset = 0;
        layer_mark_dirty(s_layer);
    }
}

static void show_pause_controls(void) {
    if (!s_running_screen
        || !s_running_paused
        || s_layer == NULL) {
        return;
    }

    if (s_pause_controls_anim_timer != NULL) {
        app_timer_cancel(s_pause_controls_anim_timer);
        s_pause_controls_anim_timer = NULL;
    }

    s_pause_controls_visible = true;
    s_pause_controls_anim_step = 1;
    s_pause_controls_offset =
        s_control_bounce_offsets[0];

    layer_mark_dirty(s_layer);

    s_pause_controls_anim_timer = app_timer_register(
        RUN_CONTROLS_BOUNCE_MS,
        pause_controls_animation_tick,
        NULL
    );

    if (s_pause_controls_anim_timer == NULL) {
        s_pause_controls_offset = 0;
        layer_mark_dirty(s_layer);
    }
}

static void hide_run_controls(void) {
    if (s_run_controls_entry_timer != NULL) {
        app_timer_cancel(s_run_controls_entry_timer);
        s_run_controls_entry_timer = NULL;
    }

    s_run_controls_entry_step = 0;
    s_run_controls_entry_offset =
        RUN_CONTROLS_HIDDEN_OFFSET;
    s_run_controls_entry_visible = false;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

static void run_controls_entry_tick(void *context) {
    UNUSED(context);
    s_run_controls_entry_timer = NULL;

    if (!s_running_screen || s_layer == NULL) {
        hide_run_controls();
        return;
    }

    if (!s_run_controls_entry_visible) {
        // The first callback is the 500 ms delay.
        s_run_controls_entry_visible = true;
        s_run_controls_entry_step = 1;
        s_run_controls_entry_offset =
            s_control_bounce_offsets[0];

        layer_mark_dirty(s_layer);

        s_run_controls_entry_timer = app_timer_register(
            RUN_CONTROLS_BOUNCE_MS,
            run_controls_entry_tick,
            NULL
        );
        return;
    }

    if (s_run_controls_entry_step
        >= ARRAY_LENGTH(s_control_bounce_offsets)) {
        s_run_controls_entry_offset = 0;
        layer_mark_dirty(s_layer);
        return;
    }

    s_run_controls_entry_offset =
        s_control_bounce_offsets[
            s_run_controls_entry_step
        ];
    s_run_controls_entry_step++;

    layer_mark_dirty(s_layer);

    if (s_run_controls_entry_step
        < ARRAY_LENGTH(s_control_bounce_offsets)) {
        s_run_controls_entry_timer = app_timer_register(
            RUN_CONTROLS_BOUNCE_MS,
            run_controls_entry_tick,
            NULL
        );
    } else {
        s_run_controls_entry_offset = 0;
        layer_mark_dirty(s_layer);
    }
}

static void schedule_run_controls_entry(void) {
    hide_run_controls();

    if (!s_running_screen || s_layer == NULL) {
        return;
    }

    s_run_controls_entry_timer = app_timer_register(
        RUN_CONTROLS_ENTRY_DELAY_MS,
        run_controls_entry_tick,
        NULL
    );

    if (s_run_controls_entry_timer == NULL) {
        s_run_controls_entry_visible = true;
        s_run_controls_entry_offset = 0;
        layer_mark_dirty(s_layer);
    }
}

static void running_frame_tick(void *context) {
    UNUSED(context);
    s_running_frame_timer = NULL;

    if (!s_running_screen || s_running_paused || s_layer == NULL) {
        return;
    }

    layer_mark_dirty(s_layer);
    s_running_frame_timer = app_timer_register(
        RUNNING_FRAME_MS,
        running_frame_tick,
        NULL
    );
}

static void draw_top_slide_transition(
    GContext *ctx,
    GRect bounds
) {
    // New running screen slides in from the top.
    // Its lower edge is a single large V with the same angle family
    // as the chevrons used in the selection arrow.
    graphics_context_set_fill_color(ctx, theme_background_color());
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    if (s_running_transition_step >= RUNNING_TRANSITION_STEPS) {
        graphics_context_set_fill_color(ctx, theme_background_color());
        graphics_fill_rect(ctx, bounds, 0, GCornerNone);
        return;
    }

    const int16_t w = bounds.size.w;
    const int16_t h = bounds.size.h;
    const int16_t center_x = w / 2;

    const int16_t screen_half_width = MAX(center_x, 1);
    const int16_t reference_arrow_half_width = MAX(w / 6, 1);

    int16_t v_depth =
        (screen_half_width * ARROW_HALF_HEIGHT) /
        reference_arrow_half_width;

    if (v_depth < ARROW_HALF_HEIGHT + 2) {
        v_depth = ARROW_HALF_HEIGHT + 2;
    }

    const int16_t max_apex_y = h + v_depth;
    const int16_t apex_y =
        (max_apex_y * s_running_transition_step) /
        RUNNING_TRANSITION_STEPS;

    graphics_context_set_stroke_color(ctx, theme_background_color());
    graphics_context_set_stroke_width(ctx, 1);

    for (int16_t x = 0; x < w; ++x) {
        const int16_t dx = ABS(x - center_x);

        int16_t edge_y =
            apex_y -
            (dx * v_depth) / screen_half_width;

        if (edge_y < 0) {
            continue;
        }

        if (edge_y > h - 1) {
            edge_y = h - 1;
        }

        graphics_draw_line(
            ctx,
            GPoint(x, 0),
            GPoint(x, edge_y)
        );
    }
}

static void running_transition_tick(void *context) {
    UNUSED(context);
    s_running_transition_timer = NULL;

    if (!s_running_screen || s_layer == NULL) {
        return;
    }

    if (s_running_transition_step >= RUNNING_TRANSITION_STEPS) {
        s_running_transition = false;
        layer_mark_dirty(s_layer);

        // Start the live countdown after the top-slide transition.
        if (!s_running_paused) {
            s_running_frame_timer = app_timer_register(
                RUNNING_FRAME_MS,
                running_frame_tick,
                NULL
            );
        }

        schedule_run_controls_entry();
        return;
    }

    s_running_transition_step++;
    layer_mark_dirty(s_layer);

    s_running_transition_timer = app_timer_register(
        RUNNING_TRANSITION_MS,
        running_transition_tick,
        NULL
    );
}

void touch_start_running(uint32_t duration_seconds) {
    if (s_layer == NULL || duration_seconds == 0) {
        return;
    }

    cancel_arrow_fill();
    stop_arrow_animation();
    cancel_run_action_animation();
    cancel_minimize_action_animation();
    hide_pause_controls();
    hide_run_controls();

    if (s_arrow_bounce_timer != NULL) {
        app_timer_cancel(s_arrow_bounce_timer);
        s_arrow_bounce_timer = NULL;
    }

    s_touching = false;
    s_running_screen = true;
    s_running_alarm_display = false;
    s_running_transition = true;
    s_running_transition_step = 0;
    s_running_duration_ms = duration_seconds * 1000U;
    s_running_started_ms = monotonic_ms();
    s_running_paused = false;
    s_running_paused_remaining_ms = s_running_duration_ms;

    if (s_touch_is_enabled) {
        touch_service_unsubscribe();
        s_touch_is_enabled = false;
    }

    cancel_running_timers();

    s_running_transition_timer = app_timer_register(
        RUNNING_TRANSITION_MS,
        running_transition_tick,
        NULL
    );

    layer_mark_dirty(s_layer);
}

void touch_restore_running(
    uint32_t remaining_seconds,
    bool paused
) {
    if (s_layer == NULL) {
        return;
    }

    cancel_arrow_fill();
    stop_arrow_animation();
    cancel_run_action_animation();
    cancel_minimize_action_animation();

    if (s_arrow_bounce_timer != NULL) {
        app_timer_cancel(s_arrow_bounce_timer);
        s_arrow_bounce_timer = NULL;
    }

    s_touching = false;
    s_running_screen = true;
    s_running_alarm_display = false;

    // Restore directly to the finished run screen. The top-slide animation
    // belongs only to a newly started timer.
    s_running_transition = false;
    s_running_transition_step = RUNNING_TRANSITION_STEPS;

    s_running_duration_ms = remaining_seconds * 1000U;
    s_running_started_ms = monotonic_ms();
    s_running_paused = paused;
    s_running_paused_remaining_ms =
        paused ? s_running_duration_ms : 0;

    if (paused) {
        show_pause_controls();
    } else {
        hide_pause_controls();
    }

    if (s_touch_is_enabled) {
        touch_service_unsubscribe();
        s_touch_is_enabled = false;
    }

    cancel_running_timers();

    if (remaining_seconds > 0) {
        schedule_run_controls_entry();

        if (!paused) {
            s_running_frame_timer = app_timer_register(
                RUNNING_FRAME_MS,
                running_frame_tick,
                NULL
            );
        }
    } else {
        // A wakeup alarm is restored at exactly 00:00. Keep the countdown
        // screen visible, but do not show normal pause/minimise controls.
        hide_run_controls();
        hide_pause_controls();
    }

    layer_mark_dirty(s_layer);
}

void touch_show_alarm(uint32_t duration_seconds) {
    if (s_layer == NULL || duration_seconds == 0) {
        return;
    }

    cancel_arrow_fill();
    stop_arrow_animation();
    cancel_run_action_animation();
    cancel_minimize_action_animation();
    hide_pause_controls();
    hide_run_controls();

    if (s_arrow_bounce_timer != NULL) {
        app_timer_cancel(s_arrow_bounce_timer);
        s_arrow_bounce_timer = NULL;
    }

    s_touching = false;
    s_running_screen = true;
    s_running_transition = false;
    s_running_transition_step = RUNNING_TRANSITION_STEPS;
    s_running_alarm_display = true;
    s_running_duration_ms = duration_seconds * 1000U;
    s_running_started_ms = monotonic_ms();
    s_running_paused = false;
    s_running_paused_remaining_ms = 0;

    if (s_touch_is_enabled) {
        touch_service_unsubscribe();
        s_touch_is_enabled = false;
    }

    cancel_running_timers();

    s_running_frame_timer = app_timer_register(
        RUNNING_FRAME_MS,
        running_frame_tick,
        NULL
    );

    layer_mark_dirty(s_layer);
}


bool touch_running_screen_active(void) {
    return s_running_screen;
}

void touch_set_paused(bool paused) {
    if (!s_running_screen || paused == s_running_paused) {
        return;
    }

    if (paused) {
        s_running_paused_remaining_ms = running_remaining_ms();
        s_running_paused = true;
        cancel_running_timers();
        show_pause_controls();
    } else {
        hide_pause_controls();
        s_running_duration_ms = s_running_paused_remaining_ms;
        s_running_started_ms = monotonic_ms();
        s_running_paused = false;

        if (s_layer != NULL) {
            if (s_running_transition) {
                s_running_transition_timer = app_timer_register(
                    RUNNING_TRANSITION_MS,
                    running_transition_tick,
                    NULL
                );
            } else {
                s_running_frame_timer = app_timer_register(
                    RUNNING_FRAME_MS,
                    running_frame_tick,
                    NULL
                );
            }
        }
    }

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

void touch_add_running_seconds(uint32_t seconds) {
    if (!s_running_screen
        || !s_running_paused
        || seconds == 0) {
        return;
    }

    const uint32_t added_ms = seconds * 1000U;

    if (s_running_paused_remaining_ms
        > UINT32_MAX - added_ms) {
        s_running_paused_remaining_ms = UINT32_MAX;
    } else {
        s_running_paused_remaining_ms += added_ms;
    }

    s_running_duration_ms =
        s_running_paused_remaining_ms;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

bool touch_run_action_press(
    TouchRunActionCallback callback
) {
    if (!s_running_screen
        || s_running_transition
        || callback == NULL
        || s_run_action_button_down
        || s_run_action_draw_width != RUN_ACTION_MARKER_WIDTH
        || s_run_action_anim_timer != NULL) {
        return false;
    }

    s_run_action_button_down = true;
    s_run_action_action_fired = false;
    s_run_action_callback = callback;

    s_run_action_draw_width++;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }

    s_run_action_anim_timer = app_timer_register(
        RUN_ACTION_ANIM_MS,
        run_action_animation_tick,
        NULL
    );

    if (s_run_action_anim_timer == NULL) {
        cancel_run_action_animation();
        return false;
    }

    return true;
}

void touch_run_action_release(void) {
    if (!s_run_action_button_down) {
        return;
    }

    s_run_action_button_down = false;

    if (s_run_action_action_fired
        && s_run_action_draw_width > RUN_ACTION_MARKER_WIDTH
        && s_run_action_anim_timer == NULL) {
        s_run_action_anim_timer = app_timer_register(
            RUN_ACTION_ANIM_MS,
            run_action_animation_tick,
            NULL
        );
    }
}

bool touch_minimize_action_press(void) {
    if (!s_running_screen
        || s_running_transition
        || s_minimize_action_button_down
        || s_minimize_action_draw_width
            != RUN_ACTION_MARKER_WIDTH
        || s_minimize_action_anim_timer != NULL) {
        return false;
    }

    s_minimize_action_button_down = true;
    s_minimize_action_pressed = false;
    s_minimize_action_release_callback = NULL;

    // First pixel immediately: the physical response feels direct.
    s_minimize_action_draw_width++;

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }

    s_minimize_action_anim_timer = app_timer_register(
        RUN_MINIMIZE_ANIM_MS,
        minimize_action_animation_tick,
        NULL
    );

    if (s_minimize_action_anim_timer == NULL) {
        cancel_minimize_action_animation();
        return false;
    }

    return true;
}

void touch_minimize_action_release(
    TouchRunActionCallback callback
) {
    if (!s_minimize_action_button_down) {
        return;
    }

    s_minimize_action_button_down = false;
    s_minimize_action_release_callback = callback;

    // A quick tap completes the outward movement first. A held press
    // starts returning as soon as the hardware button is released.
    if (s_minimize_action_pressed
        && s_minimize_action_draw_width
            > RUN_ACTION_MARKER_WIDTH
        && s_minimize_action_anim_timer == NULL) {
        s_minimize_action_anim_timer = app_timer_register(
            RUN_MINIMIZE_ANIM_MS,
            minimize_action_animation_tick,
            NULL
        );

        if (s_minimize_action_anim_timer == NULL) {
            TouchRunActionCallback fallback_callback =
                s_minimize_action_release_callback;

            cancel_minimize_action_animation();

            if (fallback_callback != NULL) {
                fallback_callback();
            }
        }
    }
}

void touch_reset_idle(void) {
    cancel_ruler_roll();
    cancel_running_timers();
    cancel_run_action_animation();
    cancel_minimize_action_animation();
    hide_pause_controls();
    hide_run_controls();

    if (s_arrow_bounce_timer != NULL) {
        app_timer_cancel(s_arrow_bounce_timer);
        s_arrow_bounce_timer = NULL;
    }

    cancel_arrow_fill();
    stop_arrow_animation();

    // Leave running mode first. The parent timer logic may ignore selection
    // callbacks while the countdown or alarm is still considered active.
    s_running_screen = false;
    s_running_alarm_display = false;
    s_running_transition = false;
    s_running_transition_step = 0;
    s_running_duration_ms = 0;
    s_running_started_ms = 0;
    s_running_paused = false;
    s_running_paused_remaining_ms = 0;

    // Now reset both the visual ruler state and the published timer value.
    s_touching = false;
    s_drag_accumulator = 0;
    s_arrow_drag_offset = 0;
    s_arrow_bounce_velocity = 0;
    s_arrow_bounce_ticks = 0;
    s_selected_minutes = 0;
    publish_selection();

    if (s_layer != NULL) {
        layer_mark_dirty(s_layer);
    }
}

static void draw_running_action_bar(
    GContext *ctx,
    GRect bounds
) {
    if (!s_run_controls_entry_visible) {
        return;
    }

    const int16_t center_y = bounds.size.h / 2;
    const int16_t edge_x =
        bounds.size.w + s_run_controls_entry_offset;

    // Stretch the black bar back to the screen edge while it bounces in.
    const int16_t extra_width =
        s_run_controls_entry_offset > 0
            ? s_run_controls_entry_offset
            : 0;
    const int16_t marker_width =
        s_run_action_draw_width + extra_width;
    const int16_t marker_x =
        bounds.size.w - marker_width;
    const int16_t marker_y =
        center_y - (RUN_ACTION_MARKER_HEIGHT / 2);

    graphics_context_set_fill_color(ctx, theme_foreground_color());
    graphics_fill_rect(
        ctx,
        GRect(
            marker_x,
            marker_y,
            marker_width,
            RUN_ACTION_MARKER_HEIGHT
        ),
        5,
        GCornersLeft
    );

    const int16_t center_x =
        edge_x - RUN_ACTION_MARKER_WIDTH - 10;

    graphics_context_set_stroke_color(ctx, theme_foreground_color());
    graphics_context_set_stroke_width(ctx, 1);

    if (s_running_paused) {
        const int16_t base_x = center_x - 5;
        const int16_t point_x = center_x + 5;

        for (int16_t x = base_x; x <= point_x; ++x) {
            const int16_t half_height =
                ((point_x - x) * 7) / (point_x - base_x);

            graphics_draw_line(
                ctx,
                GPoint(x, center_y - half_height),
                GPoint(x, center_y + half_height)
            );
        }
    } else {
        graphics_fill_rect(
            ctx,
            GRect(center_x - 5, center_y - 7, 3, 14),
            0,
            GCornerNone
        );
        graphics_fill_rect(
            ctx,
            GRect(center_x + 2, center_y - 7, 3, 14),
            0,
            GCornerNone
        );
    }
}

static void draw_running_minimize_action(GContext *ctx) {
    if (!s_run_controls_entry_visible) {
        return;
    }

    const int16_t center_y = RUN_MINIMIZE_CENTER_Y;
    const int16_t edge_x =
        -s_run_controls_entry_offset;

    // Mirror the right-side widening so no white gap appears on the left.
    const int16_t extra_width =
        s_run_controls_entry_offset > 0
            ? s_run_controls_entry_offset
            : 0;
    const int16_t marker_width =
        s_minimize_action_draw_width + extra_width;
    const int16_t marker_y =
        center_y - (RUN_ACTION_MARKER_HEIGHT / 2);

    graphics_context_set_fill_color(ctx, theme_foreground_color());
    graphics_fill_rect(
        ctx,
        GRect(
            0,
            marker_y,
            marker_width,
            RUN_ACTION_MARKER_HEIGHT
        ),
        5,
        GCornersRight
    );

    const int16_t center_x =
        edge_x + RUN_ACTION_MARKER_WIDTH + 10;

    graphics_context_set_stroke_color(ctx, theme_foreground_color());
    graphics_context_set_stroke_width(ctx, 2);

    graphics_draw_line(
        ctx,
        GPoint(center_x - 5, center_y - 5),
        GPoint(center_x, center_y)
    );
    graphics_draw_line(
        ctx,
        GPoint(center_x, center_y),
        GPoint(center_x + 5, center_y - 5)
    );
    graphics_draw_line(
        ctx,
        GPoint(center_x - 6, center_y + 6),
        GPoint(center_x + 6, center_y + 6)
    );

}

static void draw_pause_side_control(
    GContext *ctx,
    GRect bounds,
    int16_t center_y,
    bool add_control
) {
    const int16_t edge_x =
        bounds.size.w + s_pause_controls_offset;

    // While the control is still bouncing in from the right,
    // stretch the black bar so no white background peeks through.
    const int16_t extra_width =
        s_pause_controls_offset > 0
            ? s_pause_controls_offset
            : 0;
    const int16_t marker_width =
        RUN_ACTION_MARKER_WIDTH + extra_width;
    const int16_t marker_x =
        bounds.size.w - marker_width;
    const int16_t marker_y =
        center_y - (RUN_ACTION_MARKER_HEIGHT / 2);
    const int16_t center_x =
        edge_x - RUN_ACTION_MARKER_WIDTH - 10;

    graphics_context_set_fill_color(ctx, theme_foreground_color());
    graphics_fill_rect(
        ctx,
        GRect(
            marker_x,
            marker_y,
            marker_width,
            RUN_ACTION_MARKER_HEIGHT
        ),
        5,
        GCornersLeft
    );

    graphics_context_set_stroke_color(ctx, theme_foreground_color());
    graphics_context_set_stroke_width(ctx, 2);

    if (add_control) {
        // Plus symbol.
        graphics_draw_line(
            ctx,
            GPoint(center_x - 6, center_y),
            GPoint(center_x + 6, center_y)
        );
        graphics_draw_line(
            ctx,
            GPoint(center_x, center_y - 6),
            GPoint(center_x, center_y + 6)
        );
    } else {
        // Minimal X: delete the paused timer.
        graphics_context_set_stroke_width(ctx, 3);
        graphics_draw_line(
            ctx,
            GPoint(center_x - 5, center_y - 5),
            GPoint(center_x + 5, center_y + 5)
        );
        graphics_draw_line(
            ctx,
            GPoint(center_x + 5, center_y - 5),
            GPoint(center_x - 5, center_y + 5)
        );
    }
}

static void draw_running_pause_controls(
    GContext *ctx,
    GRect bounds
) {
    if (!s_running_paused
        || !s_pause_controls_visible) {
        return;
    }

    draw_pause_side_control(
        ctx,
        bounds,
        RUN_PAUSE_CONTROL_TOP_CENTER_Y,
        true
    );
    draw_pause_side_control(
        ctx,
        bounds,
        bounds.size.h - RUN_PAUSE_CONTROL_BOTTOM_MARGIN,
        false
    );
}

static int16_t ppf_character_width(char character) {
    if (character >= '0' && character <= '9') {
        return PPF_DIGIT_WIDTH;
    }

    if (character == ':') {
        return PPF_COLON_WIDTH;
    }

    return 0;
}

static int16_t ppf_text_width(const char *text) {
    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    int16_t width = 0;
    size_t count = 0;

    for (const char *cursor = text;
         *cursor != '\0';
         ++cursor) {
        const int16_t character_width =
            ppf_character_width(*cursor);

        if (character_width == 0) {
            return 0;
        }

        width += character_width;
        count++;
    }

    if (count > 1) {
        width += (int16_t)(
            (count - 1) * PPF_CHARACTER_GAP
        );
    }

    return width;
}

static void draw_ppf_colon(
    GContext *ctx,
    int16_t x,
    int16_t y,
    GColor color
) {
    graphics_context_set_fill_color(ctx, color);

    graphics_fill_rect(
        ctx,
        GRect(
            x,
            y + PPF_COLON_TOP_Y,
            PPF_COLON_WIDTH,
            PPF_COLON_DOT_SIZE
        ),
        0,
        GCornerNone
    );

    graphics_fill_rect(
        ctx,
        GRect(
            x,
            y + PPF_COLON_BOTTOM_Y,
            PPF_COLON_WIDTH,
            PPF_COLON_DOT_SIZE
        ),
        0,
        GCornerNone
    );
}

static bool set_alarm_digit_color(GColor color) {
    if (s_ppf_digit_sheet_alarm == NULL) {
        return false;
    }

    GColor *palette =
        gbitmap_get_palette(s_ppf_digit_sheet_alarm);

    if (palette == NULL) {
        return false;
    }

    size_t palette_size = 0;

    switch (gbitmap_get_format(s_ppf_digit_sheet_alarm)) {
    case GBitmapFormat1BitPalette:
        palette_size = 2;
        break;
    case GBitmapFormat2BitPalette:
        palette_size = 4;
        break;
    case GBitmapFormat4BitPalette:
        palette_size = 16;
        break;
    default:
        return false;
    }

    bool changed_opaque_color = false;

    for (size_t index = 0;
         index < palette_size;
         ++index) {
        if (!gcolor_equal(palette[index], GColorClear)) {
            palette[index] = color;
            changed_opaque_color = true;
        }
    }

    return changed_opaque_color;
}


static bool draw_ppf_text_centered(
    GContext *ctx,
    const char *text,
    GRect frame,
    GColor color,
    bool alarm_style
) {
    GBitmap **digits = NULL;
    GBitmap *digit_sheet = NULL;

    if (alarm_style) {
        if (!set_alarm_digit_color(color)) {
            return false;
        }

        digits = s_ppf_digits_alarm;
        digit_sheet = s_ppf_digit_sheet_alarm;
    } else {
        const bool use_white_sprite = !theme_is_light();

        digits = use_white_sprite
            ? s_ppf_digits_white
            : s_ppf_digits;

        digit_sheet = use_white_sprite
            ? s_ppf_digit_sheet_white
            : s_ppf_digit_sheet;
    }

    if (digit_sheet == NULL) {
        return false;
    }

    const int16_t text_width = ppf_text_width(text);

    if (text_width <= 0
        || text_width > frame.size.w
        || PPF_DIGIT_HEIGHT > frame.size.h) {
        return false;
    }

    int16_t x = frame.origin.x
        + ((frame.size.w - text_width) / 2);
    const int16_t y = frame.origin.y
        + ((frame.size.h - PPF_DIGIT_HEIGHT) / 2);

    graphics_context_set_compositing_mode(
        ctx,
        GCompOpSet
    );

    for (const char *cursor = text;
         *cursor != '\0';
         ++cursor) {
        if (*cursor >= '0' && *cursor <= '9') {
            const uint8_t digit =
                (uint8_t)(*cursor - '0');
            GBitmap *bitmap = digits[digit];

            if (bitmap == NULL) {
                return false;
            }

            graphics_draw_bitmap_in_rect(
                ctx,
                bitmap,
                GRect(
                    x,
                    y,
                    PPF_DIGIT_WIDTH,
                    PPF_DIGIT_HEIGHT
                )
            );

            x += PPF_DIGIT_WIDTH;
        } else if (*cursor == ':') {
            draw_ppf_colon(
                ctx,
                x,
                y,
                color
            );
            x += PPF_COLON_WIDTH;
        } else {
            return false;
        }

        if (cursor[1] != '\0') {
            x += PPF_CHARACTER_GAP;
        }
    }

    return true;
}


static void create_ppf_digits(void) {
    s_ppf_digit_sheet = gbitmap_create_with_resource(
        RESOURCE_ID_PPF_DIGITS
    );
    s_ppf_digit_sheet_white = gbitmap_create_with_resource(
        RESOURCE_ID_PPF_DIGITS_WHITE
    );
    s_ppf_digit_sheet_alarm = gbitmap_create_with_resource(
        RESOURCE_ID_PPF_DIGITS_WHITE
    );

    if (s_ppf_digit_sheet == NULL
        || s_ppf_digit_sheet_white == NULL) {
        if (s_ppf_digit_sheet != NULL) {
            gbitmap_destroy(s_ppf_digit_sheet);
            s_ppf_digit_sheet = NULL;
        }

        if (s_ppf_digit_sheet_white != NULL) {
            gbitmap_destroy(s_ppf_digit_sheet_white);
            s_ppf_digit_sheet_white = NULL;
        }

        if (s_ppf_digit_sheet_alarm != NULL) {
            gbitmap_destroy(s_ppf_digit_sheet_alarm);
            s_ppf_digit_sheet_alarm = NULL;
        }

        return;
    }

    for (uint8_t digit = 0;
         digit < PPF_DIGIT_COUNT;
         ++digit) {
        const GRect glyph = GRect(
            digit * PPF_DIGIT_STRIDE,
            0,
            PPF_DIGIT_WIDTH,
            PPF_DIGIT_HEIGHT
        );

        s_ppf_digits[digit] =
            gbitmap_create_as_sub_bitmap(
                s_ppf_digit_sheet,
                glyph
            );

        s_ppf_digits_white[digit] =
            gbitmap_create_as_sub_bitmap(
                s_ppf_digit_sheet_white,
                glyph
            );

        if (s_ppf_digit_sheet_alarm != NULL) {
            s_ppf_digits_alarm[digit] =
                gbitmap_create_as_sub_bitmap(
                    s_ppf_digit_sheet_alarm,
                    glyph
                );
        }
    }
}


static void destroy_ppf_digits(void) {
    for (uint8_t digit = 0;
         digit < PPF_DIGIT_COUNT;
         ++digit) {
        if (s_ppf_digits[digit] != NULL) {
            gbitmap_destroy(s_ppf_digits[digit]);
            s_ppf_digits[digit] = NULL;
        }

        if (s_ppf_digits_white[digit] != NULL) {
            gbitmap_destroy(s_ppf_digits_white[digit]);
            s_ppf_digits_white[digit] = NULL;
        }

        if (s_ppf_digits_alarm[digit] != NULL) {
            gbitmap_destroy(s_ppf_digits_alarm[digit]);
            s_ppf_digits_alarm[digit] = NULL;
        }
    }

    if (s_ppf_digit_sheet != NULL) {
        gbitmap_destroy(s_ppf_digit_sheet);
        s_ppf_digit_sheet = NULL;
    }

    if (s_ppf_digit_sheet_white != NULL) {
        gbitmap_destroy(s_ppf_digit_sheet_white);
        s_ppf_digit_sheet_white = NULL;
    }

    if (s_ppf_digit_sheet_alarm != NULL) {
        gbitmap_destroy(s_ppf_digit_sheet_alarm);
        s_ppf_digit_sheet_alarm = NULL;
    }
}


static void draw_running_countdown(Layer *layer, GContext *ctx) {
    const GRect bounds = layer_get_bounds(layer);
    const bool alarm_display = s_running_alarm_display;

    // Alarm inversion starts with a light screen and changes every second.
    // Use the alarm start timestamp so the first phase is deterministic
    // instead of depending on the watch's global monotonic clock phase.
    const uint32_t alarm_elapsed_ms = alarm_display
        ? monotonic_ms() - s_running_started_ms
        : 0;

    const bool alarm_dark_phase =
        alarm_display
        && (
            (
                alarm_elapsed_ms
                / ALARM_INVERT_INTERVAL_MS
            ) % 2U
        ) != 0;

    const GColor background_color = alarm_display
        ? (
            alarm_dark_phase
                ? GColorBlack
                : GColorWhite
        )
        : theme_background_color();

    const GColor timer_color = alarm_display
        ? (
            alarm_dark_phase
                ? GColorWhite
                : GColorBlack
        )
        : theme_foreground_color();

    graphics_context_set_fill_color(
        ctx,
        background_color
    );
    graphics_fill_rect(
        ctx,
        bounds,
        0,
        GCornerNone
    );

    if (s_running_transition) {
        draw_top_slide_transition(ctx, bounds);
        return;
    }

    const uint32_t remaining_ms = running_remaining_ms();

    const uint32_t display_ms = alarm_display
        ? s_running_duration_ms
        : remaining_ms;

    const uint32_t minutes = display_ms / 60000U;
    const uint32_t seconds =
        (display_ms / 1000U) % 60U;
    const uint32_t centiseconds =
        (display_ms % 1000U) / 10U;

    char text[20];

    const bool show_centiseconds =
        !alarm_display
        && config_get()->showCentiseconds
        && minutes < 100U;

    if (show_centiseconds) {
        snprintf(
            text,
            sizeof(text),
            "%02lu:%02lu:%02lu",
            (unsigned long)minutes,
            (unsigned long)seconds,
            (unsigned long)centiseconds
        );
    } else {
        snprintf(
            text,
            sizeof(text),
            "%02lu:%02lu",
            (unsigned long)minutes,
            (unsigned long)seconds
        );
    }

    const GRect timer_frame = GRect(
        0,
        (bounds.size.h / 2) - 24,
        bounds.size.w,
        48
    );

    if (!draw_ppf_text_centered(
        ctx,
        text,
        timer_frame,
        timer_color,
        alarm_display
    )) {
        graphics_context_set_text_color(
            ctx,
            timer_color
        );

        graphics_draw_text(
            ctx,
            text,
            fonts_get_system_font(
                FONT_KEY_GOTHIC_28_BOLD
            ),
            timer_frame,
            GTextOverflowModeTrailingEllipsis,
            GTextAlignmentCenter,
            NULL
        );
    }

    if (!alarm_display) {
        draw_running_minimize_action(ctx);
        draw_running_pause_controls(ctx, bounds);
        draw_running_action_bar(ctx, bounds);
    }
}



// Sideways Swiss shield. The point faces right.
static const char *EMBLEM_ROWS[EMBLEM_HEIGHT] = {
    ".RRRRRRRR.....",
    "RRRRWWWRRRR...",
    "RRRRWWWRRRR...",
    "RRRRWWWRRRRR..",
    "RWWWWWWWWWRRR.",
    "RWWWWWWWWWRRRR",
    "RWWWWWWWWWRRR.",
    "RRRRWWWRRRRR..",
    "RRRRWWWRRRR...",
    "RRRRWWWRRRR...",
    ".RRRRRRRR....."
};

static const char *tiny_glyph_rows(char character) {
    if (character >= 'A' && character <= 'Z') {
        character = (char)(character - 'A' + 'a');
    }

    switch (character) {
    case 'a': return "010""101""111""101""101";
    case 'c': return "111""100""100""100""111";
    case 'g': return "111""100""101""101""111";
    case 'h': return "101""101""111""101""101";
    case 'i': return "111""010""010""010""111";
    case 'n': return "110""101""101""101""101";
    case 'o': return "111""101""101""101""111";
    case 'p': return "110""101""110""100""100";
    case 'r': return "110""101""110""101""101";
    case 's': return "111""100""111""001""111";
    case 'w': return "101""101""111""111""101";
    default: return NULL;
    }
}

static void draw_tiny_rotated_glyph(
    GContext *ctx,
    char character,
    int16_t x,
    int16_t y,
    GColor color
) {
    const char *pixels = tiny_glyph_rows(character);

    if (pixels == NULL) {
        return;
    }

    graphics_context_set_fill_color(ctx, color);

    for (int16_t source_y = 0;
         source_y < TINY_GLYPH_HEIGHT;
         ++source_y) {
        for (int16_t source_x = 0;
             source_x < TINY_GLYPH_WIDTH;
             ++source_x) {
            const int index =
                source_y * TINY_GLYPH_WIDTH + source_x;

            if (pixels[index] != '1') {
                continue;
            }

            // 90 degrees counter-clockwise.
            // Together with the reversed vertical layout this rotates
            // the complete former branding by exactly 180 degrees.
            graphics_fill_rect(
                ctx,
                GRect(
                    x + source_y,
                    y + TINY_GLYPH_WIDTH - 1 - source_x,
                    1,
                    1
                ),
                0,
                GCornerNone
            );
        }
    }
}

static void draw_tiny_vertical_text(
    GContext *ctx,
    const char *text,
    int16_t center_x,
    int16_t bottom_y,
    GColor color
) {
    const int16_t x =
        center_x - (TINY_GLYPH_ROTATED_WIDTH / 2);

    int16_t glyph_bottom_y = bottom_y;

    // Characters are placed upward. Reading from the bottom toward
    // the top therefore produces the normal character order.
    for (const char *cursor = text;
         *cursor != '\0';
         ++cursor) {
        const int16_t glyph_top_y =
            glyph_bottom_y - (TINY_GLYPH_WIDTH - 1);

        draw_tiny_rotated_glyph(
            ctx,
            *cursor,
            x,
            glyph_top_y,
            color
        );

        glyph_bottom_y -= TINY_GLYPH_ADVANCE_Y;
    }

}

static void draw_sideways_swiss_emblem(
    GContext *ctx,
    int16_t x,
    int16_t y
) {
#if PBL_COLOR
    const GColor red = GColorRed;
    const GColor white = GColorWhite;
#else
    const GColor red = theme_foreground_color();
    const GColor white = theme_background_color();
#endif

    for (int16_t row = 0;
         row < EMBLEM_HEIGHT;
         ++row) {
        for (int16_t column = 0;
             column < EMBLEM_WIDTH;
             ++column) {
            const char pixel = EMBLEM_ROWS[row][column];

            if (pixel == '.') {
                continue;
            }

            graphics_context_set_fill_color(
                ctx,
                pixel == 'R' ? red : white
            );
            graphics_fill_rect(
                ctx,
                GRect(x + column, y + row, 1, 1),
                0,
                GCornerNone
            );
        }
    }
}

static void draw_swiss_chronograph_branding(
    GContext *ctx,
    GRect bounds,
    int16_t fine_offset
) {
    // Two completely empty pixels remain between the right-facing
    // shield tip and the physical display edge.
    const int16_t emblem_left_x =
        bounds.size.w - EMBLEM_WIDTH - 2;
    // The white cross spans emblem columns 1 through 9, so its
    // visual centre is column 5. The asymmetric shield outline,
    // including its right-facing point, is centred two pixels right.
    const int16_t brand_center_x =
        emblem_left_x + 5;

    // At minute zero, the shield itself is centred on the display.
    // The same scale offset moves it downward together with the ruler.
    const int32_t ruler_offset_y =
        ((int32_t)s_selected_minutes * TICK_SPACING)
        + fine_offset;

    const int16_t emblem_top_y =
        (bounds.size.h / 2)
        - (EMBLEM_HEIGHT / 2)
        + (int16_t)ruler_offset_y;

    const int16_t emblem_bottom_y =
        emblem_top_y + EMBLEM_HEIGHT - 1;

    // Rotated 3x5 text uses four vertical pixels per character,
    // except that the final character needs no trailing gap.
    const int16_t swiss_height =
        (5 * TINY_GLYPH_ADVANCE_Y) - 1;
    const int16_t chronograph_height =
        (11 * TINY_GLYPH_ADVANCE_Y) - 1;

    const int16_t chronograph_bottom_y =
        emblem_top_y - BRANDING_GAP_Y - 1;
    const int16_t chronograph_top_y =
        chronograph_bottom_y - chronograph_height + 1;

    const int16_t swiss_top_y =
        emblem_bottom_y + BRANDING_GAP_Y + 1;
    const int16_t swiss_bottom_y =
        swiss_top_y + swiss_height - 1;

    if (chronograph_top_y >= bounds.size.h
        || swiss_bottom_y < 0) {
        return;
    }

    const GColor text_color =
        theme_foreground_color();

    // Readable from the right-hand side, bottom to top:
    // swiss -> shield -> Chronograph.
    draw_tiny_vertical_text(
        ctx,
        "swiss",
        brand_center_x,
        swiss_bottom_y,
        text_color
    );

    draw_sideways_swiss_emblem(
        ctx,
        emblem_left_x,
        emblem_top_y
    );

    draw_tiny_vertical_text(
        ctx,
        "Chronograph",
        brand_center_x,
        chronograph_bottom_y,
        text_color
    );
}


static void draw_read_line_number_bubble(
    GContext *ctx,
    GRect bounds,
    int16_t ruler_left,
    int16_t center_y,
    int16_t fine_offset
) {
    enum {
        BADGE_FULL_HEIGHT = 25,
        BADGE_PADDING_X = 7,
        BADGE_TEXT_FRAME_WIDTH = 36,
        BADGE_TEXT_FRAME_HEIGHT = 18,
        MAJOR_TICK_LENGTH = 22,
        LABEL_RIGHT_GAP = 3,
        ANIMATION_SCALE = 100
    };

    bool found = false;
    int nearest_distance = TICK_SPACING + 1;
    int nearest_minute = 0;
    int16_t nearest_y = center_y;

    // A major numbered mark affects the bubble only while it is
    // within exactly one minute of the fixed read line.
    for (int offset = -VISIBLE_TICKS;
         offset <= VISIBLE_TICKS;
         ++offset) {
        const int minute =
            s_selected_minutes + offset;

        if (minute < MIN_MINUTES
            || minute > MAX_MINUTES
            || (minute % 5) != 0) {
            continue;
        }

        const int16_t y =
            center_y
            - (offset * TICK_SPACING)
            + fine_offset;

        const int distance =
            ABS(y - center_y);

        if (distance <= TICK_SPACING
            && distance < nearest_distance) {
            found = true;
            nearest_distance = distance;
            nearest_minute = minute;
            nearest_y = y;
        }
    }

    if (!found || nearest_distance >= TICK_SPACING) {
        return;
    }

    char label[8];
    snprintf(
        label,
        sizeof(label),
        "%d",
        nearest_minute
    );

    const GFont font =
        fonts_get_system_font(FONT_KEY_GOTHIC_14);

    const GSize text_size =
        graphics_text_layout_get_content_size(
            label,
            font,
            GRect(
                0,
                0,
                BADGE_TEXT_FRAME_WIDTH,
                BADGE_TEXT_FRAME_HEIGHT
            ),
            GTextOverflowModeTrailingEllipsis,
            GTextAlignmentCenter
        );

    int16_t target_width =
        text_size.w + (BADGE_PADDING_X * 2);

    if (target_width < BADGE_FULL_HEIGHT) {
        target_width = BADGE_FULL_HEIGHT;
    }

    // 0 at one minute distance, 100 at the exact numbered mark.
    const int raw_progress =
        ((TICK_SPACING - nearest_distance)
            * ANIMATION_SCALE)
        / TICK_SPACING;

    // Smoothstep: the opening starts and ends softly but remains
    // completely tied to the physical ruler movement.
    const int eased_progress =
        (
            raw_progress
            * raw_progress
            * ((3 * ANIMATION_SCALE)
                - (2 * raw_progress))
        )
        / (ANIMATION_SCALE * ANIMATION_SCALE);

    int16_t bubble_h =
        1
        + (
            (BADGE_FULL_HEIGHT - 1)
            * eased_progress
        )
        / ANIMATION_SCALE;

    int16_t bubble_w =
        1
        + (
            (target_width - 1)
            * eased_progress
        )
        / ANIMATION_SCALE;

    if (bubble_h < 2 || bubble_w < 2) {
        return;
    }

    // Existing ruler labels are right-aligned to this edge.
    const int16_t label_right =
        bounds.size.w
        - MAJOR_TICK_LENGTH
        - LABEL_RIGHT_GAP;

    // Keep the bubble centred on the label's natural position while
    // the label itself continues moving vertically with the ruler.
    const int16_t label_center_x =
        label_right - (text_size.w / 2);

    int16_t bubble_x =
        label_center_x - (bubble_w / 2);

    if (bubble_x < ruler_left) {
        bubble_x = ruler_left;
    }

    const int16_t bubble_y =
        center_y - (bubble_h / 2);

    const GRect bubble_rect =
        GRect(
            bubble_x,
            bubble_y,
            bubble_w,
            bubble_h
        );

    const int16_t radius =
        bubble_h / 2;

    // The read line was drawn immediately before this function.
    // Filling here makes the line appear to flow behind the bubble.
    graphics_context_set_fill_color(
        ctx,
        theme_background_color()
    );
    graphics_fill_rect(
        ctx,
        bubble_rect,
        radius,
        GCornersAll
    );

    graphics_context_set_stroke_color(
        ctx,
        config_get()->ringColorRemaining
    );
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(
        ctx,
        bubble_rect,
        radius
    );

    // Redraw the moving ruler number after the read line and bubble.
    // Its y-position remains attached to the ruler, not to the bubble.
    graphics_context_set_text_color(
        ctx,
        theme_foreground_color()
    );
    graphics_draw_text(
        ctx,
        label,
        font,
        GRect(
            ruler_left,
            nearest_y - 9,
            RULER_WIDTH
                - MAJOR_TICK_LENGTH
                - LABEL_RIGHT_GAP,
            BADGE_TEXT_FRAME_HEIGHT
        ),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentRight,
        NULL
    );
}


static void draw_ruler(Layer *layer, GContext *ctx) {
    if (s_running_screen) {
        draw_running_countdown(layer, ctx);
        return;
    }

    const GRect bounds = layer_get_bounds(layer);
    const int16_t ruler_left = bounds.size.w - RULER_WIDTH;
    const int16_t center_y = READOUT_Y;

    const int16_t fine_offset =
        s_ruler_roll_offset;

    graphics_context_set_fill_color(ctx, theme_background_color());
    graphics_fill_rect(
        ctx,
        GRect(ruler_left, 0, RULER_WIDTH, bounds.size.h),
        0,
        GCornerNone
    );

    graphics_context_set_stroke_color(ctx, theme_foreground_color());
    graphics_context_set_text_color(ctx, theme_foreground_color());

    const GFont small_font =
        fonts_get_system_font(FONT_KEY_GOTHIC_14);

    char label[8];

    for (int offset = -VISIBLE_TICKS; offset <= VISIBLE_TICKS; ++offset) {
        const int minute = s_selected_minutes + offset;

        if (minute < MIN_MINUTES || minute > MAX_MINUTES) {
            continue;
        }

        const int16_t y =
            center_y - (offset * TICK_SPACING) + fine_offset;

        if (y < -TICK_SPACING || y > bounds.size.h + TICK_SPACING) {
            continue;
        }

        const bool major = (minute % 5) == 0;
        const int16_t tick_length = major ? 22 : 11;

        graphics_context_set_stroke_width(ctx, major ? 2 : 1);

        graphics_draw_line(
            ctx,
            GPoint(bounds.size.w - tick_length, y),
            GPoint(bounds.size.w - 1, y)
        );

        if (major) {
            snprintf(label, sizeof(label), "%d", minute);

            graphics_draw_text(
                ctx,
                label,
                small_font,
                GRect(
                    ruler_left,
                    y - 9,
                    RULER_WIDTH - tick_length - 3,
                    18
                ),
                GTextOverflowModeTrailingEllipsis,
                GTextAlignmentRight,
                NULL
            );
        }
    }

    draw_swiss_chronograph_branding(
        ctx,
        bounds,
        fine_offset
    );

    graphics_context_set_stroke_width(ctx, 3);
    graphics_context_set_stroke_color(
        ctx,
        config_get()->ringColorRemaining
    );

    graphics_draw_line(
        ctx,
        GPoint(ruler_left, center_y),
        GPoint(bounds.size.w - 1, center_y)
    );

    draw_read_line_number_bubble(
        ctx,
        bounds,
        ruler_left,
        center_y,
        fine_offset
    );


    // Keep the zero position visually quiet. The large value
    // appears only after at least one minute is selected.
    if (s_selected_minutes > 0) {
        snprintf(label, sizeof(label), "%d", s_selected_minutes);

        // Center on the full display and exactly on the read line.
        const GRect selected_value_frame = GRect(
            0,
            center_y - (PPF_DIGIT_HEIGHT / 2),
            bounds.size.w,
            PPF_DIGIT_HEIGHT
        );

        if (!draw_ppf_text_centered(
            ctx,
            label,
            selected_value_frame,
            theme_foreground_color(),
            false
        )) {
            graphics_context_set_text_color(
                ctx,
                theme_foreground_color()
            );

            graphics_draw_text(
                ctx,
                label,
                fonts_get_system_font(
                    FONT_KEY_GOTHIC_28_BOLD
                ),
                selected_value_frame,
                GTextOverflowModeTrailingEllipsis,
                GTextAlignmentCenter,
                NULL
            );
        }

    }

    const int16_t arrow_center_x = bounds.size.w / 2;
    const int16_t arrow_half_width = bounds.size.w / 6;

    // Centre the complete arrow stack vertically when the scale is at zero.
    const int16_t arrow_stack_half_height =
        ((ARROW_COUNT - 1) * ARROW_STEP_Y) / 2;

    const int32_t lower_arrow_top =
        (bounds.size.h / 2) - arrow_stack_half_height +
        s_arrow_drag_offset;

    for (int i = 0; i < ARROW_COUNT; ++i) {
        const int32_t y_down =
            lower_arrow_top + (i * ARROW_STEP_Y);

        if (y_down >= -ARROW_STEP_Y &&
            y_down <= bounds.size.h + ARROW_STEP_Y) {
            draw_chevron_down(
                ctx,
                arrow_center_x,
                (int16_t)y_down,
                arrow_half_width,
                arrow_color_for_index(i)
            );
        }
    }
}

static void handle_touch_event(
    const TouchEvent *event,
    void *context
) {
    if (s_running_screen) {
        return;
    }

    switch (event->type) {
    case TouchEvent_Touchdown:
        cancel_arrow_fill();

        // Keep the selector quiet during the entire drag gesture.
        stop_arrow_animation();
        s_touching = true;
        s_last_y = event->y;

        if (s_arrow_bounce_timer != NULL) {
            app_timer_cancel(s_arrow_bounce_timer);
            s_arrow_bounce_timer = NULL;
        }
        s_arrow_bounce_velocity = 0;
        break;

    case TouchEvent_PositionUpdate:
        if (s_touching) {
            const int16_t delta_y =
                event->y - s_last_y;
            s_last_y = event->y;

            if (delta_y == 0) {
                break;
            }

            s_drag_accumulator += delta_y;
            s_arrow_drag_offset += delta_y;

            // The halfway point is the ridge between two minute
            // valleys. Crossing it commits immediately and
            // unambiguously to the neighbouring valley.
            if (delta_y > 0) {
                while (
                    s_drag_accumulator
                        >= MINUTE_DETENT_THRESHOLD_PX
                ) {
                    if (s_selected_minutes
                        >= MAX_MINUTES) {
                        s_drag_accumulator =
                            MINUTE_DETENT_THRESHOLD_PX - 1;
                        break;
                    }

                    s_drag_accumulator -=
                        PIXELS_PER_MINUTE;

                    set_minutes(
                        s_selected_minutes + 1,
                        true
                    );
                    start_ruler_roll(1);
                }
            } else {
                while (
                    s_drag_accumulator
                        <= -MINUTE_DETENT_THRESHOLD_PX
                ) {
                    if (s_selected_minutes
                        <= MIN_MINUTES) {
                        s_drag_accumulator =
                            -MINUTE_DETENT_THRESHOLD_PX + 1;
                        break;
                    }

                    s_drag_accumulator +=
                        PIXELS_PER_MINUTE;

                    set_minutes(
                        s_selected_minutes - 1,
                        true
                    );
                    start_ruler_roll(-1);
                }
            }

            layer_mark_dirty(s_layer);
        }
        break;

    case TouchEvent_Liftoff:
        s_touching = false;

        // The ruler is already in one of the two neighbouring
        // valleys. Discard only the invisible finger remainder.
        s_drag_accumulator = 0;

        if (s_selected_minutes > 0) {
            start_arrow_animation();
        } else {
            stop_arrow_animation();
        }

        start_arrow_bounce();
        layer_mark_dirty(s_layer);
        break;

    default:
        break;
    }

    if (s_parent_handler != NULL) {
        s_parent_handler(event, context);
    }
}


void touch_enable(bool enable) {
    if (!touch_service_is_enabled() || s_layer == NULL) {
        return;
    }

    if (enable && s_running_screen) {
        return;
    }

    if (enable) {
        if (!s_touch_is_enabled) {
            touch_service_subscribe(handle_touch_event, NULL);
            s_touch_is_enabled = true;
        }

    } else {
        cancel_ruler_roll();


        if (s_touch_is_enabled) {
            touch_service_unsubscribe();
            s_touch_is_enabled = false;
        }

        s_touching = false;
    }
}

void touch_create(
    Layer *parent,
    TouchSelectionCallback callback,
    TouchServiceHandler handler,
    TouchAutoStartCallback auto_start_callback
) {
    if (!touch_service_is_enabled() || s_layer != NULL) {
        return;
    }

    s_callback = callback;
    s_parent_handler = handler;
    s_auto_start_callback = auto_start_callback;
    s_selected_minutes = 0;
    s_drag_accumulator = 0;
    s_ruler_roll_timer = NULL;
    s_ruler_roll_offset = 0;
    s_arrow_drag_offset = 0;
    s_arrow_bounce_velocity = 0;
    s_arrow_bounce_timer = NULL;
    s_running_screen = false;
    s_running_transition = false;
    s_running_transition_step = 0;
    s_running_duration_ms = 0;
    s_running_started_ms = 0;
    s_running_paused = false;
    s_running_paused_remaining_ms = 0;
    s_running_frame_timer = NULL;
    s_running_transition_timer = NULL;
    s_run_action_anim_timer = NULL;
    s_run_action_draw_width = RUN_ACTION_MARKER_WIDTH;
    s_run_action_button_down = false;
    s_run_action_action_fired = false;
    s_run_action_callback = NULL;
    s_minimize_action_anim_timer = NULL;
    s_minimize_action_draw_width = RUN_ACTION_MARKER_WIDTH;
    s_minimize_action_button_down = false;
    s_minimize_action_pressed = false;
    s_minimize_action_release_callback = NULL;
    s_pause_controls_anim_timer = NULL;
    s_pause_controls_anim_step = 0;
    s_pause_controls_offset =
        RUN_CONTROLS_HIDDEN_OFFSET;
    s_pause_controls_visible = false;
    s_run_controls_entry_timer = NULL;
    s_run_controls_entry_step = 0;
    s_run_controls_entry_offset =
        RUN_CONTROLS_HIDDEN_OFFSET;
    s_run_controls_entry_visible = false;

    create_ppf_digits();

    s_layer = layer_create(layer_get_bounds(parent));
    layer_set_update_proc(s_layer, draw_ruler);
    layer_add_child(parent, s_layer);
    layer_set_hidden(s_layer, false);

    s_arrow_animation_started = false;
    s_arrow_anim_phase = 0;
    s_arrow_anim_timer = NULL;
    s_arrow_fill_count = 0;
    s_arrow_fill_wave_index = -1;

    touch_enable(true);
}

void touch_destroy(void) {
    cancel_ruler_roll();
    if (s_layer == NULL) {
        return;
    }

    cancel_running_timers();
    cancel_run_action_animation();
    cancel_minimize_action_animation();
    hide_pause_controls();
    hide_run_controls();
    touch_enable(false);


    stop_arrow_animation();

    if (s_arrow_bounce_timer != NULL) {
        app_timer_cancel(s_arrow_bounce_timer);
        s_arrow_bounce_timer = NULL;
    }

    if (s_arrow_fill_timer != NULL) {
        app_timer_cancel(s_arrow_fill_timer);
        s_arrow_fill_timer = NULL;
    }

    layer_destroy(s_layer);
    s_layer = NULL;

    destroy_ppf_digits();
    s_callback = NULL;
    s_parent_handler = NULL;
    s_auto_start_callback = NULL;
}

#endif // PBL_TOUCH

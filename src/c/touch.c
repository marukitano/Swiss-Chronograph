// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).
// TAP_TIMER_TOUCH_UI_CLEAN_V1
// Vertical timer-ruler interaction by Maru Kitano, 2026.

#include "touch.h"

#include <pebble.h>

#if PBL_TOUCH

#include "config.h"
#include "macros.h"

#define TOUCH_ENABLED_DURATION_MS 3000
#define PIXELS_PER_MINUTE 10
#define MIN_MINUTES 0
#define MAX_MINUTES 180
#define RULER_WIDTH 58
#define TICK_SPACING 12
#define VISIBLE_TICKS 16

#define READOUT_Y 20

#define ARROW_COUNT 6
#define ARROW_STEP_Y 16
#define ARROW_HALF_HEIGHT 7
#define ARROW_ANIM_MS 120
#define ARROW_BOUNCE_MS 28
#define ARROW_BOUNCE_MAX_TICKS 36
#define ARROW_BOUNCE_DAMPING_NUM 72
#define ARROW_BOUNCE_DAMPING_DEN 100
#define ARROW_FILL_MS 120
#define RUNNING_FRAME_MS 50
#define RUNNING_TRANSITION_MS 24
#define RUNNING_TRANSITION_STEPS 12
#define RUN_ACTION_AREA_WIDTH 24
#define RUN_ACTION_MARKER_WIDTH 5
#define RUN_ACTION_MARKER_HEIGHT 52
#define RUN_ACTION_ANIM_MS 28
#define RUN_ACTION_PRESS_OFFSET 2
#define RUN_ACTION_PRESSED_WIDTH (RUN_ACTION_MARKER_WIDTH + RUN_ACTION_PRESS_OFFSET)
#define RUN_MINIMIZE_CENTER_Y 36
#define RUN_MINIMIZE_ANIM_MS 55
#define RUN_MINIMIZE_EXIT_DELAY_MS 90

static Layer *s_layer;
static TouchSelectionCallback s_callback;
static TouchServiceHandler s_parent_handler;
static TouchAutoStartCallback s_auto_start_callback;

static bool s_touch_is_enabled;
static bool s_touching;
static int16_t s_last_y;
static int16_t s_drag_accumulator;
static int16_t s_selected_minutes;

static AppTimer *s_touch_disable_timer;
static AppTimer *s_arrow_anim_timer;
static uint8_t s_arrow_anim_phase;
static AppTimer *s_arrow_bounce_timer;
static int16_t s_arrow_drag_offset;
static int16_t s_arrow_bounce_velocity;
static uint8_t s_arrow_bounce_ticks;
static AppTimer *s_arrow_fill_timer;
static uint8_t s_arrow_fill_count;
static int8_t s_arrow_fill_wave_index;
static bool s_running_screen;
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


void touch_adjust_minutes(int delta) {
    if (s_running_screen) {
        return;
    }

    set_minutes(s_selected_minutes + delta, true);
}


static void arrow_anim_tick(void *context) {
    UNUSED(context);

    s_arrow_anim_phase = (s_arrow_anim_phase + 1) % ARROW_COUNT;

    if (s_layer != NULL) {
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


static void cancel_arrow_fill(void);
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

    // Five chevrons fill normally. When the sixth chevron would start,
    // replace it with the run-screen transition from the top.
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
    const GColor dim = GColorBlack;
    const GColor mid = GColorWhite;
#endif

    if (s_arrow_fill_count > 0 ||
        s_arrow_fill_wave_index >= 0) {
        const int first_completed =
            ARROW_COUNT - s_arrow_fill_count;

        if (index >= first_completed) {
            return GColorWhite;
        }

        if (index == s_arrow_fill_wave_index) {
            return GColorWhite;
        }

        if (s_arrow_fill_wave_index > 0 &&
            index == s_arrow_fill_wave_index - 1) {
            return mid;
        }

        return dim;
    }

    if (index == s_arrow_anim_phase) {
        return config_get()->textColor;
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
    graphics_context_set_fill_color(ctx, config_get()->bgColor);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    if (s_running_transition_step >= RUNNING_TRANSITION_STEPS) {
        graphics_context_set_fill_color(ctx, GColorWhite);
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

    graphics_context_set_stroke_color(ctx, GColorWhite);
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

        graphics_context_set_stroke_color(ctx, GColorWhite);

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
    cancel_run_action_animation();
    cancel_minimize_action_animation();

    if (s_arrow_bounce_timer != NULL) {
        app_timer_cancel(s_arrow_bounce_timer);
        s_arrow_bounce_timer = NULL;
    }

    s_touching = false;
    s_running_screen = true;
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
    if (s_layer == NULL || remaining_seconds == 0) {
        return;
    }

    cancel_arrow_fill();
    cancel_run_action_animation();
    cancel_minimize_action_animation();

    if (s_arrow_bounce_timer != NULL) {
        app_timer_cancel(s_arrow_bounce_timer);
        s_arrow_bounce_timer = NULL;
    }

    s_touching = false;
    s_running_screen = true;

    // Restore directly to the finished run screen. The top-slide animation
    // belongs only to a newly started timer.
    s_running_transition = false;
    s_running_transition_step = RUNNING_TRANSITION_STEPS;

    s_running_duration_ms = remaining_seconds * 1000U;
    s_running_started_ms = monotonic_ms();
    s_running_paused = paused;
    s_running_paused_remaining_ms =
        paused ? s_running_duration_ms : 0;

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
    } else {
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
    cancel_running_timers();
    cancel_run_action_animation();
    cancel_minimize_action_animation();

    if (s_arrow_bounce_timer != NULL) {
        app_timer_cancel(s_arrow_bounce_timer);
        s_arrow_bounce_timer = NULL;
    }

    cancel_arrow_fill();

    // Leave running mode first. The parent timer logic may ignore selection
    // callbacks while the countdown or alarm is still considered active.
    s_running_screen = false;
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
    const int16_t center_y = bounds.size.h / 2;
    const int16_t marker_x =
        bounds.size.w - s_run_action_draw_width;
    const int16_t marker_y =
        center_y - (RUN_ACTION_MARKER_HEIGHT / 2);

    // Small visual marker aligned with the physical middle button.
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(
        ctx,
        GRect(
            marker_x,
            marker_y,
            s_run_action_draw_width,
            RUN_ACTION_MARKER_HEIGHT
        ),
        5,
        GCornersLeft
    );

    // Keep the existing play/pause shape directly beside the marker.
    const int16_t center_x =
        bounds.size.w - RUN_ACTION_MARKER_WIDTH - 10;

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);

    if (s_running_paused) {
        // Right-pointing play triangle.
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
        // Pause symbol.
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


static void draw_running_minimize_action(
    GContext *ctx,
    GRect bounds
) {
    const int16_t center_y = RUN_MINIMIZE_CENTER_Y;
    const int16_t marker_y =
        center_y - (RUN_ACTION_MARKER_HEIGHT / 2);

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(
        ctx,
        GRect(
            0,
            marker_y,
            s_minimize_action_draw_width,
            RUN_ACTION_MARKER_HEIGHT
        ),
        5,
        GCornersRight
    );

    // Downward chevron above a short line: minimize, not close.
    const int16_t center_x =
        RUN_ACTION_MARKER_WIDTH + 10;

    graphics_context_set_stroke_color(ctx, GColorBlack);
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

    UNUSED(bounds);
}




static void draw_running_countdown(Layer *layer, GContext *ctx) {
    const GRect bounds = layer_get_bounds(layer);

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    if (s_running_transition) {
        draw_top_slide_transition(ctx, bounds);
        return;
    }

    const uint32_t remaining_ms = running_remaining_ms();

    const uint32_t minutes = remaining_ms / 60000U;
    const uint32_t seconds = (remaining_ms / 1000U) % 60U;
    const uint32_t centiseconds = (remaining_ms % 1000U) / 10U;

    char text[20];
    snprintf(
        text,
        sizeof(text),
        "%02lu:%02lu:%02lu",
        (unsigned long)minutes,
        (unsigned long)seconds,
        (unsigned long)centiseconds
    );

    graphics_context_set_text_color(ctx, GColorBlack);

    graphics_draw_text(
        ctx,
        text,
        fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
        GRect(
            2,
            (bounds.size.h / 2) - 24,
            bounds.size.w - RUN_ACTION_AREA_WIDTH - 4,
            48
        ),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter,
        NULL
    );

    draw_running_minimize_action(ctx, bounds);
    draw_running_action_bar(ctx, bounds);
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
        (s_drag_accumulator * TICK_SPACING) / PIXELS_PER_MINUTE;

    graphics_context_set_fill_color(ctx, config_get()->bgColor);
    graphics_fill_rect(
        ctx,
        GRect(ruler_left, 0, RULER_WIDTH, bounds.size.h),
        0,
        GCornerNone
    );

    graphics_context_set_stroke_color(ctx, config_get()->textColor);
    graphics_context_set_text_color(ctx, config_get()->textColor);

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

    graphics_context_set_stroke_width(ctx, 3);
    graphics_context_set_stroke_color(
        ctx,
        config_get()->ringColorRemaining
    );

    graphics_draw_line(
        ctx,
        GPoint(ruler_left - 7, center_y),
        GPoint(bounds.size.w - 1, center_y)
    );

    graphics_context_set_stroke_width(ctx, 5);

    graphics_draw_line(
        ctx,
        GPoint(ruler_left - 7, center_y),
        GPoint(ruler_left, center_y)
    );

    snprintf(label, sizeof(label), "%d", s_selected_minutes);

    graphics_context_set_text_color(ctx, config_get()->textColor);

    graphics_draw_text(
        ctx,
        label,
        fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
        GRect(0, center_y - 20, bounds.size.w, 40),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter,
        NULL
    );

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
            const int16_t delta_y = event->y - s_last_y;
            s_last_y = event->y;
            s_drag_accumulator += delta_y;
            s_arrow_drag_offset += delta_y;

            while (s_drag_accumulator >= PIXELS_PER_MINUTE) {
                if (s_selected_minutes >= MAX_MINUTES) {
                    s_drag_accumulator = 0;
                    break;
                }

                s_drag_accumulator -= PIXELS_PER_MINUTE;
                set_minutes(s_selected_minutes + 1, true);
            }

            while (s_drag_accumulator <= -PIXELS_PER_MINUTE) {
                if (s_selected_minutes <= MIN_MINUTES) {
                    s_drag_accumulator = 0;
                    break;
                }

                s_drag_accumulator += PIXELS_PER_MINUTE;
                set_minutes(s_selected_minutes - 1, true);
            }

            if (
                (s_selected_minutes <= MIN_MINUTES &&
                 s_drag_accumulator < 0)
                ||
                (s_selected_minutes >= MAX_MINUTES &&
                 s_drag_accumulator > 0)
            ) {
                s_drag_accumulator = 0;
            }

            layer_mark_dirty(s_layer);
        }
        break;

    case TouchEvent_Liftoff:
        s_touching = false;

        // Snap the ruler to the nearest whole minute.
        if (s_drag_accumulator >= (PIXELS_PER_MINUTE / 2) &&
            s_selected_minutes < MAX_MINUTES) {
            set_minutes(s_selected_minutes + 1, true);
        } else if (s_drag_accumulator <= -(PIXELS_PER_MINUTE / 2) &&
                   s_selected_minutes > MIN_MINUTES) {
            set_minutes(s_selected_minutes - 1, true);
        }

        // Remove the fractional visual offset so a tick sits exactly
        // underneath the read line.
        s_drag_accumulator = 0;

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


static void timeout_enable_callback(void *context) {
    UNUSED(context);
    s_touch_disable_timer = NULL;
    touch_enable(false);
}


static void schedule_touch_disable(void) {
    if (!config_get()->touchDisableWhileInactive) {
        return;
    }

    if (s_touch_disable_timer == NULL) {
        s_touch_disable_timer = app_timer_register(
            TOUCH_ENABLED_DURATION_MS,
            timeout_enable_callback,
            NULL
        );
    } else {
        app_timer_reschedule(
            s_touch_disable_timer,
            TOUCH_ENABLED_DURATION_MS
        );
    }
}


bool touch_in_progress(void) {
    return s_touching;
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

        schedule_touch_disable();
    } else {
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

    s_layer = layer_create(layer_get_bounds(parent));
    layer_set_update_proc(s_layer, draw_ruler);
    layer_add_child(parent, s_layer);
    layer_set_hidden(s_layer, false);

    s_arrow_anim_phase = 0;
    s_arrow_anim_timer = app_timer_register(
        ARROW_ANIM_MS,
        arrow_anim_tick,
        NULL
    );

    touch_enable(true);
}


void touch_destroy(void) {
    if (s_layer == NULL) {
        return;
    }

    cancel_running_timers();
    cancel_run_action_animation();
    cancel_minimize_action_animation();
    touch_enable(false);

    if (s_touch_disable_timer != NULL) {
        app_timer_cancel(s_touch_disable_timer);
        s_touch_disable_timer = NULL;
    }

    if (s_arrow_anim_timer != NULL) {
        app_timer_cancel(s_arrow_anim_timer);
        s_arrow_anim_timer = NULL;
    }

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
    s_callback = NULL;
    s_parent_handler = NULL;
    s_auto_start_callback = NULL;
}

#endif // PBL_TOUCH

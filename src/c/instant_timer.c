// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    - Allow more than 12 hours touch alarm setting, and/or make AM/PM clearer
    - Enable touch on system touch-to-wake event?
    - Config down-from-zero wrap values
    - increase big font size on gabbro when FONT_KEY_GOTHIC_36_BOLD is available
    - insert timeline pin
        - "PKJS when running on the new Core app can just do Pebble.insertTimelinePin"
        - https://github.com/CometDog/pebble-kite/blob/main/src/ts/timeline.ts#L6-L30
        - https://github.com/coredevices/pypkjs/commit/b0f02e1bca2d005524c8ee46aaf45aac1531b816
*/

#include <pebble.h>
#include <stdio.h>
#include <time.h>

#define DEBUG (0 || PBL_DEBUG)
#define FORCE_BACKLIGHT_ON 0
#if FORCE_BACKLIGHT_ON
    #define DEMO_BACKLIGHT_ENABLE(on) light_enable(on)
#else // !FORCE_BACKLIGHT_ON
    #define DEMO_BACKLIGHT_ENABLE(on)
#endif // !FORCE_BACKLIGHT_ON

#include "config.h"
#include "macros.h"
#include "misc.h"
#include "persist_keys.h"
#include "touch.h"

#define LIGHT_FADE_TIME_MS (500)  // The system duration for backlight fade, from light.c

static Window *s_main_window;

static Layer* s_bg_layer;
static Layer* s_duration_layer;

static BitmapLayer* s_status_icon_layer;
static GBitmap* s_status_icon_alarm;
static GBitmap* s_status_icon_alert;
static GBitmap* s_status_icon_pause;

// text
static TextLayer *s_text_layer_edit_indicator;
static TextLayer *s_text_layer_alarm_duration;
static TextLayer *s_text_layer_alarm_time;
static TextLayer *s_text_layer_big_remaining;
static TextLayer *s_text_layer_big_elapsed;
static TextLayer *s_text_layer_small_elapsed;
static char s_edit_indicator_text[MAX_TIME_TEXT_SIZE] = "^";
static char s_alarm_duration_text[MAX_TIME_TEXT_SIZE] = "00h00m00s";
static char s_alarm_time_text[MAX_TIME_TEXT_SIZE] = "00:00 PM";
static char s_elapsed_text[MAX_TIME_TEXT_SIZE] = "00h00m00s";
static char s_remaining_text[MAX_TIME_TEXT_SIZE] = "00h00m00s";

// action bar
static ActionBarLayer *s_action_bar;
static GBitmap* s_icon_start;
static GBitmap* s_icon_tick;

typedef enum IncrementMode_e {
    INCR_HOURS = 0,
    INCR_5MINS = 1,
    INCR_MINS  = 2,
    INCR_SECS  = 3
} IncrementMode;

// mode state
#define MODE_HOURS INCR_HOURS
#define MODE_5MINS INCR_5MINS
#define MODE_MINS  INCR_MINS
#define MODE_SECS  INCR_SECS
#define MODE_CTRL  MODE_SECS + 1
#define MODE_MAX MODE_CTRL
static int32_t s_mode = MODE_HOURS;

// app state that needs persistence
#define PERSIST_TIMER_STATE_VERSION (4)  // The current persistent storage version. Increment when making changes to stored data.
typedef struct State_s {
    time_t alarm_duration;
    time_t start_time;
    time_t elapsed_time;
    bool is_counting;
    bool is_alarm_done;
    WakeupId alarm_wakeup_id;
} State_t;
State_t s_state = {
    .alarm_duration = 0,
    .start_time = 0,
    .elapsed_time = 0,
    .is_counting = false,
    .is_alarm_done = false,
    .alarm_wakeup_id = E_DOES_NOT_EXIST
};

// state that doesn't need persistence
static bool s_save = true;  // whether to save on exit
static bool s_initialising = true;
static TimeUnits s_update_rate = YEAR_UNIT;

/******************************************************************************
 Generic-ish functions
******************************************************************************/

// Styles for showing seconds for snprintf_hms
typedef enum SecDisplay {
    SEC_HIDE = 0,
    SEC_DASH,
    SEC_SHOW
} SecDisplay;

/// Format `seconds` into a `buffer` of `size` as days, hours, minutes, seconds
/// `truncate_h` to exclude hours if 0 (days is always truncated if 0)
static void snprintf_hms(char* buffer, size_t size, time_t seconds, bool truncate_h, SecDisplay sec_disp) {
    const char* neg = seconds < 0 ? "-" : "";
    const int abs_seconds = ABS(seconds);
    const int d = abs_seconds / SECONDS_PER_DAY;
    const int h = (abs_seconds % SECONDS_PER_DAY ) / SECONDS_PER_HOUR;
    const int m = (abs_seconds % SECONDS_PER_HOUR ) / SECONDS_PER_MINUTE;
    const int s = abs_seconds % SECONDS_PER_MINUTE;
    if (d) {
        const char* fmt = (sec_disp == SEC_SHOW) ? "%s%dd%02dh%02dm%02ds"
                        : (sec_disp == SEC_DASH) ? "%s%dd%02dh%02dm--s"
                        :                          "%s%dd%02dh%02dm";
        snprintf(buffer, size, fmt, neg, d, h, m, s);
    } else if (h || !truncate_h) {
        const char* fmt = (sec_disp == SEC_SHOW) ? "%s%dh%02dm%02ds"
                        : (sec_disp == SEC_DASH) ? "%s%dh%02dm--s"
                        :                          "%s%dh%02dm";
        snprintf(buffer, size, fmt, neg, h, m, s);
    } else if (m || (s_update_rate == MINUTE_UNIT)) {
        const char* fmt = (sec_disp == SEC_SHOW) ? "%s%dm%02ds"
                        : (sec_disp == SEC_DASH) ? "%s%dm--s"
                        :                          "%s%dm";
        snprintf(buffer, size, fmt, neg, m, s);
    } else {
        snprintf(buffer, size, "%s%ds", neg, s);
    }
}

/// Animate `layer` to `appear` or disappear by scrolling pixels vertically `from_below` or above.
/// `was_visible` a pointer to a static bool; will be updated
static void animate_scroll(Layer *layer, bool appear, bool from_below, bool* was_visible) {
    const int16_t hide_offset = (from_below ? 1 : -1) * layer_get_bounds(layer).size.h;
    GPoint hidden_point = GPoint(0, hide_offset);
    GPoint zero = GPoint(0, 0);
    GPoint *from = NULL;
    GPoint *to = NULL;
    if (appear) {
        from = &hidden_point;
        to = &zero;
    } else {
        // from wherever it currently is
        to = &hidden_point;
    }

    if (s_initialising) {  // start in the correct location
        GRect bounds = layer_get_bounds(layer);
        bounds.origin = *to;
        layer_set_bounds(layer, bounds);
    } else if (appear != *was_visible) {
        Animation *animation = (Animation *)property_animation_create_bounds_origin(layer, from, to);
        animation_set_curve(animation, AnimationCurveLinear);
        animation_set_duration(animation, 100);
        animation_schedule(animation);
    }
    *was_visible = appear;
}

/******************************************************************************
 Persistence
******************************************************************************/

static bool is_persist_written_and_current_version(void) {
    return persist_read_int(PERSIST_KEY_TIMER_VERSION) == PERSIST_TIMER_STATE_VERSION;
}

/// Return true if data was loaded
static bool stopwatch_load(void){
    StatusCode status = E_DOES_NOT_EXIST;
    if (is_persist_written_and_current_version()){
        status = persist_read_data(PERSIST_KEY_TIMER_STATE, &s_state, sizeof(s_state));
        ASSERT(status == sizeof(s_state));
    }
    return status == sizeof(s_state);
}

static void stopwatch_save(void){
    StatusCode status = persist_write_data(PERSIST_KEY_TIMER_STATE, &s_state, sizeof(s_state));
    ASSERT(status == sizeof(s_state));

    if (status == sizeof(s_state)) {
        status = persist_write_int(PERSIST_KEY_TIMER_VERSION, PERSIST_TIMER_STATE_VERSION);
        ASSERT(status == sizeof(int32_t));
    }
}

static void stopwatch_delete(void){
    StatusCode status = persist_delete(PERSIST_KEY_TIMER_STATE);
    ASSERT((status == S_TRUE) || (status == E_DOES_NOT_EXIST));

    status = persist_delete(PERSIST_KEY_TIMER_VERSION);
    ASSERT((status == S_TRUE) || (status == E_DOES_NOT_EXIST));
}

/******************************************************************************
 Business logic
******************************************************************************/

/// Return the time at which the alarm should go off, or 0 if there is no future alarm.
static time_t stopwatch_get_alarm_time(void) {
    const time_t end_time = s_state.start_time + s_state.alarm_duration;
    if (end_time > time(NULL)) {
        return end_time;
    } else {
        return 0;
    }
}

static void stopwatch_tick(void) {
    if (s_state.is_counting) {
        s_state.elapsed_time = time(NULL) - s_state.start_time;
    }
}

// Resume counting if not already
static void stopwatch_resume(void) {
    if (!s_state.is_counting) {
        // resume; reload a new start time from the elapsed time
        s_state.start_time = time(NULL) - s_state.elapsed_time;
        s_state.is_counting = true;
    }
}

static void stopwatch_toggle(void) {
    if (s_state.is_counting) {
        // pause; ensure elapsed time is up-to-date
        stopwatch_tick();
        s_state.is_counting = false;
    } else {
        stopwatch_resume();
    }
}

/** Return the value to be added (add=True) or subtracted (add=False) by the next increment_alarm().

    This doesn't count handling of decrements below 0.
*/
static time_t get_alarm_increment_diff(const IncrementMode incr, const bool add) {
    // Each increment adds 15s until 2 minutes, then by 30s until 5m etc
    const time_t diffs[3]      = {15,   30,   60      };
    const time_t thresholds[3] = {2*60, 5*60, TIME_MAX};
    size_t bucket = 0;
    for (; bucket < ARRAY_LENGTH(thresholds); bucket++){
        if ((s_state.alarm_duration - ((add || !s_state.alarm_duration) ? 0 : 1)) < thresholds[bucket]){
            break;
        }
    }

    time_t change = diffs[bucket];
    switch (incr) {
        case INCR_HOURS:
            change = MAX(SECONDS_PER_HOUR, change);
            break;
        case INCR_5MINS:
            change = MAX(5 * SECONDS_PER_MINUTE, change);
            break;
        case INCR_MINS:
            change = MAX(SECONDS_PER_MINUTE, change);
            break;
        case INCR_SECS:
            break;
        default:
            break;
    }
    return change;
}

/// Increment (add=True) or decrement (add=False) the alarm duration.
/// Return the next mode that would be reached via increment (add=True) or decrement (add=False)
static int get_next_mode(const bool add) {
    int next_mode = s_mode + (add ? 1 : -1);
    const time_t incr_diff = get_alarm_increment_diff((IncrementMode)next_mode, false);

    // Skip selection if the alarm duration is beyond that modification threshold
    if ((next_mode == MODE_MINS) && (incr_diff > SECONDS_PER_MINUTE)){
        next_mode += (add ? 2 : s_mode == MODE_SECS ? -1 : -2);
    } else if ((next_mode == MODE_SECS) && (incr_diff >= SECONDS_PER_MINUTE)){
        if (incr_diff > SECONDS_PER_MINUTE) {
            next_mode += (add ? 2 : -2);
        } else {
            next_mode += (add ? 1 : -1);
        }
    }

    return MIN(next_mode, MODE_MAX);
}

/// Increment (add=True) or decrement (add=False) the mode.
/******************************************************************************
 Alarm
******************************************************************************/

#define ALARM_PULSE_DURATION SECONDS_PER_MINUTE  // how long the alarm will ring before automatically stopping
static AppTimer* s_alarm_pulse_timer = NULL;

/// Return true if an alarm was pulsing
static bool alarm_clear(void) {
    const bool was_active = s_alarm_pulse_timer != NULL;
    if (was_active) {
        LOG("Clearing alarm");
        app_timer_cancel(s_alarm_pulse_timer);
        s_alarm_pulse_timer = NULL;
        vibes_cancel();
#if PBL_SPEAKER
        speaker_stop();
#endif // PBL_SPEAKER
        s_state.is_alarm_done = true;
    }
    return was_active;
}

#if PBL_SPEAKER
static void alarm_play_audio(void) {
    static const SpeakerNote beep = {
        .midi_note = 95,  // B6
        .waveform = SpeakerWaveformSquare,
        .duration_ms = 150,
        .velocity = 0
    };
    static const SpeakerNote silence = {
        .midi_note = 0,
        .waveform = SpeakerWaveformSine,
        .duration_ms = 100,
        .velocity = 0,
    };
    static const SpeakerNote notes[4] = {beep, silence, beep, silence};

    const uint8_t volume = config_get()->audioVolume;
    if ((volume > 0) && !speaker_is_muted()) {
        (void)speaker_play_notes(notes, ARRAY_LENGTH(notes), volume);
    }
}
#endif // PBL_SPEAKER

static void alarm_pulse(void) {
    switch (config_get()->alarmVibePattern) {
    case AlarmVibePattern_Double:
        LOG("ALARM PULSE! - double");
        vibes_double_pulse();
        break;
    case AlarmVibePattern_Short:
        LOG("ALARM PULSE! - short");
        vibes_short_pulse();
        break;
    case AlarmVibePattern_Long:
        LOG("ALARM PULSE! - long");
        vibes_long_pulse();
        break;
    case AlarmVibePattern_None:
        LOG("ALARM PULSE! - no vibe");
        break;
    default:
        ASSERT(false);
        vibes_double_pulse();
        break;
    }
#if PBL_SPEAKER
    alarm_play_audio();
#endif // PBL_SPEAKER
}

static bool alarm_is_pulsing(void) {
    return s_alarm_pulse_timer;
}

// Return the time at which alarm pulses would automatically end
static time_t alarm_get_pulse_end_time(void) {
    return s_state.start_time + s_state.alarm_duration + ALARM_PULSE_DURATION;
}

/// Repeat alarm_pulse() until ALARM_PULSE_DURATION is up
static void alarm_pulse_timer_handler(void* data) {
    alarm_pulse();
    if (time(NULL) < alarm_get_pulse_end_time()) {
        s_alarm_pulse_timer = app_timer_register(2000, alarm_pulse_timer_handler, NULL);
    } else {
        (void) alarm_clear();
    }
}

static bool alarm_should_start(void) {
    return (
        (s_state.alarm_duration > 0)
        && !s_alarm_pulse_timer  // already started
        && !s_state.is_alarm_done  // already started and finished
        && (s_state.elapsed_time >= s_state.alarm_duration)
    );
}

/// Trigger the alarm
static void alarm_start(void) {
    ASSERT(s_alarm_pulse_timer == NULL);
    ASSERT(!s_state.is_alarm_done);  // TODO fails sometimes
    alarm_pulse_timer_handler(NULL);
}

static void alarm_cancel_any_wakeup(void) {
    if (s_state.alarm_wakeup_id >= 0) {
        TRACE("CANCEL WAKEUP");
        wakeup_cancel(s_state.alarm_wakeup_id);
        s_state.alarm_wakeup_id = E_DOES_NOT_EXIST;
    }
}

/// Schedule (or cancel) the wakeup timer
static void alarm_schedule_any_wakeup(void) {
    TRACE("alarm_schedule_wakeup_timer");
    ASSERT(s_state.alarm_wakeup_id == E_DOES_NOT_EXIST);

    time_t alarm_time = stopwatch_get_alarm_time();
    if (s_state.is_counting && (alarm_time != 0)) {
        do {
            s_state.alarm_wakeup_id = wakeup_schedule(alarm_time, 0, true);
            alarm_time -= 1;
        } while (s_state.alarm_wakeup_id == E_RANGE);  // other app already scheduled wakeup within 1 minute
        alarm_time += 1;
        ASSERT(s_state.alarm_wakeup_id >= 0);

        const struct tm* alarm_time_local = localtime(&alarm_time);
        char time_str[MAX_TIME_TEXT_SIZE] = {0};
        const size_t num_bytes = strftime(time_str, sizeof(time_str), "%H:%M:%S", alarm_time_local);
        ASSERT(num_bytes);
        LOG("alarm_wakeup_id = %d at %s", s_state.alarm_wakeup_id, time_str);
    }
}

/// Reset the alarm if necessary
static void alarm_reset(void) {
    s_state.is_alarm_done = stopwatch_get_alarm_time() == 0;
}

/******************************************************************************
 UI updates
******************************************************************************/

static void click_config_provider(void *context);

// for elapsed and remaining
static SecDisplay seconds_elapsed_display_style(void) {
    const bool show = (s_update_rate == SECOND_UNIT) || !s_state.is_counting || s_initialising;
    return show ? SEC_SHOW : SEC_DASH;
}

static SecDisplay seconds_duration_display_style(void) {
    const bool show = (
        (s_mode == MODE_SECS)
        || ((s_state.alarm_duration % SECONDS_PER_MINUTE) != 0)
        || ((s_mode == MODE_CTRL) && (get_next_mode(false) == MODE_SECS))
    );
    return show ? SEC_SHOW : SEC_HIDE;
}

/// Note: Max icon size is 28x18, recommended 15x15
static void update_action_bar(void) {
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, NULL);
    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_DOWN, NULL);

    const GBitmap *select_icon = NULL;
    if (alarm_is_pulsing()) {
        select_icon = s_icon_tick;
    } else if (!s_state.is_counting && (s_state.alarm_duration > 0)) {
        select_icon = s_icon_start;
    }

    action_bar_layer_set_icon(s_action_bar, BUTTON_ID_SELECT, select_icon);
    action_bar_layer_set_click_config_provider(s_action_bar, click_config_provider);
}

// Update the primary large text and the secondary text below it
static void update_primary_and_secondary_text(void) {
    const bool is_stopwatch_mode = s_state.alarm_duration == 0;
    if (is_stopwatch_mode) {
        text_layer_set_text(s_text_layer_big_elapsed, s_elapsed_text);
    } else {
        text_layer_set_text(s_text_layer_big_remaining, s_remaining_text);
        text_layer_set_text(s_text_layer_small_elapsed, s_elapsed_text);
    }
    static bool was_big_remaining_visible = false;
    animate_scroll((Layer*)s_text_layer_big_remaining, !is_stopwatch_mode, false, &was_big_remaining_visible);
    static bool was_big_elapsed_visible = false;
    animate_scroll((Layer*)s_text_layer_big_elapsed, is_stopwatch_mode, true, &was_big_elapsed_visible);
    static bool was_secondary_visible = false;
    animate_scroll((Layer*)s_text_layer_small_elapsed, !is_stopwatch_mode, false, &was_secondary_visible);
}

static void update_status_icon(void) {
    const time_t remaining = s_state.alarm_duration - s_state.elapsed_time;

    bool show_status_icon = true;
    if (alarm_is_pulsing()) {
        bitmap_layer_set_bitmap(s_status_icon_layer, s_status_icon_alert);
    } else if (!s_state.is_counting) {
        bitmap_layer_set_bitmap(s_status_icon_layer, s_status_icon_pause);
    } else if (remaining > 0) {
        bitmap_layer_set_bitmap(s_status_icon_layer, s_status_icon_alarm);
    } else {
        show_status_icon = false;
    }
    static bool was_show_status_icon = false;
    animate_scroll(bitmap_layer_get_layer(s_status_icon_layer), show_status_icon, true, &was_show_status_icon);
}

static void update_alarm_time(void) {
    // TRACE("update_alarm_time");
    const bool show_alarm_time = s_state.alarm_duration > 0;
    if (show_alarm_time) {
        const time_t alarm_time = time(NULL) + s_state.alarm_duration - s_state.elapsed_time;
        const struct tm* alarm_time_local = localtime(&alarm_time);
        const size_t num_bytes = strftime(s_alarm_time_text, sizeof(s_alarm_time_text), time_fmt(), alarm_time_local);
        ASSERT(num_bytes);
        text_layer_set_text(s_text_layer_alarm_time, s_alarm_time_text);
    }

    static bool was_show_alarm_time = false;
    animate_scroll(text_layer_get_layer(s_text_layer_alarm_time), show_alarm_time, true, &was_show_alarm_time);

    update_primary_and_secondary_text();
    alarm_reset();
}

static void update_remaining(void) {
    // TRACE("update_remaining");
    if (s_state.alarm_duration > 0) {
        const time_t remaining = s_state.alarm_duration - s_state.elapsed_time;
        snprintf_hms(s_remaining_text, sizeof(s_remaining_text), remaining, true, seconds_elapsed_display_style());
    }
    update_status_icon();
    update_primary_and_secondary_text();
}

// Set the duration text, which depends on the mode
static void set_duration_text(void) {
    snprintf_hms(s_alarm_duration_text, sizeof(s_alarm_duration_text), s_state.alarm_duration,
                 false, seconds_duration_display_style());
    text_layer_set_text(s_text_layer_alarm_duration, s_alarm_duration_text);
}

static void update_mode(void) {
    const char* text = "^^";
    GRect frame = layer_get_frame((Layer*)s_text_layer_edit_indicator);
    const bool two_digit_hours = s_state.alarm_duration >= (10 * SECONDS_PER_HOUR);
    const bool has_days = s_state.alarm_duration >= SECONDS_PER_DAY;
    #define ADJUST_FOR_HIDDEN_SECONDS() MACRO_START \
        if (seconds_duration_display_style() != SEC_SHOW) { \
            frame.origin.x += 10; \
        } \
    MACRO_END
#if PBL_DISPLAY_HEIGHT >= 200
    #define ADJUST_FOR_LARGE_FONT() MACRO_START \
        frame.origin.x = (int16_t)((float)frame.origin.x) * (22.0 / 18.0); \
    MACRO_END
#else // PBL_DISPLAY_HEIGHT < 200
    #define ADJUST_FOR_LARGE_FONT()
#endif // PBL_DISPLAY_HEIGHT < 200
    // TODO I *really* wish this font was fixed-width :')
    switch (s_mode) {
        case MODE_HOURS:
            text = two_digit_hours ? "^^" : "^";
            frame.origin.x = has_days ? -19 : -26;
            ADJUST_FOR_HIDDEN_SECONDS();
            ADJUST_FOR_LARGE_FONT();
            break;
        case MODE_5MINS:
            frame.origin.x = has_days ? 2 : two_digit_hours ? -5 : -9;
            ADJUST_FOR_HIDDEN_SECONDS();
            ADJUST_FOR_LARGE_FONT();
            break;
        case MODE_MINS:
            text = "^";
            frame.origin.x = has_days ? 6 : two_digit_hours ? -1 : -5;
            ADJUST_FOR_HIDDEN_SECONDS();
            ADJUST_FOR_LARGE_FONT();
            break;
        case MODE_SECS:
            frame.origin.x = 16;
            ADJUST_FOR_LARGE_FONT();
            break;
        case MODE_CTRL:
            frame.origin.x = 150;
            break;
        default:
            ASSERT(false);
            break;
    }
    #undef ADJUST_FOR_LARGE_FONT
    #undef ADJUST_FOR_HIDDEN_SECONDS
    snprintf(s_edit_indicator_text, sizeof(s_edit_indicator_text), text);
    text_layer_set_text(s_text_layer_edit_indicator, s_edit_indicator_text);
    animation_schedule((Animation*)property_animation_create_layer_frame(
        (Layer*)s_text_layer_edit_indicator, NULL, &frame));

    set_duration_text();
}

static void update_alarm_duration(void) {
    TRACE("update_alarm_duration");
    // note set_duration_text() is called by update_mode
    update_mode();
    update_alarm_time();
}

static void update_elapsed(void) {
    // TRACE("update_elapsed");
    snprintf_hms(s_elapsed_text, sizeof(s_elapsed_text), s_state.elapsed_time, true, seconds_elapsed_display_style());
    update_remaining();
}

// Short vibe on any calls to stopwatch_toggle, stopwatch_restart or stopwatch_clear
static void vibe_for_start_stop(void) {
    TRACE("vibe_for_start_stop");
    static const uint32_t segments[] = {100};
    VibePattern pat = {
        .durations = segments,
        .num_segments = ARRAY_LENGTH(segments),
    };
    vibes_enqueue_custom_pattern(pat);
}

/******************************************************************************
 Handlers
******************************************************************************/

#if !PBL_PLATFORM_APLITE
void glance_reload_callback(AppGlanceReloadSession *session, size_t limit, void *context) {
    char subtitle[150] = {0};
    AppGlanceSlice slice = {
        .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION,
        .layout.icon = APP_GLANCE_SLICE_DEFAULT_ICON, // TODO "publishedResource"
        .layout.subtitle_template_string = subtitle,
    };

    #define ADD_SLICE() \
        MACRO_START \
            AppGlanceResult result = app_glance_add_slice(session, slice); \
            ASSERT(result == APP_GLANCE_RESULT_SUCCESS); \
        MACRO_END

    if (s_save && s_state.is_counting) {
        if (s_state.alarm_duration > 0) {
            const time_t alarm_time = s_state.start_time + s_state.alarm_duration;
            snprintf_time(subtitle, sizeof(subtitle), "Expired at %s", alarm_time);
            ADD_SLICE();
            if (s_state.elapsed_time < s_state.alarm_duration) {
                snprintf_time(subtitle, sizeof(subtitle), "Alarm set for %s", alarm_time);
                slice.expiration_time = alarm_time;
                ADD_SLICE();
            }
        } else {
            snprintf_time(subtitle, sizeof(subtitle), "Running since %s", s_state.start_time);
            ADD_SLICE();
        }
    }

    #undef ADD_SLICE
}
#endif // !PBL_PLATFORM_APLITE

// clear the entire app back to nothing
#if PBL_TOUCH
// set the duration straight to a new value
#endif // PBL_TOUCH

// restart the timer, keeping the alarm duration
// pause/unpause the timer
static void do_toggle_pause(void) {
    vibe_for_start_stop();
    stopwatch_toggle();
    update_elapsed();  // to unhide seconds and update status icon
}

// modify the alarm duration
// return true if the alarm was cleared
static bool do_alarm_clear(void) {
    const bool cleared = alarm_clear();
    if (cleared) {
        update_action_bar();
        update_status_icon();
    }
    return cleared;
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    // Note this function gets called with NULL by minute_tick_timer_callback
    UNUSED(tick_time);
    UNUSED(units_changed);

    // TRACE("tick_handler");
    if (s_state.is_counting) {
        stopwatch_tick();
        update_elapsed();
        if (alarm_should_start()) {
            alarm_start();
            update_action_bar();
            update_status_icon();
        }
    } else {
        update_alarm_time();
    }

}

static void update_tick_subscription(TimeUnits new_update_rate);
static AppTimer* s_tick_reschedule_timer = NULL;

// Timer callback which changes the rate of tick_handler t after its timer expires.
// WARNING: `void* data`'s pointer value is directly interpreted as a TimeUnits, it isn't a pointer to a TimeUnits!
static void update_rate_timer_callback(void* data) {
    TRACE("update_rate_timer_minutes_callback");
    s_tick_reschedule_timer = NULL;
    update_tick_subscription((TimeUnits)data);
}

// schedule/deschedule a timer for the next call to update_tick_subscription()
// timeout_ms 0 to deschedule
static void schedule_tick_subscription_update(uint32_t timeout_ms, TimeUnits update_rate) {
    static TimeUnits scheduled_update_rate = 0;
    if (timeout_ms == 0) {
        LOG("Scheduled update rate -> never");
        if (s_tick_reschedule_timer != NULL) {
            app_timer_cancel(s_tick_reschedule_timer);
            s_tick_reschedule_timer = NULL;
        }
    } else {
        LOG("Scheduled update rate -> %d in %ums", update_rate, timeout_ms);
        if (s_tick_reschedule_timer == NULL) {
            s_tick_reschedule_timer = app_timer_register(timeout_ms, update_rate_timer_callback, (void*)update_rate);
            ASSERT(s_tick_reschedule_timer != NULL);
        } else {
            if (scheduled_update_rate == update_rate) {
                app_timer_reschedule(s_tick_reschedule_timer, timeout_ms);
            } else {
                app_timer_cancel(s_tick_reschedule_timer);
                s_tick_reschedule_timer = app_timer_register(timeout_ms, update_rate_timer_callback, (void*)update_rate);
                ASSERT(s_tick_reschedule_timer != NULL);
            }
        }
    }
    scheduled_update_rate = update_rate;
}

// Recurring timer to trigger tick_handler() on every minute of elapsed/remaining time instead of clock time.
static AppTimer* s_minute_tick_timer = NULL;
static void minute_tick_timer_subscribe(bool subscribe);
static void minute_tick_timer_callback(void* context) {
    UNUSED(context);
    s_minute_tick_timer = NULL;
    tick_handler(NULL, MINUTE_UNIT);
    minute_tick_timer_subscribe(true);
}
static void minute_tick_timer_subscribe(bool subscribe) {
    if (s_minute_tick_timer != NULL) {
        app_timer_cancel(s_minute_tick_timer);
        s_minute_tick_timer = NULL;
    }
    if (subscribe) {
        bool is_counting_up = true;
        time_t now;
        if (s_state.alarm_duration > 0) {
            now = (s_state.alarm_duration - s_state.elapsed_time);
            is_counting_up = now < 0;
        } else {
            now = s_state.elapsed_time;
        }
        const uint32_t seconds_to_next_min = (
            is_counting_up
            ? (SECONDS_PER_MINUTE - ABS(now % SECONDS_PER_MINUTE))
            : ((now % SECONDS_PER_MINUTE) + 1)
        );
        TRACE("minute_tick_timer_subscribe %ds", seconds_to_next_min);
        s_minute_tick_timer = app_timer_register(seconds_to_next_min * MS_PER_S, minute_tick_timer_callback, NULL);
    }
}

/** Control the state update rate.
    To save power, update as little as possible.

    We only want to update seconds if the user is probably looking:
        - if we're in "stopwatch mode" and 1min hasn't elapsed yet
        - there's less than 3mins remaining on the timer (and every repeat of the ring on overtime)
        - if the alarm duration is very short (including after the alarm has expired; show looping overtime)
        - if there are any signs of user activity (buttons, accel-tap/shake, battery charger, screen touch)
        - if the alarm is pulsing (especially because the overtime counter is below ALARM_PULSE_DURATION)
*/
static void update_tick_subscription(TimeUnits new_update_rate) {
    TRACE("update_tick_subscription");
    const Config* config = config_get();

    const bool is_timer_enabled = (s_state.alarm_duration > 0);
    // Time until the elapsed time is close to a multiple of the alarm duration
    // i.e. the green or red ring is close to completion.
    const time_t time_to_next_alarm = (
        is_timer_enabled ? (s_state.alarm_duration - (s_state.elapsed_time % s_state.alarm_duration))
        : 0  // note: this condition avoids % 0
    );
    const int32_t short_alarm_s = config->shortAlarmMinutes * SECONDS_PER_MINUTE;
    const bool is_short_timer = is_timer_enabled && (time_to_next_alarm <= short_alarm_s);

    // Override the new update rate in some circumstances
    if (!s_state.is_counting) {
        // if paused, the only thing that can change is the alarm time, which only shows minutes
        // so force a slower update rate than requested
        if (stopwatch_get_alarm_time()) {
            LOG("Forced minute update rate");
            new_update_rate = MINUTE_UNIT;
        } else {
            LOG("Forced disabled update rate");
            new_update_rate = MONTH_UNIT;
        }
    } else if (alarm_is_pulsing() || is_short_timer) {
        // full rate while timer is short or pulsing
        LOG("Forced second update rate");
        new_update_rate = SECOND_UNIT;
    }

    // set the new update rate
    if (s_update_rate != new_update_rate) {
        s_update_rate = new_update_rate;
        if (new_update_rate == MINUTE_UNIT) {
            minute_tick_timer_subscribe(true);
            tick_timer_service_unsubscribe();
        } else {
            minute_tick_timer_subscribe(false);
            tick_timer_service_subscribe(new_update_rate, tick_handler);
        }
        tick_handler(NULL, 0);  // update to new layout
        LOG("Update rate changed to %d", new_update_rate);
    }

    // schedule changing the update rate in future
    if (!s_state.is_counting) {
        schedule_tick_subscription_update(0, 0);  // unschedule; only woken by user activity
    } else {  // is_counting
        if (new_update_rate == SECOND_UNIT) {
            // decide when to drop back to minutes
            if ((!is_timer_enabled && (config->shortStopwatchMinutes == MAX_POWER_SAVING_THRESHOLD))
                || (is_timer_enabled && (config->shortAlarmMinutes == MAX_POWER_SAVING_THRESHOLD))) {
                schedule_tick_subscription_update(0, 0);  // unschedule; never drop back to minutes
            } else if (is_short_timer) {
                // back to minutes when the timer expires
                // (note actually, the "alarm_is_pulsing" condition will then kick in to stay on seconds)
                schedule_tick_subscription_update(time_to_next_alarm * MS_PER_S, MINUTE_UNIT);
            } else {  // !is_short_timer
                int32_t high_rate_timeout_ms = (config->backlightDurationS * MS_PER_S) + LIGHT_FADE_TIME_MS;
                const int32_t short_stopwatch_s = config->shortStopwatchMinutes * SECONDS_PER_MINUTE;
                const bool is_short_stopwatch = !is_timer_enabled && (s_state.elapsed_time < short_stopwatch_s);
                if (is_short_stopwatch) {
                    // back to minutes when the stopwatch gets high enough
                    high_rate_timeout_ms = (uint32_t) MAX(high_rate_timeout_ms,
                                                          ((int32_t)short_stopwatch_s - (int32_t)s_state.elapsed_time) * MS_PER_S);
                } else if (alarm_is_pulsing()){
                    // back to minutes when the alarm pulses expire automatically
                    high_rate_timeout_ms = (uint32_t) MAX(high_rate_timeout_ms,
                                                          ABSDIFF(alarm_get_pulse_end_time(), time(NULL)) * MS_PER_S);
                } else {
                    // back to minutes after config->backlightDurationS
                }
                schedule_tick_subscription_update(high_rate_timeout_ms, MINUTE_UNIT);
            }
        } else if (new_update_rate == MINUTE_UNIT) {
            if (!is_timer_enabled || ((config->shortAlarmMinutes == 0) && (s_state.alarm_duration < s_state.elapsed_time))) {
                // stay on minutes indefinitely; back to seconds only on user activity
                schedule_tick_subscription_update(0, 0);
            } else {
                // set the timer to go back to seconds when the is_short_timer condition will become true
                schedule_tick_subscription_update((time_to_next_alarm + 1 - short_alarm_s) * MS_PER_S, SECOND_UNIT);
            }
        } else {
            // the only other new_update_rate is MONTH_UNIT, which only happens when !is_counting
            ASSERT(false); // should be unreachable
            schedule_tick_subscription_update(0, 0);
        }
    }
}

// Modified by Maru Kitano, 2026.
// Simplified interaction model: touch selects, Select starts, Back exits.
static void simple_timer_reset_to_idle(void) {
#if PBL_TOUCH
    touch_reset_idle();
#endif
    (void)alarm_clear();
    alarm_cancel_any_wakeup();

    s_state.alarm_duration = 0;
    s_state.start_time = 0;
    s_state.elapsed_time = 0;
    s_state.is_counting = false;
    s_state.is_alarm_done = false;
    s_state.alarm_wakeup_id = E_DOES_NOT_EXIST;

    s_save = false;
    s_mode = MODE_CTRL;

    update_alarm_duration();
    update_elapsed();
    update_mode();
    update_action_bar();
}

static void simple_timer_select_duration(time_t duration) {
    (void)alarm_clear();
    alarm_cancel_any_wakeup();

    s_state.alarm_duration = MAX((time_t)0, duration);
    s_state.start_time = 0;
    s_state.elapsed_time = 0;
    s_state.is_counting = false;
    s_state.is_alarm_done = false;
    s_state.alarm_wakeup_id = E_DOES_NOT_EXIST;

    s_save = false;
    s_mode = MODE_CTRL;

    update_alarm_duration();
    update_elapsed();
    update_mode();
    update_action_bar();
}

static void simple_timer_start(void) {
    if (s_state.is_counting || (s_state.alarm_duration <= 0)) {
        return;
    }

    vibe_for_start_stop();
    s_state.start_time = time(NULL);
    s_state.elapsed_time = 0;
    s_state.is_counting = true;
    s_state.is_alarm_done = false;
    s_save = true;
    s_mode = MODE_CTRL;

#if PBL_TOUCH
    touch_start_running((uint32_t)s_state.alarm_duration);
#endif

    update_alarm_duration();
    update_elapsed();
    update_mode();
    update_action_bar();
    update_tick_subscription(SECOND_UNIT);
}

static void simple_timer_exit(void) {
    if (alarm_is_pulsing()) {
        simple_timer_reset_to_idle();
    } else {
        stopwatch_tick();
        // Preserve every unfinished timer, including a paused one.
        s_save = (
            (s_state.alarm_duration > 0)
            && (s_state.elapsed_time < s_state.alarm_duration)
        );
    }

    window_stack_pop(true);
}

#if PBL_TOUCH

// handle new touch time selection
static void handle_touch_selection(bool is_duration, uint8_t hours, uint8_t minutes, uint8_t seconds) {
    time_t duration = 0;

    if (is_duration) {
        duration = (
            (hours * SECONDS_PER_HOUR)
            + (minutes * SECONDS_PER_MINUTE)
            + seconds
        );
    } else {
        const time_t now = time(NULL);
        struct tm target = *localtime(&now);
        target.tm_hour = hours;
        target.tm_min = minutes;
        target.tm_sec = 0;

        time_t target_time = mktime(&target);
        if (target_time <= now) {
            target.tm_mday += 1;
            target_time = mktime(&target);
        }
        duration = target_time - now;
    }

    simple_timer_select_duration(duration);
    update_tick_subscription(SECOND_UNIT);
}

// handle raw touch events
static void handle_touch_event(const TouchEvent *event, void *context) {
    update_tick_subscription(SECOND_UNIT);
}

static void enable_touch(void) {
    touch_enable(config_get()->enableTouch);
}

#else // !PBL_TOUCH

static void enable_touch(void) {
    // nothing
}

#endif  // !PBL_TOUCH

#if PBL_BACKLIGHT_SERVICE
// Subscribing to the backlight handler mops up any other activity events
// (e.g. touch-to-wake) that we can't subscribe to directly.
// We can't rely on backlight alone because it might be set to always-off or always-on.
static void backlight_handler(bool on){
    if (on) {
        TRACE("backlight_handler on");
        update_tick_subscription(SECOND_UNIT);
    }
}
#endif // PBL_BACKLIGHT_SERVICE

static void battery_state_handler(BatteryChargeState charge) {
    TRACE("battery_state_handler");
    static bool was_plugged = false;
    if (charge.is_plugged != was_plugged) {
        update_tick_subscription(SECOND_UNIT);
        // note: deliberately not enabling touch for this event
        was_plugged = charge.is_plugged;
    }
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    TRACE("accel_tap_handler");
    update_tick_subscription(SECOND_UNIT);
    enable_touch();
}

#if PBL_TOUCH
static void run_screen_add_minute_callback(void) {
    if (!touch_running_screen_active()
        || s_state.is_counting
        || alarm_is_pulsing()) {
        return;
    }

    if (s_state.alarm_duration
        > TIME_MAX - SECONDS_PER_MINUTE) {
        return;
    }

    vibe_for_start_stop();
    s_state.alarm_duration += SECONDS_PER_MINUTE;
    touch_add_running_seconds(SECONDS_PER_MINUTE);

    update_alarm_duration();
    update_elapsed();
    update_action_bar();
    update_tick_subscription(SECOND_UNIT);
}

static void run_screen_delete_callback(void) {
    if (!touch_running_screen_active()
        || s_state.is_counting
        || alarm_is_pulsing()) {
        return;
    }

    vibe_for_start_stop();
    simple_timer_reset_to_idle();
    update_tick_subscription(SECOND_UNIT);
}
#endif // PBL_TOUCH

static void up_click_handler(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

#if PBL_TOUCH
    if (touch_running_screen_active()) {
        if (!s_state.is_counting) {
            run_screen_add_minute_callback();
        }

        enable_touch();
        return;
    }

    touch_adjust_minutes(1);
#endif

    enable_touch();
}

static void down_click_handler(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

#if PBL_TOUCH
    if (touch_running_screen_active()) {
        if (!s_state.is_counting) {
            run_screen_delete_callback();
        }

        enable_touch();
        return;
    }

    touch_adjust_minutes(-1);
#endif

    enable_touch();
}

#if PBL_TOUCH
static void run_screen_pause_toggle_callback(void) {
    // The alarm may begin during the short marker animation.
    if (alarm_is_pulsing()) {
        return;
    }

    do_toggle_pause();
    touch_set_paused(!s_state.is_counting);
    update_action_bar();
    update_tick_subscription(SECOND_UNIT);
}
#endif

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    UNUSED(recognizer);
    UNUSED(context);

    if (do_alarm_clear()) {
        simple_timer_reset_to_idle();
#if PBL_TOUCH
    } else if (touch_running_screen_active()) {
        return;
#endif
    } else {
        simple_timer_start();
    }

    enable_touch();
}

static bool s_select_run_screen_press;

static void select_raw_down_handler(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

#if PBL_TOUCH
    if (touch_running_screen_active() && !alarm_is_pulsing()) {
        s_select_run_screen_press = true;
        (void)touch_run_action_press(
            run_screen_pause_toggle_callback
        );
        return;
    }
#endif

    s_select_run_screen_press = false;
}

static void select_raw_up_handler(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

#if PBL_TOUCH
    if (s_select_run_screen_press) {
        s_select_run_screen_press = false;
        touch_run_action_release();
        enable_touch();
        return;
    }
#endif

    select_click_handler(NULL, NULL);
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
    UNUSED(recognizer);
    UNUSED(context);
    simple_timer_exit();
}

static void back_release_click_handler(
    ClickRecognizerRef recognizer,
    void *context
) {
    UNUSED(recognizer);
    UNUSED(context);

#if PBL_TOUCH
    if (touch_running_screen_active()
        && !alarm_is_pulsing()) {
        // This handler is called only after the hardware button is released.
        // Play the full visual press and return, then minimize.
        if (touch_minimize_action_press()) {
            touch_minimize_action_release(simple_timer_exit);
            enable_touch();
            return;
        }
    }
#endif

    back_click_handler(NULL, NULL);
}

static void click_config_provider(void *context) {
    UNUSED(context);
#if PBL_TOUCH
    window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
#endif
    window_raw_click_subscribe(
        BUTTON_ID_SELECT,
        select_raw_down_handler,
        select_raw_up_handler,
        NULL
    );
    window_multi_click_subscribe(
        BUTTON_ID_BACK,
        1,
        1,
        0,
        true,
        back_release_click_handler
    );
}

/******************************************************************************
 Graphics
******************************************************************************/

#if PBL_COLOR
#endif // PBL_COLOR

#if PBL_ROUND
// Draw a custom rounded StatusBar background
#endif // PBL_ROUND

// Render background elements, including the progress ring
static void render_background(Layer *layer, GContext *ctx) {
    const GRect bounds = layer_get_bounds(layer);
    graphics_context_set_fill_color(ctx, config_get()->bgColor);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
}

static void set_text_colors(const Config* config) {
    const GColor text_color = config->textColor;
    text_layer_set_text_color(s_text_layer_edit_indicator, text_color);
    text_layer_set_text_color(s_text_layer_alarm_duration, text_color);
    text_layer_set_text_color(s_text_layer_alarm_time,     text_color);
    text_layer_set_text_color(s_text_layer_big_remaining,  text_color);
    text_layer_set_text_color(s_text_layer_big_elapsed,    text_color);
    text_layer_set_text_color(s_text_layer_small_elapsed,  text_color);
}

static void set_bitmap_colors(const Config* config) {

    // Actionbar icon palettes are white fill with black outline {0: Black, 1-2: Clear, 3: White}
    const size_t action_fill_index = 3;
    const size_t action_line_index = 0;
    const GColor action_fill_color = config->actionBarIconColor;
    gbitmap_set_color(s_icon_start,   action_fill_index, action_fill_color);
    gbitmap_set_color(s_icon_tick,    action_fill_index, action_fill_color);
    const GColor action_line_color = config->roundIconOutline ? config->actionBarBgColor : GColorClear;
    gbitmap_set_color(s_icon_start,   action_line_index, action_line_color);
    gbitmap_set_color(s_icon_tick,    action_line_index, action_line_color);

    // Other bitmaps' palettes are {0: Clear, 1: White}
    const size_t color_index = 1;

    // status
    const GColor text_color = config->textColor;
    gbitmap_set_color(s_status_icon_alarm, color_index, text_color);
    gbitmap_set_color(s_status_icon_alert, color_index, text_color);
    gbitmap_set_color(s_status_icon_pause, color_index, text_color);

}

// Handle new app config submission
static void new_config_handler(const Config* config) {
    UNUSED(config);
    set_text_colors(config);
    set_bitmap_colors(config);

#if PBL_RECT
    action_bar_layer_set_background_color(s_action_bar, config->actionBarBgColor);
#endif // PBL_RECT

#if PBL_TOUCH
    touch_enable(config->enableTouch);
#endif // PBL_TOUCH

    update_tick_subscription(SECOND_UNIT);

    layer_mark_dirty(window_get_root_layer(s_main_window));
}

/******************************************************************************
 Main
******************************************************************************/

// Create the resources for the small icon at the top
static void create_status_icon(Layer* parent, int16_t alarm_text_y) {
    const GRect bounds = layer_get_bounds(parent);

    s_status_icon_alarm = gbitmap_create_with_resource(RESOURCE_ID_ALARM);
    s_status_icon_alert = gbitmap_create_with_resource(RESOURCE_ID_ALERT);
    s_status_icon_pause = gbitmap_create_with_resource(RESOURCE_ID_PAUSE);
    const GSize size = gbitmap_get_bounds(s_status_icon_alarm).size;

    const GRect status_icon_frame = {
        .origin = {
            (bounds.size.w / 2) - (size.w / 2),
            alarm_text_y - size.h + 1
        },
        .size = size
    };

    s_status_icon_layer = bitmap_layer_create(status_icon_frame);
    bitmap_layer_set_bitmap(s_status_icon_layer, s_status_icon_alarm);
    bitmap_layer_set_compositing_mode(s_status_icon_layer, GCompOpSet);  // enable transparency
    bitmap_layer_set_background_color(s_status_icon_layer, GColorClear);
    layer_add_child(parent, bitmap_layer_get_layer(s_status_icon_layer));
}

static void create_text_layout(Layer* parent) {
    const GRect bounds = layer_get_bounds(parent);

#if PBL_DISPLAY_HEIGHT >= 200
    const GFont small_text_font = fonts_get_system_font(FONT_KEY_GOTHIC_24);  // TODO maybe make bold?
    const int16_t small_text_h = 24;
#else // PBL_DISPLAY_HEIGHT < 200
    const GFont small_text_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    const int16_t small_text_h = 18;
#endif  // PBL_DISPLAY_HEIGHT < 200

    const GFont main_text_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    const int16_t main_text_h = 28;

    // TODO redo this layout to distribute evenly from the centre
    int16_t first_text_y = (bounds.size.h / 4);
    int16_t status_icon_bottom_y = 0;
    int16_t spacing;
#if PBL_DISPLAY_HEIGHT < 180
    spacing = 3;
#elif PBL_DISPLAY_HEIGHT < 200
    spacing = 4;
#elif PBL_DISPLAY_HEIGHT < 240
    spacing = 5;
#else // PBL_DISPLAY_HEIGHT >= 240
    spacing = 6;
    first_text_y += 4;
    status_icon_bottom_y -= 3;
#endif // PBL_DISPLAY_HEIGHT

#if PBL_PLATFORM_CHALK
    first_text_y += 3;  // ring is slightly wider to accommodate actionbar icons
    spacing -= 1;
#endif // PBL_PLATFORM_CHALK

    first_text_y -= spacing;
    status_icon_bottom_y += first_text_y;

    const int16_t second_text_y = first_text_y + small_text_h + spacing;
    const int16_t main_text_y = second_text_y + small_text_h + (spacing * 2);

    create_status_icon(parent, status_icon_bottom_y);

    #define SMALL_TEXT(name, x_loc, y_loc) \
    MACRO_START \
        s_text_layer_##name = text_layer_create(GRect(x_loc, y_loc, bounds.size.w, small_text_h)); \
        text_layer_set_text(s_text_layer_##name, s_##name##_text); \
        text_layer_set_text_alignment(s_text_layer_##name, GTextAlignmentCenter); \
        text_layer_set_font(s_text_layer_##name, small_text_font); \
        text_layer_set_background_color(s_text_layer_##name, GColorClear); \
    MACRO_END

    // alarm time
    SMALL_TEXT(alarm_time, 0, first_text_y);
    layer_add_child(parent, text_layer_get_layer(s_text_layer_alarm_time));

    // duration
    s_duration_layer = layer_create(GRect(0, second_text_y, bounds.size.w, small_text_h * 2));
    layer_add_child(parent, s_duration_layer);

    SMALL_TEXT(alarm_duration, 0, 0);
    layer_add_child(s_duration_layer, text_layer_get_layer(s_text_layer_alarm_duration));

    SMALL_TEXT(edit_indicator, -150, small_text_h - 3);
    layer_add_child(s_duration_layer, text_layer_get_layer(s_text_layer_edit_indicator));

    // primary text (elapsed or remaining)
    const uint16_t border_w = 9;  // tweaked to get the desired auto-truncation on smallest screens
    #define BIG_TEXT(name); \
    MACRO_START \
        s_text_layer_big_##name = text_layer_create( \
            GRect(border_w, main_text_y, bounds.size.w - (border_w * 2), main_text_h)); \
        text_layer_set_text(s_text_layer_big_##name, s_##name##_text); \
        text_layer_set_text_alignment(s_text_layer_big_##name, GTextAlignmentCenter); \
        text_layer_set_font(s_text_layer_big_##name, main_text_font); \
        text_layer_set_background_color(s_text_layer_big_##name, GColorClear); \
        layer_add_child(parent, text_layer_get_layer(s_text_layer_big_##name)); \
    MACRO_END

    // only one is shown at a time; update_primary_and_secondary_text() swaps between them
    BIG_TEXT(elapsed);
    BIG_TEXT(remaining);
    #undef BIG_TEXT

    // secondary text; elapsed while remaining is in the primary slot
    #define s_small_elapsed_text s_elapsed_text
    SMALL_TEXT(small_elapsed, 0, main_text_y + main_text_h + spacing);
    layer_add_child(parent, text_layer_get_layer(s_text_layer_small_elapsed));
    #undef s_small_elapsed_text

    #undef SMALL_TEXT

    // Hide all legacy timer text and status graphics.
    layer_set_hidden(text_layer_get_layer(s_text_layer_edit_indicator), true);
    layer_set_hidden(text_layer_get_layer(s_text_layer_alarm_duration), true);
    layer_set_hidden(text_layer_get_layer(s_text_layer_alarm_time), true);
    layer_set_hidden(text_layer_get_layer(s_text_layer_big_remaining), true);
    layer_set_hidden(text_layer_get_layer(s_text_layer_big_elapsed), true);
    layer_set_hidden(text_layer_get_layer(s_text_layer_small_elapsed), true);
    layer_set_hidden(bitmap_layer_get_layer(s_status_icon_layer), true);
}

static void touch_auto_start_wrapper(void) {
    select_click_handler(NULL, NULL);
}

static void main_window_load(Window *window) {
    TRACE("main_window_load");

    config_init(new_config_handler);

    Layer *window_layer = window_get_root_layer(window);

    // background
    s_bg_layer = layer_create(layer_get_bounds(window_layer));
    layer_set_update_proc(s_bg_layer, render_background);
    layer_add_child(window_layer, s_bg_layer);

    // text
    create_text_layout(s_bg_layer);

    // action bar
    s_action_bar = action_bar_layer_create();
#if PBL_ROUND
    action_bar_layer_set_background_color(s_action_bar, GColorClear);
#endif // PBL_ROUND
    action_bar_layer_add_to_window(s_action_bar, window);
    action_bar_layer_set_click_config_provider(s_action_bar, click_config_provider);
    s_icon_start = gbitmap_create_with_resource(RESOURCE_ID_ICON_START);
    s_icon_tick = gbitmap_create_with_resource(RESOURCE_ID_ICON_TICK);

    // touch selector
#if PBL_TOUCH
    touch_create(window_layer, &handle_touch_selection, &handle_touch_event, touch_auto_start_wrapper);
#endif // PBL_TOUCH

    // business logic
    s_initialising = true;
    const bool loaded_state = stopwatch_load();
    if (loaded_state) {
        stopwatch_tick();
        stopwatch_delete();
    } else {
        s_state.alarm_duration = 0;
        s_state.start_time = 0;
        s_state.elapsed_time = 0;
        s_state.is_counting = false;
        s_state.is_alarm_done = false;
        s_state.alarm_wakeup_id = E_DOES_NOT_EXIST;
    }

    if (
        (launch_reason() != APP_LAUNCH_WAKEUP)
        && (
            (s_state.alarm_duration <= 0)
            || (s_state.elapsed_time >= s_state.alarm_duration)
        )
    ) {
        s_state.alarm_duration = 0;
        s_state.start_time = 0;
        s_state.elapsed_time = 0;
        s_state.is_counting = false;
        s_state.is_alarm_done = false;
        s_state.alarm_wakeup_id = E_DOES_NOT_EXIST;
    }

#if PBL_TOUCH
    const bool restore_run_screen =
        loaded_state
        && (s_state.alarm_duration > 0)
        && (s_state.elapsed_time < s_state.alarm_duration);

    if (restore_run_screen) {
        const time_t remaining_seconds =
            s_state.alarm_duration - s_state.elapsed_time;

        touch_restore_running(
            (uint32_t)remaining_seconds,
            !s_state.is_counting
        );
    }
#endif // PBL_TOUCH

    s_save = false;
    s_mode = MODE_CTRL;

    update_mode();
    update_alarm_duration();
    update_elapsed();

    if (
        (launch_reason() == APP_LAUNCH_WAKEUP)
        && alarm_should_start()
    ) {
        alarm_start();
    }

    update_action_bar();
    alarm_cancel_any_wakeup();

    // services
    update_tick_subscription(SECOND_UNIT);
    accel_tap_service_subscribe(accel_tap_handler);
    battery_state_service_subscribe(battery_state_handler);
#if PBL_BACKLIGHT_SERVICE
    backlight_service_subscribe(backlight_handler);
#endif // PBL_BACKLIGHT_SERVICE

    new_config_handler(config_get());

    s_initialising = false;
}

static void main_window_unload(Window *window) {
    TRACE("main_window_unload");

    // background
    layer_destroy(s_bg_layer);

    // status icon
    gbitmap_destroy(s_status_icon_alarm);
    gbitmap_destroy(s_status_icon_alert);
    gbitmap_destroy(s_status_icon_pause);
    bitmap_layer_destroy(s_status_icon_layer);

    // text
    layer_destroy(s_duration_layer);
    text_layer_destroy(s_text_layer_edit_indicator);
    text_layer_destroy(s_text_layer_alarm_duration);
    text_layer_destroy(s_text_layer_alarm_time);
    text_layer_destroy(s_text_layer_big_remaining);
    text_layer_destroy(s_text_layer_big_elapsed);
    text_layer_destroy(s_text_layer_small_elapsed);

    // action bar
    action_bar_layer_destroy(s_action_bar);
    gbitmap_destroy(s_icon_start);
    gbitmap_destroy(s_icon_tick);

    // touch selector
#if PBL_TOUCH
    touch_destroy();
#endif // PBL_TOUCH

    // services
    tick_timer_service_unsubscribe();
    accel_tap_service_unsubscribe();
    battery_state_service_unsubscribe();
#if PBL_BACKLIGHT_SERVICE
    backlight_service_unsubscribe();
#endif // PBL_BACKLIGHT_SERVICE

    // business logic
    if (!alarm_clear() && s_save){
        alarm_schedule_any_wakeup();
    }
    if (s_save) {
        stopwatch_save();
    }

#if !PBL_PLATFORM_APLITE
    app_glance_reload(glance_reload_callback, NULL);
#endif // !PBL_PLATFORM_APLITE
}

static void init(void) {
    LOG("Init");
    s_main_window = window_create();
    window_set_click_config_provider(s_main_window, click_config_provider);
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload,
    });
    window_stack_push(s_main_window, true);
}

static void deinit(void) {
    window_destroy(s_main_window);
}

int main(void) {
    DEMO_BACKLIGHT_ENABLE(true);

    init();
    app_event_loop();
    deinit();

    DEMO_BACKLIGHT_ENABLE(false);
}

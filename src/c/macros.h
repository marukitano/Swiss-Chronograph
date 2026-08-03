// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// Generic C macros.

#pragma once

// Macros
#define MACRO_START do {
#define MACRO_END } while (0)

// Misc
#define UNUSED(x) ((void)(x))

// Maths
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ABS(a) (((a) >= 0) ? (a) : ((a) * -1))
#define ABSDIFF(a, b) ((a) >= (b) ? (a) - (b) : (b) - (a))

// Debugging
#if DEBUG
    #define LOG(...) APP_LOG(APP_LOG_LEVEL_INFO, __VA_ARGS__)
    #define TRACE(...) APP_LOG(APP_LOG_LEVEL_DEBUG, __VA_ARGS__)
    #define ASSERT(condition) if (!(condition)) APP_LOG(APP_LOG_LEVEL_ERROR, \
        "ASSERTION FAILED AT %s:%d - "#condition, __FILE__, __LINE__)
    #define PROFILE_START() const uint32_t _PROFILE_start_ms = timestamp_ms()
    #define PROFILE_END(name) MACRO_START \
        const uint32_t _PROFILE_end_ms = timestamp_ms(); \
        LOG(name": %ums", _PROFILE_end_ms - _PROFILE_start_ms); \
        MACRO_END

#else // !DEBUG
    #define LOG(...)
    #define TRACE(...)
    #define ASSERT(condition) ((void)(condition))
    #define PROFILE_START()
    #define PROFILE_END(name)
#endif // !DEBUG

#define STATIC_ASSERT(condition) _Static_assert((condition), #condition)

// Strings

// Constants
#define TIME_MAX INT32_MAX  // max value of time_t
#define MS_PER_S (1000)  // milliseconds per second

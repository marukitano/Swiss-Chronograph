// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/** App configuration via Clay (pebble package install @rebble/clay)

    To use this module, you must define the following:
        PERSIST_KEY_CONFIG_VERSION and PERSIST_KEY_CONFIG (see persist_keys.h)
        A file named config_defs.h which defines:
            X_CONFIG_OPTIONS
            PERSIST_CONFIG_VERSION

    TODO:
        Don't take over the entire app message inbox.
*/

#include "config.h"

#include <pebble.h>

#include "macros.h"
#include "persist_keys.h"

#define CONFIG_STRUCT_DEFAULT(_conftype, _ctype, _message_key, _default) ._message_key = _default,
static Config s_config = {
    X_CONFIG_OPTIONS(CONFIG_STRUCT_DEFAULT)
};
#undef CONFIG_STRUCT_DEFAULT

static NewConfigCallback s_new_config_callback = NULL;


/******************************************************************************
 Private methods; Local watch persistence
******************************************************************************/


static void local_persist_save(void);

static void local_persist_load(void) {
    const int32_t stored_version =
        persist_read_int(PERSIST_KEY_CONFIG_VERSION);

    StatusCode status = E_DOES_NOT_EXIST;

    if (stored_version == PERSIST_CONFIG_VERSION) {
        status = persist_read_data(
            PERSIST_KEY_CONFIG,
            &s_config,
            sizeof(s_config)
        );
    } else if (
        stored_version == 3
        || stored_version == 2
    ) {
        // Versions 2 and 3 already use the current compact Config layout.
        // Preserve every user setting. Only migrate defaults introduced by
        // the two cleanup revisions:
        // - version 2 accidentally defaulted to silent
        // - version 3 accidentally defaulted to 50%
        status = persist_read_data(
            PERSIST_KEY_CONFIG,
            &s_config,
            sizeof(s_config)
        );

        if (status == sizeof(s_config)) {
            const bool migrate_version_2_default =
                stored_version == 2
                && s_config.audioVolume == 0;

            const bool migrate_version_3_default =
                stored_version == 3
                && s_config.audioVolume == 50;

            if (
                migrate_version_2_default
                || migrate_version_3_default
            ) {
                s_config.audioVolume =
                    DEFAULT_AUDIO_VOLUME;
            }

            local_persist_save();
        }
    }

    if (status == sizeof(s_config)) {
        LOG("Loaded config from persistent storage");
    } else {
        // Version 1 used an incompatible, much larger structure.
        // Keep the new compact defaults; notably, the alarm beep is now
        // audible instead of silently defaulting to zero.
        LOG(
            "Using current config defaults "
            "(stored version %ld, status %d)",
            (long)stored_version,
            status
        );
    }
}

static void local_persist_save(void) {
    StatusCode status = persist_write_data(PERSIST_KEY_CONFIG, &s_config, sizeof(s_config));
    ASSERT(status == sizeof(s_config));

    if (status == sizeof(s_config)) {
        status = persist_write_int(PERSIST_KEY_CONFIG_VERSION, PERSIST_CONFIG_VERSION);
        ASSERT(status == sizeof(int32_t));
    }
}


/******************************************************************************
 Private methods; Receive config from phone
******************************************************************************/

#define RECEIVE_CONFIG(message_key, convert) MACRO_START \
    const Tuple *tuple = dict_find(iter, MESSAGE_KEY_##message_key); \
    if (tuple) { \
        s_config.message_key = convert; \
    } else { \
        LOG("ERR: " #message_key); \
    } \
MACRO_END

#define RECEIVE_CONFIG_BOOL(message_key) \
    RECEIVE_CONFIG(message_key, (tuple->value->int32 == 1))

#define RECEIVE_CONFIG_COLOR(message_key) \
    RECEIVE_CONFIG(message_key, GColorFromHEX(tuple->value->int32))

#define RECEIVE_CONFIG_INT(message_key) \
    RECEIVE_CONFIG(message_key, tuple->value->int32)

// Clay enum workaround: interpret the first digit of the submitted value.
#define RECEIVE_CONFIG_ENUM(message_key) \
    RECEIVE_CONFIG(message_key, (tuple->value->int32 - '0'))

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
    Config saved_config = s_config;

#define CONFIG_STRUCT_RECEIVE(_conftype, _ctype, _message_key, _default) RECEIVE_CONFIG_##_conftype(_message_key);
    X_CONFIG_OPTIONS(CONFIG_STRUCT_RECEIVE)
#undef CONFIG_STRUCT_RECEIVE

    if (memcmp(&saved_config, &s_config, sizeof(saved_config)) != 0) {
        LOG("New app config received");
        local_persist_save();
        if (s_new_config_callback != NULL) {
            s_new_config_callback(&s_config);
        }
    }
}


/******************************************************************************
 Public methods
******************************************************************************/

// `callback` is an optional function to call whenever a new config is received from the phone;
// it should be used to mark affected layers dirty or otherwise live-update config changes
void config_init(NewConfigCallback callback) {
    s_new_config_callback = callback;
    local_persist_load();
    app_message_register_inbox_received(&inbox_received_handler);

    AppMessageResult result = app_message_open(256, 0);
    if (result != APP_MSG_OK) {
        LOG("app_message_open failed: 0x%x", result);
    }
}

void config_deinit(void) {
    app_message_deregister_callbacks();
}

const Config* config_get(void) {
    return &s_config;
}

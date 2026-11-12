#pragma once

#include "automation_rules.h"
#include "sharp_ac_util.h"
#include "logger_util.h"
#include "clock_util.h"
#include "blynk_util.h"
#include "thingsboard_util.h"
#include "voice_util.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "nvs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUTO_NVS_NAMESPACE "talos"
#define AUTO_NVS_KEY "auto_en"
#define AUTO_NVS_MODEL_KEY "auto_model"
#define AUTO_BLYNK_SYNC_S 60
#define AUTO_LAST_LEN 160
#define AUTO_STATE_LEN 32

static struct AutoState auto_state;
static SemaphoreHandle_t auto_mutex = NULL;
static char auto_last_action[AUTO_LAST_LEN] = "none";
static char auto_override_source[AUTO_LAST_LEN] = "";
static int64_t auto_last_blynk_sync = AUTO_NEVER;

static int64_t auto_now_s () {
    return esp_timer_get_time() / 1'000'000;
}

static bool automation_load_flag ( const char* key, uint8_t fallback ) {
    nvs_handle_t h;
    uint8_t value = fallback;

    if ( nvs_open( AUTO_NVS_NAMESPACE, NVS_READONLY, &h ) == ESP_OK ) {
        nvs_get_u8( h, key, &value );
        nvs_close( h );
    }

    return value != 0;
}

static void automation_save_flag ( const char* key, bool on ) {
    nvs_handle_t h;

    if ( nvs_open( AUTO_NVS_NAMESPACE, NVS_READWRITE, &h ) != ESP_OK ) {
        printf("Auto: could not save setting\n");
        return;
    }

    nvs_set_u8( h, key, on ? 1 : 0 );
    nvs_commit( h );
    nvs_close( h );
}

static void automation_set_enabled ( bool on ) {
    if ( !auto_mutex ) {
        return;
    }

    xSemaphoreTake( auto_mutex, portMAX_DELAY );
    bool changed = auto_state.enabled != on;
    auto_state.enabled = on;
    xSemaphoreGive( auto_mutex );

    if ( changed ) {
        automation_save_flag( AUTO_NVS_KEY, on );
    }

    printf("Auto: %s\n", on ? "enabled" : "disabled");
    blynk_send_int( BLYNK_DS_AUTOMATION, on ? 1 : 0 );
    thingsboard_send_automation( on );
}

static void automation_set_use_model ( bool use_model ) {
    if ( !auto_mutex ) {
        return;
    }

    xSemaphoreTake( auto_mutex, portMAX_DELAY );
    bool changed = auto_state.use_model != use_model;
    auto_state.use_model = use_model;
    xSemaphoreGive( auto_mutex );

    if ( changed ) {
        automation_save_flag( AUTO_NVS_MODEL_KEY, use_model );
    }

    printf("Auto: using the %s\n", use_model ? "model" : "rules");
    thingsboard_send_use_model( use_model );
}

static bool automation_uses_model () {
    if ( !auto_mutex ) {
        return false;
    }

    xSemaphoreTake( auto_mutex, portMAX_DELAY );
    bool use_model = auto_state.use_model;
    xSemaphoreGive( auto_mutex );

    return use_model;
}

static bool automation_is_enabled () {
    if ( !auto_mutex ) {
        return false;
    }

    xSemaphoreTake( auto_mutex, portMAX_DELAY );
    bool on = auto_state.enabled;
    xSemaphoreGive( auto_mutex );

    return on;
}

static void automation_on_manual ( const char* source ) {
    struct tm tm;
    char when[24];

    if ( clock_now( &tm ) ) {
        strftime( when, sizeof( when ), "%H:%M:%S", &tm );
    } else {
        snprintf( when, sizeof( when ), "uptime %llds", auto_now_s() );
    }

    xSemaphoreTake( auto_mutex, portMAX_DELAY );
    auto_note_manual( &auto_state, auto_now_s() );
    snprintf( auto_override_source, sizeof( auto_override_source ), "%s at %s", source, when );
    xSemaphoreGive( auto_mutex );

    printf("Auto: manual change (%s), paused for %d min\n", source, AUTO_OVERRIDE_S / 60);
}

static void automation_announce ( enum AutoAction action, int setpoint ) {
    switch ( action ) {
    case AUTO_OFF:
        voice_say( VOICE_AC_OFF );
        break;

    case AUTO_ON_COOL16:
    case AUTO_SETPOINT:
        if ( action == AUTO_ON_COOL16 && setpoint == AUTO_ON_SETPOINT ) {
            voice_say( VOICE_AC_ON_16 );
        } else if ( setpoint == 16 ) {
            voice_say( VOICE_SET_16 );
        } else if ( setpoint == 19 ) {
            voice_say( VOICE_SET_19 );
        } else if ( setpoint == 20 ) {
            voice_say( VOICE_SET_20 );
        } else if ( setpoint == 21 ) {
            voice_say( VOICE_SET_21 );
        }

        break;

    case AUTO_NONE:
        break;
    }
}

static void automation_apply ( enum AutoAction action, int new_setpoint, const char* who, const char* prefix ) {
    esp_err_t err = ESP_OK;
    char state[AUTO_STATE_LEN];

    sharp_ac_lock();

    switch ( action ) {
    case AUTO_OFF:
        err = sharp_ac_set_power( false );
        break;

    case AUTO_ON_COOL16:
        sharp_ac.mode = SHARP_AC_COOL;
        sharp_ac.temp = new_setpoint;
        sharp_ac.fan = SHARP_AC_FAN_MAX;
        sharp_ac.turbo = false;
        err = sharp_ac_set_power( true );
        break;

    case AUTO_SETPOINT:
        err = sharp_ac_set_temp( new_setpoint );
        break;

    case AUTO_NONE:
        break;
    }

    sharp_ac_state_string( state, sizeof( state ) );
    sharp_ac_unlock();

    if ( err != ESP_OK ) {
        printf("Auto: %s failed (%s)\n", who, esp_err_to_name( err ));
        return;
    }

    char ir[LOG_IR_LEN];
    snprintf( ir, sizeof( ir ), "%s:%s", prefix, state );
    logger_set_ir( ir );

    printf("Auto: %s -> %s\n", who, state);
    automation_announce( action, new_setpoint );

    struct tm tm;
    char when[24] = "uptime";

    if ( clock_now( &tm ) ) {
        strftime( when, sizeof( when ), "%Y-%m-%d %H:%M", &tm );
    }

    xSemaphoreTake( auto_mutex, portMAX_DELAY );
    snprintf( auto_last_action, sizeof( auto_last_action ), "%s -> %s at %s %llds", who, state, when, auto_now_s() );
    xSemaphoreGive( auto_mutex );
}

static void automation_on_row ( float temp, float hum, bool dht_valid, int mic_peak, const char* ir ) {
    struct AutoInput in = {
        .now_s = auto_now_s(),
        .temp = temp,
        .hum = hum,
        .dht_valid = dht_valid,
        .mic_peak = mic_peak,
        .hour = clock_hour(),
    };

    sharp_ac_lock();
    in.ac_on = sharp_ac.power;
    in.setpoint = sharp_ac.temp;
    sharp_ac_unlock();

    int new_setpoint = in.setpoint;

    xSemaphoreTake( auto_mutex, portMAX_DELAY );
    enum AutoAction action = auto_decide( &auto_state, &in, &new_setpoint );
    bool enabled = auto_state.enabled;
    xSemaphoreGive( auto_mutex );

    if ( action != AUTO_NONE ) {
        automation_apply( action, new_setpoint, auto_action_rule( action ), "auto" );
    }

    if ( in.now_s - auto_last_blynk_sync >= AUTO_BLYNK_SYNC_S ) {
        auto_last_blynk_sync = in.now_s;
        blynk_send_int( BLYNK_DS_AUTOMATION, enabled ? 1 : 0 );
    }
}

static void automation_on_prediction ( const char* setting, float confidence ) {
    if ( !auto_mutex ) {
        return;
    }

    sharp_ac_lock();
    bool ac_on = sharp_ac.power;
    int setpoint = sharp_ac.temp;
    sharp_ac_unlock();

    int new_setpoint = setpoint;

    xSemaphoreTake( auto_mutex, portMAX_DELAY );
    enum AutoAction action = auto_decide_model( &auto_state, auto_now_s(), setting, confidence, ac_on, setpoint, &new_setpoint );
    xSemaphoreGive( auto_mutex );

    if ( action != AUTO_NONE ) {
        char who[AUTO_STATE_LEN];
        snprintf( who, sizeof( who ), "model %s (%.0f%%)", setting, confidence * 100 );
        automation_apply( action, new_setpoint, who, "model" );
    }
}

static const char* automation_presence_name ( enum AutoPresence p ) {
    switch ( p ) {
    case AUTO_PRESENCE_OCCUPIED: return "occupied";
    case AUTO_PRESENCE_EMPTY: return "empty";
    case AUTO_PRESENCE_UNKNOWN: break;
    }

    return "unknown (window filling)";
}

static void automation_print_status () {
    int64_t now = auto_now_s();

    xSemaphoreTake( auto_mutex, portMAX_DELAY );
    struct AutoState s = auto_state;
    char last[AUTO_LAST_LEN];
    char source[AUTO_LAST_LEN];
    strlcpy( last, auto_last_action, sizeof( last ) );
    strlcpy( source, auto_override_source, sizeof( source ) );
    xSemaphoreGive( auto_mutex );

    printf("Auto: %s, using the %s\n", s.enabled ? "enabled" : "disabled", s.use_model ? "model" : "rules");

    if ( s.model_last_at == AUTO_NEVER ) {
        printf("  model: no prediction received\n");
    } else {
        printf(
            "  model: %s, confidence %.0f%%, %d/%d in a row, %lld s ago\n",
            s.model_setting[0] ? s.model_setting : "unsure",
            s.model_confidence * 100,
            s.model_streak,
            AUTO_MODEL_STREAK,
            now - s.model_last_at
        );
    }

    printf("  mic 5-min median: %d (window %d/%d, threshold %d)\n", s.median, s.mic_count, AUTO_MIC_WINDOW, AUTO_OCCUPIED_MIC);
    printf("  presence: %s", automation_presence_name( s.presence ));

    if ( s.presence != AUTO_PRESENCE_UNKNOWN ) {
        printf(" for %lld min", ( now - s.presence_since ) / 60);
    }

    printf("\n");

    if ( now < s.override_until ) {
        printf("  manual override: %lld min left (%s)\n", ( s.override_until - now + 59 ) / 60, source);
    } else {
        printf("  manual override: none\n");
    }

    struct tm tm;

    if ( clock_now( &tm ) ) {
        char when[24];
        strftime( when, sizeof( when ), "%Y-%m-%d %H:%M:%S", &tm );
        printf("  clock: %s\n", when);
    } else {
        printf("  clock: not synced (R3 inactive)\n");
    }

    printf("  last action: %s\n", last);
    sharp_ac_print_state();
}

static bool automation_console ( const char* cmd, const char* arg ) {
    if ( strcmp( cmd, "auto" ) != 0 ) {
        return false;
    }

    if ( arg && strcmp( arg, "say" ) == 0 ) {
        const char* what = strtok( NULL, " " );

        if ( !what ) {
            printf("Usage: auto say on|off|16|19|20|21\n");
        } else if ( strcmp( what, "on" ) == 0 ) {
            automation_announce( AUTO_ON_COOL16, AUTO_ON_SETPOINT );
        } else if ( strcmp( what, "off" ) == 0 ) {
            automation_announce( AUTO_OFF, 0 );
        } else {
            automation_announce( AUTO_SETPOINT, atoi( what ) );
        }
    } else if ( arg && strcmp( arg, "use" ) == 0 ) {
        const char* what = strtok( NULL, " " );

        if ( what && strcmp( what, "model" ) == 0 ) {
            automation_set_use_model( true );
        } else if ( what && strcmp( what, "rules" ) == 0 ) {
            automation_set_use_model( false );
        } else {
            printf("Usage: auto use rules|model\n");
        }
    } else if ( arg && strcmp( arg, "on" ) == 0 ) {
        automation_set_enabled( true );
    } else if ( arg && strcmp( arg, "off" ) == 0 ) {
        automation_set_enabled( false );
    } else {
        automation_print_status();
    }

    return true;
}

static void automation_cfg () {
    auto_mutex = xSemaphoreCreateMutex();
    auto_init( &auto_state, automation_load_flag( AUTO_NVS_KEY, 1 ), automation_load_flag( AUTO_NVS_MODEL_KEY, 0 ) );

    sharp_ac_manual_cb = automation_on_manual;
    sharp_ac_extra_cmd_cb = automation_console;
    logger_add_row_cb( automation_on_row );

    printf("Auto: %s, using the %s\n", auto_state.enabled ? "enabled" : "disabled", auto_state.use_model ? "model" : "rules");
}

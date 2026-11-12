#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define AUTO_MIC_WINDOW 150
#define AUTO_OCCUPIED_MIC 30000
#define AUTO_EMPTY_OFF_S 900
#define AUTO_OCCUPIED_ON_S 120
#define AUTO_ARRIVE_MIN_TEMP 26.0f
#define AUTO_ON_SETPOINT 16
#define AUTO_CLIMB_JUMP_SETPOINT 19
#define AUTO_CLIMB_MAX_SETPOINT 21
#define AUTO_CLIMB_MAX_HUM 66.0f
#define AUTO_CLIMB_MARGIN 0.2f
#define AUTO_CLIMB_GAP_S 1800
#define AUTO_CLIMB_FROM_HOUR 23
#define AUTO_CLIMB_UNTIL_HOUR 3
#define AUTO_OVERRIDE_S 3600
#define AUTO_MIN_GAP_S 600
#define AUTO_MODEL_MIN_CONFIDENCE 0.8f
#define AUTO_MODEL_STREAK 3
#define AUTO_MODEL_STALE_S 180
#define AUTO_MODEL_MIN_SETPOINT 16
#define AUTO_MODEL_MAX_SETPOINT 30
#define AUTO_MODEL_SETTING_LEN 8
#define AUTO_NEVER ( -( ( int64_t )1 << 40 ) )

enum AutoAction {
    AUTO_NONE,
    AUTO_OFF,
    AUTO_ON_COOL16,
    AUTO_SETPOINT,
};

enum AutoPresence {
    AUTO_PRESENCE_UNKNOWN = -1,
    AUTO_PRESENCE_EMPTY = 0,
    AUTO_PRESENCE_OCCUPIED = 1,
};

struct AutoState {
    int mic[AUTO_MIC_WINDOW];
    int mic_next;
    int mic_count;
    int median;
    enum AutoPresence presence;
    int64_t presence_since;
    int64_t last_auto_cmd;
    int64_t last_setpoint_change;
    int64_t override_until;
    bool enabled;
    bool use_model;
    char model_setting[AUTO_MODEL_SETTING_LEN];
    int model_streak;
    float model_confidence;
    int64_t model_last_at;
};

struct AutoInput {
    int64_t now_s;
    float temp;
    float hum;
    bool dht_valid;
    int mic_peak;
    int hour;
    bool ac_on;
    int setpoint;
};

static void auto_init ( struct AutoState* s, bool enabled, bool use_model ) {
    memset( s, 0, sizeof( *s ) );
    s->presence = AUTO_PRESENCE_UNKNOWN;
    s->presence_since = 0;
    s->last_auto_cmd = AUTO_NEVER;
    s->last_setpoint_change = AUTO_NEVER;
    s->override_until = AUTO_NEVER;
    s->enabled = enabled;
    s->use_model = use_model;
    s->model_last_at = AUTO_NEVER;
}

static const char* auto_action_rule ( enum AutoAction a ) {
    switch ( a ) {
    case AUTO_OFF: return "R1 leave";
    case AUTO_ON_COOL16: return "R2 arrive";
    case AUTO_SETPOINT: return "R3 climb";
    case AUTO_NONE: break;
    }

    return "none";
}

static float auto_settled_temp ( int setpoint ) {
    return 20.9f + 0.32f * ( setpoint - 16 );
}

static int auto_median ( const struct AutoState* s ) {
    int v[AUTO_MIC_WINDOW];
    int n = s->mic_count;

    memcpy( v, s->mic, n * sizeof( int ) );

    for ( int i = 1; i < n; ++i ) {
        int x = v[i];
        int j = i - 1;

        while ( j >= 0 && v[j] > x ) {
            v[j + 1] = v[j];
            --j;
        }

        v[j + 1] = x;
    }

    return v[n / 2];
}

static void auto_update_presence ( struct AutoState* s, int64_t now_s, int mic_peak ) {
    s->mic[s->mic_next] = mic_peak;
    s->mic_next = ( s->mic_next + 1 ) % AUTO_MIC_WINDOW;

    if ( s->mic_count < AUTO_MIC_WINDOW ) {
        ++s->mic_count;
    }

    if ( s->mic_count < AUTO_MIC_WINDOW ) {
        return;
    }

    s->median = auto_median( s );

    enum AutoPresence p = s->median >= AUTO_OCCUPIED_MIC
        ? AUTO_PRESENCE_OCCUPIED
        : AUTO_PRESENCE_EMPTY;

    if ( p != s->presence ) {
        s->presence = p;
        s->presence_since = now_s;
    }
}

static void auto_note_manual ( struct AutoState* s, int64_t now_s ) {
    s->override_until = now_s + AUTO_OVERRIDE_S;
    s->last_setpoint_change = now_s;
}

static bool auto_climb_hour ( int hour ) {
    if ( hour < 0 ) {
        return false;
    }

    return hour >= AUTO_CLIMB_FROM_HOUR || hour < AUTO_CLIMB_UNTIL_HOUR;
}

static void auto_note_command ( struct AutoState* s, int64_t now_s, enum AutoAction action ) {
    s->last_auto_cmd = now_s;

    if ( action != AUTO_OFF ) {
        s->last_setpoint_change = now_s;
    }
}

static enum AutoAction auto_decide ( struct AutoState* s, const struct AutoInput* in, int* new_setpoint ) {
    auto_update_presence( s, in->now_s, in->mic_peak );

    if (
        !s->enabled
        || s->use_model
        || in->now_s < s->override_until
        || in->now_s - s->last_auto_cmd < AUTO_MIN_GAP_S
        || s->presence == AUTO_PRESENCE_UNKNOWN
    ) {
        return AUTO_NONE;
    }

    int64_t held = in->now_s - s->presence_since;
    enum AutoAction action = AUTO_NONE;

    if ( in->ac_on && s->presence == AUTO_PRESENCE_EMPTY && held >= AUTO_EMPTY_OFF_S ) {
        action = AUTO_OFF;
    } else if (
        !in->ac_on
        && s->presence == AUTO_PRESENCE_OCCUPIED
        && held >= AUTO_OCCUPIED_ON_S
        && in->dht_valid
        && in->temp >= AUTO_ARRIVE_MIN_TEMP
    ) {
        action = AUTO_ON_COOL16;
        *new_setpoint = AUTO_ON_SETPOINT;
    } else if (
        in->ac_on
        && auto_climb_hour( in->hour )
        && in->setpoint < AUTO_CLIMB_MAX_SETPOINT
        && in->dht_valid
        && in->temp <= auto_settled_temp( in->setpoint ) + AUTO_CLIMB_MARGIN
        && in->hum <= AUTO_CLIMB_MAX_HUM
        && in->now_s - s->last_setpoint_change >= AUTO_CLIMB_GAP_S
    ) {
        action = AUTO_SETPOINT;
        *new_setpoint = in->setpoint < AUTO_CLIMB_JUMP_SETPOINT
            ? AUTO_CLIMB_JUMP_SETPOINT
            : in->setpoint + 1;
    }

    if ( action != AUTO_NONE ) {
        auto_note_command( s, in->now_s, action );
    }

    return action;
}

static enum AutoAction auto_decide_model (
    struct AutoState* s,
    int64_t now_s,
    const char* setting,
    float confidence,
    bool ac_on,
    int setpoint,
    int* new_setpoint
) {
    bool sure = confidence >= AUTO_MODEL_MIN_CONFIDENCE;
    bool fresh = now_s - s->model_last_at <= AUTO_MODEL_STALE_S;

    if ( sure && fresh && strcmp( setting, s->model_setting ) == 0 ) {
        ++s->model_streak;
    } else {
        strlcpy( s->model_setting, sure ? setting : "", sizeof( s->model_setting ) );
        s->model_streak = sure ? 1 : 0;
    }

    s->model_confidence = confidence;
    s->model_last_at = now_s;

    if (
        !s->enabled
        || !s->use_model
        || now_s < s->override_until
        || now_s - s->last_auto_cmd < AUTO_MIN_GAP_S
        || s->model_streak < AUTO_MODEL_STREAK
    ) {
        return AUTO_NONE;
    }

    enum AutoAction action = AUTO_NONE;

    if ( strcmp( setting, "off" ) == 0 ) {
        if ( ac_on ) {
            action = AUTO_OFF;
        }
    } else {
        int target = atoi( setting );

        if ( target < AUTO_MODEL_MIN_SETPOINT || target > AUTO_MODEL_MAX_SETPOINT ) {
            return AUTO_NONE;
        }

        if ( !ac_on ) {
            action = AUTO_ON_COOL16;
            *new_setpoint = target;
        } else if ( setpoint != target ) {
            action = AUTO_SETPOINT;
            *new_setpoint = target;
        }
    }

    if ( action != AUTO_NONE ) {
        auto_note_command( s, now_s, action );
    }

    return action;
}

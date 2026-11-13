#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// mic samples for the 5 minute median (150 rows x 2 seconds), occupied if the median is at least this
#define AUTO_MIC_WINDOW 150
#define AUTO_OCCUPIED_MIC 30000
// R1, AC goes off after the room has been empty this long
#define AUTO_EMPTY_OFF_S 900
// R2, AC goes on after someone's been there this long and it's at least this hot, always at 16
#define AUTO_OCCUPIED_ON_S 120
#define AUTO_ARRIVE_MIN_TEMP 26.0f
#define AUTO_ON_SETPOINT 16
// R3, at night the setpoint jumps to 19 and then goes up by 1 until 21, but only if the room already cooled down to the settled temp and it's not too humid
#define AUTO_CLIMB_JUMP_SETPOINT 19
#define AUTO_CLIMB_MAX_SETPOINT 21
#define AUTO_CLIMB_MAX_HUM 66.0f
#define AUTO_CLIMB_MARGIN 0.2f
// at least 30 min between setpoint changes, and only between 23:00 and 03:00
#define AUTO_CLIMB_GAP_S 1800
#define AUTO_CLIMB_FROM_HOUR 23
#define AUTO_CLIMB_UNTIL_HOUR 3
// using the real remote pauses the automation for an hour, and there's always 10 min between automatic commands
#define AUTO_OVERRIDE_S 3600
#define AUTO_MIN_GAP_S 600
// the model needs at least 80% confidence on the same setting 3 minutes in a row, older than 3 minutes doesn't count
#define AUTO_MODEL_MIN_CONFIDENCE 0.8f
#define AUTO_MODEL_STREAK 3
#define AUTO_MODEL_STALE_S 180
// setpoints the model is allowed to ask for
#define AUTO_MODEL_MIN_SETPOINT 16
#define AUTO_MODEL_MAX_SETPOINT 30
#define AUTO_MODEL_SETTING_LEN 8
// a time far enough in the past so the gaps always count as passed
#define AUTO_NEVER ( -( ( int64_t )1 << 40 ) )

// what the automation wants to do with the AC
enum AutoAction {
    AUTO_NONE,
    AUTO_OFF,
    AUTO_ON_COOL16,
    AUTO_SETPOINT,
};

// unknown until the mic window is full
enum AutoPresence {
    AUTO_PRESENCE_UNKNOWN = -1,
    AUTO_PRESENCE_EMPTY = 0,
    AUTO_PRESENCE_OCCUPIED = 1,
};

// everything the automation remembers between rows
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

// one logger row, plus the AC state and the hour
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

// starts with nothing in the window and every timer already expired
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

// the rule name for the logs
static const char* auto_action_rule ( enum AutoAction a ) {
    switch ( a ) {
    case AUTO_OFF: return "R1 leave";
    case AUTO_ON_COOL16: return "R2 arrive";
    case AUTO_SETPOINT: return "R3 climb";
    case AUTO_NONE: break;
    }

    return "none";
}

// room temp the AC usually settles at for a setpoint, from the Assignment 2 data
static float auto_settled_temp ( int setpoint ) {
    return 20.9f + 0.32f * ( setpoint - 16 );
}

// copies the window and insertion sorts it to get the median, it's only 150 numbers
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

// adds the mic reading to the window and updates the presence once the window is full
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

    // only reset the timer when the presence actually changes
    if ( p != s->presence ) {
        s->presence = p;
        s->presence_since = now_s;
    }
}

// someone used the real remote, so pause and count it as a setpoint change
static void auto_note_manual ( struct AutoState* s, int64_t now_s ) {
    s->override_until = now_s + AUTO_OVERRIDE_S;
    s->last_setpoint_change = now_s;
}

// true between 23:00 and 03:00, false if the clock isn't synced yet
static bool auto_climb_hour ( int hour ) {
    if ( hour < 0 ) {
        return false;
    }

    return hour >= AUTO_CLIMB_FROM_HOUR || hour < AUTO_CLIMB_UNTIL_HOUR;
}

// remembers when the last command went out, turning off doesn't count as a setpoint change
static void auto_note_command ( struct AutoState* s, int64_t now_s, enum AutoAction action ) {
    s->last_auto_cmd = now_s;

    if ( action != AUTO_OFF ) {
        s->last_setpoint_change = now_s;
    }
}

// the rules engine (R1 to R3), called on every logger row, returns what to do and the new setpoint if any
static enum AutoAction auto_decide ( struct AutoState* s, const struct AutoInput* in, int* new_setpoint ) {
    auto_update_presence( s, in->now_s, in->mic_peak );

    // nothing to do if it's off, the model is in charge, it's paused, too soon, or we don't know the presence yet
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

    // R1: AC on and empty for 15 min -> off
    if ( in->ac_on && s->presence == AUTO_PRESENCE_EMPTY && held >= AUTO_EMPTY_OFF_S ) {
        action = AUTO_OFF;
    // R2: AC off, occupied for 2 min and hot -> on at 16
    } else if (
        !in->ac_on
        && s->presence == AUTO_PRESENCE_OCCUPIED
        && held >= AUTO_OCCUPIED_ON_S
        && in->dht_valid
        && in->temp >= AUTO_ARRIVE_MIN_TEMP
    ) {
        action = AUTO_ON_COOL16;
        *new_setpoint = AUTO_ON_SETPOINT;
    // R3: night, AC on, room cooled down, humidity ok and 30 min since the last change -> raise the setpoint
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

// the model engine, called once per prediction, returns what to do and the new setpoint if any
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

    // count how many predictions in a row said the same thing with enough confidence
    if ( sure && fresh && strcmp( setting, s->model_setting ) == 0 ) {
        ++s->model_streak;
    } else {
        strlcpy( s->model_setting, sure ? setting : "", sizeof( s->model_setting ) );
        s->model_streak = sure ? 1 : 0;
    }

    s->model_confidence = confidence;
    s->model_last_at = now_s;

    // nothing to do if it's off, the rules are in charge, it's paused, too soon, or the streak isn't long enough yet
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

    // "off" turns it off, anything else is a setpoint like "19C", turn on with it or change to it
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

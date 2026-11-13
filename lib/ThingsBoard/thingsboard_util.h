#pragma once

#include "thingsboard_config.h"
#include "logger_util.h"
#include "sharp_ac_util.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// callback types for reading and setting the switches
typedef bool ( *tb_get_auto_cb_t )( void );
typedef void ( *tb_set_auto_cb_t )( bool on );

// the MQTT client, whether we're connected, and who to ask about the automation and model switches
static esp_mqtt_client_handle_t tb_client = NULL;
static volatile bool tb_ok = false;
static tb_get_auto_cb_t tb_get_auto_cb = NULL;
static tb_set_auto_cb_t tb_set_auto_cb = NULL;
static tb_get_auto_cb_t tb_get_model_cb = NULL;
static tb_set_auto_cb_t tb_set_model_cb = NULL;

// the sensor values collected since the last publish
static int tb_rows = 0;
static int tb_mic_max = 0;
static float tb_temp = 0.0f;
static float tb_hum = 0.0f;
static bool tb_dht_valid = false;
static char tb_ir[LOG_IR_LEN] = "";

// the last AC state we sent, so we only send it again when it changes or the heartbeat is due
static volatile bool tb_ac_dirty = true;
static struct SharpAcState tb_ac_sent;
static int tb_actuator_rows = 0;
// OLED and speaker state since the last publish
static volatile bool tb_oled_bars = false;
static volatile bool tb_speaker_playing = false;
static volatile bool tb_speaker_played = false;
static bool tb_speaker_sent = false;

// sends something to ThingsBoard, does nothing if we're not connected
static void tb_publish ( const char* topic, const char* payload ) {
    if ( !tb_ok ) {
        return;
    }

    esp_mqtt_client_publish( tb_client, topic, payload, 0, 0, 0 );
}

// the automation switch as a client attribute
static void thingsboard_send_automation ( bool on ) {
    tb_publish( TB_ATTRIBUTES_TOPIC, on ? "{\"automation\":true}" : "{\"automation\":false}" );
}

// same thing for the rules/model switch
static void thingsboard_send_use_model ( bool use_model ) {
    tb_publish( TB_ATTRIBUTES_TOPIC, use_model ? "{\"use_model\":true}" : "{\"use_model\":false}" );
}

// true if the RPC params is true or 1
static bool tb_params_true ( const char* payload ) {
    const char* p = strstr( payload, "\"params\"" );

    if ( !p ) {
        return false;
    }

    p += strlen( "\"params\"" );

    while ( *p == ' ' || *p == ':' || *p == '"' ) {
        ++p;
    }

    return strncmp( p, "true", 4 ) == 0 || *p == '1';
}

// handles the switch RPCs, set changes it, get just asks, and both answer with the current value
static void tb_handle_rpc ( const char* id, const char* payload ) {
    bool on;

    if ( strstr( payload, TB_RPC_SET_USE_MODEL ) ) {
        on = tb_params_true( payload );

        if ( tb_set_model_cb ) {
            tb_set_model_cb( on );
        }
    } else if ( strstr( payload, TB_RPC_GET_USE_MODEL ) ) {
        on = tb_get_model_cb ? tb_get_model_cb() : false;
    } else if ( strstr( payload, TB_RPC_SET_AUTOMATION ) ) {
        on = tb_params_true( payload );

        if ( tb_set_auto_cb ) {
            tb_set_auto_cb( on );
        }
    } else if ( strstr( payload, TB_RPC_GET_AUTOMATION ) ) {
        on = tb_get_auto_cb ? tb_get_auto_cb() : false;
    } else {
        ESP_LOGW(TB_TAG, "Unknown RPC: %s", payload);
        return;
    }

    char topic[sizeof( TB_RPC_RESPONSE_PREFIX ) + TB_TOPIC_LEN];
    snprintf( topic, sizeof( topic ), TB_RPC_RESPONSE_PREFIX "%s", id );
    tb_publish( topic, on ? "true" : "false" );
}

// handles everything that happens on the MQTT side
static void tb_event_handler ( void* arg, esp_event_base_t base, int32_t id, void* data ) {
    esp_mqtt_event_handle_t event = data;

    switch ( ( esp_mqtt_event_id_t ) id ) {
    // connected, listen for RPCs and resend the AC state and both switches
    case MQTT_EVENT_CONNECTED:
        tb_ok = true;
        printf("ThingsBoard: connected\n");
        esp_mqtt_client_subscribe( tb_client, TB_RPC_REQUEST_SUB, 0 );
        tb_ac_dirty = true;

        if ( tb_get_auto_cb ) {
            thingsboard_send_automation( tb_get_auto_cb() );
        }

        if ( tb_get_model_cb ) {
            thingsboard_send_use_model( tb_get_model_cb() );
        }

        break;

    case MQTT_EVENT_DISCONNECTED:
        tb_ok = false;
        printf("ThingsBoard: disconnected\n");
        break;

    // got an RPC, topic and data aren't null terminated so copy them out first
    case MQTT_EVENT_DATA: {
        char topic[TB_TOPIC_LEN];
        char payload[TB_PAYLOAD_LEN];
        size_t prefix_len = strlen( TB_RPC_REQUEST_PREFIX );

        int t_len = event->topic_len < ( int )sizeof( topic ) - 1 ? event->topic_len : ( int )sizeof( topic ) - 1;
        int p_len = event->data_len < ( int )sizeof( payload ) - 1 ? event->data_len : ( int )sizeof( payload ) - 1;

        memcpy( topic, event->topic, t_len );
        topic[t_len] = '\0';
        memcpy( payload, event->data, p_len );
        payload[p_len] = '\0';

        if ( strncmp( topic, TB_RPC_REQUEST_PREFIX, prefix_len ) == 0 ) {
            tb_handle_rpc( topic + prefix_len, payload );
        }

        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TB_TAG, "MQTT error");
        break;

    default:
        break;
    }
}

// starts the MQTT client once we get an IP
static void tb_on_ip ( void* arg, esp_event_base_t base, int32_t id, void* data ) {
    if ( tb_client ) {
        return;
    }

    tb_client = esp_mqtt_client_init( &tb_mqtt_cfg );
    esp_mqtt_client_register_event( tb_client, ESP_EVENT_ANY_ID, tb_event_handler, NULL );
    esp_mqtt_client_start( tb_client );
}

// the OLED calls this, remembers if the bars showed up at all since the last publish
static void thingsboard_note_oled ( bool bars ) {
    if ( bars ) {
        tb_oled_bars = true;
    }
}

// the speaker calls this, remembers if it played at all since the last publish
static void thingsboard_note_speaker ( bool playing ) {
    tb_speaker_playing = playing;

    if ( playing ) {
        tb_speaker_played = true;
    }
}

// sends the AC and speaker state if something changed or 5 minutes went by
static void tb_send_actuators () {
    sharp_ac_lock();
    struct SharpAcState ac = sharp_ac;
    sharp_ac_unlock();

    bool speaker = tb_speaker_playing || tb_speaker_played;
    tb_speaker_played = false;

    bool changed = ac.power != tb_ac_sent.power
        || ac.mode != tb_ac_sent.mode
        || ac.temp != tb_ac_sent.temp
        || ac.fan != tb_ac_sent.fan
        || speaker != tb_speaker_sent;

    bool heartbeat = ++tb_actuator_rows >= TB_ACTUATOR_HEARTBEAT_ROWS;

    if ( !tb_ok || ( !changed && !heartbeat && !tb_ac_dirty ) ) {
        return;
    }

    char payload[TB_PAYLOAD_LEN];
    snprintf(
        payload, sizeof( payload ),
        "{\"ac_power\":%s,\"ac_mode\":\"%s\",\"ac_setpoint\":%d,\"ac_fan\":\"%s\",\"speaker\":\"%s\"}",
        ac.power ? "true" : "false",
        sharp_ac_mode_name( ac.mode ),
        ac.temp,
        sharp_ac_fan_name( ac.fan ),
        speaker ? "playing" : "idle"
    );

    tb_publish( TB_TELEMETRY_TOPIC, payload );
    tb_ac_sent = ac;
    tb_speaker_sent = speaker;
    tb_ac_dirty = false;
    tb_actuator_rows = 0;
}

// called on every logger row, collects the values and publishes every 8 rows
static void thingsboard_on_row ( float temp, float hum, bool dht_valid, int mic_peak, const char* ir ) {
    tb_send_actuators();

    if ( dht_valid ) {
        tb_temp = temp;
        tb_hum = hum;
        tb_dht_valid = true;
    }

    if ( mic_peak > tb_mic_max ) {
        tb_mic_max = mic_peak;
    }

    if ( strcmp( ir, "none" ) != 0 ) {
        strlcpy( tb_ir, ir, sizeof( tb_ir ) );
    }

    if ( ++tb_rows < TB_ROWS_PER_PUBLISH ) {
        return;
    }

    // loudest mic in the last 16 seconds, the latest temp and humidity, and the last IR command if there was one
    char payload[TB_PAYLOAD_LEN];
    int n = snprintf( payload, sizeof( payload ), "{\"mic_peak\":%d", tb_mic_max );

    if ( tb_dht_valid ) {
        n += snprintf( payload + n, sizeof( payload ) - n, ",\"temperature\":%.1f,\"humidity\":%.1f", tb_temp, tb_hum );
    }

    if ( tb_ir[0] ) {
        n += snprintf( payload + n, sizeof( payload ) - n, ",\"ir_cmd\":\"%s\"", tb_ir );
    }

    n += snprintf( payload + n, sizeof( payload ) - n, ",\"oled_mode\":\"%s\"", tb_oled_bars ? "bars" : "eyes" );
    snprintf( payload + n, sizeof( payload ) - n, "}" );

    tb_publish( TB_TELEMETRY_TOPIC, payload );

    tb_oled_bars = false;
    tb_rows = 0;
    tb_mic_max = 0;
    tb_ir[0] = '\0';
}

// forwards a prediction from the model service, the forecast only if there is one and the recommendation only if the model could make one
static void thingsboard_send_prediction ( const char* setting, float confidence, float temp_in_10m, int anomalies, int streak ) {
    char payload[TB_PAYLOAD_LEN];
    int n = snprintf( payload, sizeof( payload ), "{\"pred_anomalies\":%d,\"auto_streak\":%d", anomalies, streak );

    if ( !isnan( temp_in_10m ) ) {
        n += snprintf( payload + n, sizeof( payload ) - n, ",\"pred_temp_10m\":%.2f", temp_in_10m );
    }

    if ( setting[0] ) {
        n += snprintf(
            payload + n, sizeof( payload ) - n,
            ",\"pred_ac\":\"%s\",\"pred_ac_setpoint\":%d,\"pred_confidence\":%.2f",
            setting,
            // setpoint as a number so it can go on a chart, off is 0
            strcmp( setting, "off" ) == 0 ? 0 : atoi( setting ),
            confidence
        );
    }

    snprintf( payload + n, sizeof( payload ) - n, "}" );
    tb_publish( TB_TELEMETRY_TOPIC, payload );
}

// every command the automation sends, so the dashboard has a history of them
static void thingsboard_send_auto_cmd ( const char* cmd ) {
    char payload[TB_PAYLOAD_LEN];
    snprintf( payload, sizeof( payload ), "{\"auto_cmd\":\"%s\"}", cmd );
    tb_publish( TB_TELEMETRY_TOPIC, payload );
}

// tells us who to ask about the rules/model switch
static void thingsboard_cfg_model ( tb_get_auto_cb_t get_cb, tb_set_auto_cb_t set_cb ) {
    tb_get_model_cb = get_cb;
    tb_set_model_cb = set_cb;
}

// tells us who to ask about the automation switch, connects once we get an IP and hooks into the logger
static void thingsboard_cfg ( tb_get_auto_cb_t get_cb, tb_set_auto_cb_t set_cb ) {
    tb_get_auto_cb = get_cb;
    tb_set_auto_cb = set_cb;

    ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, tb_on_ip, NULL ) );

    logger_add_row_cb( thingsboard_on_row );
}

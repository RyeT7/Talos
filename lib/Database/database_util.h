#pragma once

#include "database_config.h"
#include "logger_util.h"
#include "clock_util.h"
#include "sharp_ac_util.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "mqtt_client.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

// one logger row plus the AC state at that moment, waiting to be uploaded
struct DbRow {
    int64_t ts_ms;
    int64_t uptime_ms;
    float temp;
    float hum;
    bool dht_valid;
    int mic_peak;
    char ir[LOG_IR_LEN];
    bool ac_power;
    enum SharpAcMode ac_mode;
    int ac_setpoint;
    enum SharpAcFan ac_fan;
};

// ring buffer of rows waiting for the broker, oldest at db_head
static struct DbRow db_rows[DB_BUFFER_ROWS];
static int db_head = 0;
static int db_count = 0;
static int db_dropped = 0;
static portMUX_TYPE db_mux = portMUX_INITIALIZER_UNLOCKED;

// the MQTT client, whether we're connected, the uploader task, and the last prediction we got
static esp_mqtt_client_handle_t db_client = NULL;
static volatile bool db_online = false;
static TaskHandle_t db_task_handle = NULL;
static char db_prediction[DB_PREDICTION_LEN] = "";

// callback type for when the model service sends a prediction
typedef void ( *db_prediction_cb_t )( const char* setting, float confidence, float temp_in_10m, int anomalies );

static db_prediction_cb_t db_prediction_cb = NULL;

// tells us who to call when a prediction comes in
static void database_set_prediction_cb ( db_prediction_cb_t cb ) {
    db_prediction_cb = cb;
}

// tiny JSON reader, finds the key and copies out the string after it, good enough for our own flat payloads
static bool db_json_string ( const char* json, const char* key, char* out, size_t len ) {
    const char* p = strstr( json, key );

    if ( !p ) {
        return false;
    }

    p += strlen( key );

    if ( *p != '"' ) {
        return false;
    }

    ++p;
    size_t n = 0;

    while ( p[n] && p[n] != '"' && n + 1 < len ) {
        out[n] = p[n];
        ++n;
    }

    out[n] = 0;

    return p[n] == '"';
}

// same thing but for numbers, false if the key isn't there or the value is null
static bool db_json_number ( const char* json, const char* key, float* out ) {
    const char* p = strstr( json, key );

    if ( !p ) {
        return false;
    }

    char* end;
    *out = strtof( p + strlen( key ), &end );

    return end != p + strlen( key );
}

// pulls the fields out of a prediction and hands them to the callback
static void db_handle_prediction ( const char* json ) {
    if ( !db_prediction_cb ) {
        return;
    }

    char setting[DB_SETTING_LEN];
    float confidence;
    float temp_in_10m;
    float anomalies;

    // null recommendation means the model couldn't predict yet, send an empty setting so only the forecast and anomalies get forwarded
    if (
        !db_json_string( json, "\"ac_recommendation\":", setting, sizeof( setting ) )
        || !db_json_number( json, "\"confidence\":", &confidence )
    ) {
        setting[0] = '\0';
        confidence = NAN;
    }

    if ( !db_json_number( json, "\"temp_in_10m\":", &temp_in_10m ) ) {
        temp_in_10m = NAN;
    }

    if ( !db_json_number( json, "\"anomalies\":", &anomalies ) ) {
        anomalies = 0;
    }

    db_prediction_cb( setting, confidence, temp_in_10m, ( int )anomalies );
}

// real time in ms, or 0 if the clock isn't synced yet (sent as null so the server uses its own time)
static int64_t db_epoch_ms () {
    struct tm tm;

    if ( !clock_now( &tm ) ) {
        return 0;
    }

    struct timeval tv;
    gettimeofday( &tv, NULL );

    return ( int64_t )tv.tv_sec * 1'000 + tv.tv_usec / 1'000;
}

// called on every logger row, copies it with the AC state into the buffer and wakes the uploader
static void database_on_row ( float temp, float hum, bool dht_valid, int mic_peak, const char* ir ) {
    struct DbRow row = {
        .ts_ms = db_epoch_ms(),
        .uptime_ms = esp_timer_get_time() / 1'000,
        .temp = temp,
        .hum = hum,
        .dht_valid = dht_valid,
        .mic_peak = mic_peak,
    };

    strlcpy( row.ir, ir, sizeof( row.ir ) );

    sharp_ac_lock();
    row.ac_power = sharp_ac.power;
    row.ac_mode = sharp_ac.mode;
    row.ac_setpoint = sharp_ac.temp;
    row.ac_fan = sharp_ac.fan;
    sharp_ac_unlock();

    portENTER_CRITICAL( &db_mux );

    // buffer's full (broker down for 10 min), drop the oldest row
    if ( db_count == DB_BUFFER_ROWS ) {
        db_head = ( db_head + 1 ) % DB_BUFFER_ROWS;
        --db_count;
        ++db_dropped;
    }

    db_rows[( db_head + db_count ) % DB_BUFFER_ROWS] = row;
    ++db_count;

    portEXIT_CRITICAL( &db_mux );

    if ( db_task_handle ) {
        xTaskNotifyGive( db_task_handle );
    }
}

// turns a row into the JSON the Go server expects, nulls for anything we don't have
static int db_row_json ( char* out, size_t len, const struct DbRow* r ) {
    int n = 0;

    if ( r->ts_ms > 0 ) {
        n += snprintf( out + n, len - n, "{\"ts\":%lld", r->ts_ms );
    } else {
        n += snprintf( out + n, len - n, "{\"ts\":null" );
    }

    n += snprintf( out + n, len - n, ",\"uptime_ms\":%lld", r->uptime_ms );

    if ( r->dht_valid ) {
        n += snprintf( out + n, len - n, ",\"temperature\":%.1f,\"humidity\":%.1f", r->temp, r->hum );
    } else {
        n += snprintf( out + n, len - n, ",\"temperature\":null,\"humidity\":null" );
    }

    n += snprintf( out + n, len - n, ",\"mic_peak\":%d", r->mic_peak );

    if ( strcmp( r->ir, "none" ) != 0 ) {
        n += snprintf( out + n, len - n, ",\"ir_cmd\":\"%s\"", r->ir );
    } else {
        n += snprintf( out + n, len - n, ",\"ir_cmd\":null" );
    }

    n += snprintf(
        out + n, len - n,
        ",\"ac_power\":%s,\"ac_mode\":\"%s\",\"ac_setpoint\":%d,\"ac_fan\":\"%s\"}",
        r->ac_power ? "true" : "false",
        sharp_ac_mode_name( r->ac_mode ),
        r->ac_setpoint,
        sharp_ac_fan_name( r->ac_fan )
    );

    return n;
}

// looks at the oldest row without taking it out
static bool db_peek ( struct DbRow* out ) {
    bool has = false;

    portENTER_CRITICAL( &db_mux );

    if ( db_count > 0 ) {
        *out = db_rows[db_head];
        has = true;
    }

    portEXIT_CRITICAL( &db_mux );

    return has;
}

// takes the oldest row out once it got published
static void db_pop () {
    portENTER_CRITICAL( &db_mux );

    if ( db_count > 0 ) {
        db_head = ( db_head + 1 ) % DB_BUFFER_ROWS;
        --db_count;
    }

    portEXIT_CRITICAL( &db_mux );
}

// uploader task, wakes on every new row or every 2 seconds and publishes as much as it can
static void database_task ( void* arg ) {
    char json[DB_ROW_JSON_LEN];
    struct DbRow row;

    while ( true ) {
        ulTaskNotifyTake( pdTRUE, pdMS_TO_TICKS( DB_RETRY_MS ) );

        // stop when we're offline or the outbox is getting too big, rows stay in the buffer for later
        while ( db_online && esp_mqtt_client_get_outbox_size( db_client ) < DB_OUTBOX_MAX && db_peek( &row ) ) {
            int len = db_row_json( json, sizeof( json ), &row );

            if ( esp_mqtt_client_publish( db_client, DB_TOPIC, json, len, DB_QOS, 0 ) < 0 ) {
                break;
            }

            db_pop();
        }

        // report how many rows got dropped since last time
        portENTER_CRITICAL( &db_mux );
        int dropped = db_dropped;
        db_dropped = 0;
        portEXIT_CRITICAL( &db_mux );

        if ( dropped > 0 ) {
            printf("Database: buffer was full, %d oldest rows dropped\n", dropped);
        }
    }
}

// handles everything that happens on the MQTT side
static void db_event_handler ( void* arg, esp_event_base_t base, int32_t id, void* data ) {
    switch ( ( esp_mqtt_event_id_t ) id ) {
    // connected, say we're online, listen for predictions and flush whatever is waiting
    case MQTT_EVENT_CONNECTED:
        db_online = true;
        printf("Database: connected to %s\n", ADDRESS);
        esp_mqtt_client_publish( db_client, DB_STATUS_TOPIC, "online", 0, DB_QOS, 1 );
        esp_mqtt_client_subscribe( db_client, DB_PREDICTION_TOPIC, DB_QOS );

        if ( db_task_handle ) {
            xTaskNotifyGive( db_task_handle );
        }

        break;

    // got a prediction, only take it if it came in one piece
    case MQTT_EVENT_DATA: {
        esp_mqtt_event_handle_t event = data;

        if ( event->current_data_offset != 0 || event->data_len != event->total_data_len ) {
            break;
        }

        int len = event->data_len < DB_PREDICTION_LEN - 1 ? event->data_len : DB_PREDICTION_LEN - 1;

        portENTER_CRITICAL( &db_mux );
        memcpy( db_prediction, event->data, len );
        db_prediction[len] = '\0';
        portEXIT_CRITICAL( &db_mux );

        printf("Model: %s\n", db_prediction);
        db_handle_prediction( db_prediction );
        break;
    }

    // lost the connection, the client will try to reconnect by itself
    case MQTT_EVENT_DISCONNECTED:
        if ( db_online ) {
            printf("Database: disconnected\n");
        }

        db_online = false;
        break;

    default:
        break;
    }
}

// starts the MQTT client once we get an IP, the will marks us offline if we drop
static void db_on_ip ( void* arg, esp_event_base_t base, int32_t id, void* data ) {
    if ( db_client ) {
        return;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = ADDRESS,
        .credentials.client_id = DB_CLIENT_ID,
        .session.keepalive = DB_KEEPALIVE_S,
        .session.last_will = {
            .topic = DB_STATUS_TOPIC,
            .msg = "offline",
            .qos = DB_QOS,
            .retain = 1,
        },
    };

    db_client = esp_mqtt_client_init( &cfg );
    esp_mqtt_client_register_event( db_client, ESP_EVENT_ANY_ID, db_event_handler, NULL );
    esp_mqtt_client_start( db_client );
}

// starts the uploader and hooks into wifi and the logger, does nothing if ADDRESS is empty
static void database_cfg () {
    if ( ADDRESS[0] == '\0' ) {
        printf("Database: ADDRESS not set, upload off\n");
        return;
    }

    xTaskCreate( database_task, "database", DB_TASK_STACK, NULL, 3, &db_task_handle );
    ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, db_on_ip, NULL ) );
    logger_add_row_cb( database_on_row );

    printf("Database: MQTT to %s, topic %s\n", ADDRESS, DB_TOPIC);
}

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// one row every 2 seconds, and up to 3 things can listen for rows (automation, database, ThingsBoard)
#define LOG_INTERVAL_MS 2'000
#define LOG_IR_LEN 48
#define LOG_FIELD_LEN 16
#define LOGGER_MAX_ROW_CBS 3

// callback type for whoever wants every row
typedef void ( *logger_row_cb_t )( float temp, float hum, bool dht_valid, int mic_peak, const char* ir );

// the latest values, filled in by the other tasks and read once per row
static portMUX_TYPE log_mux = portMUX_INITIALIZER_UNLOCKED;
static logger_row_cb_t logger_row_cbs[LOGGER_MAX_ROW_CBS];
static int logger_row_cb_count = 0;

static float log_temp = 0.0f;
static float log_hum = 0.0f;
static bool log_dht_valid = false;
static int log_mic_peak = 0;
static char log_ir[LOG_IR_LEN] = "none";

// DHT task calls this with every good reading
static void logger_set_dht ( float temperature, float humidity ) {
    portENTER_CRITICAL( &log_mux );
    log_temp = temperature;
    log_hum = humidity;
    log_dht_valid = true;
    portEXIT_CRITICAL( &log_mux );
}

// mic loop calls this all the time, we only keep the loudest one since the last row
static void logger_add_audio ( int level_left, int level_right ) {
    int level = level_left > level_right ? level_left : level_right;

    portENTER_CRITICAL( &log_mux );

    if ( level > log_mic_peak ) {
        log_mic_peak = level;
    }

    portEXIT_CRITICAL( &log_mux );
}

// remembers the IR command so it goes in the next row
static void logger_set_ir ( const char* command ) {
    portENTER_CRITICAL( &log_mux );
    strlcpy( log_ir, command, sizeof( log_ir ) );
    portEXIT_CRITICAL( &log_mux );
}

// adds someone to the list of row listeners
static void logger_add_row_cb ( logger_row_cb_t cb ) {
    if ( logger_row_cb_count < LOGGER_MAX_ROW_CBS ) {
        logger_row_cbs[logger_row_cb_count++] = cb;
    }
}

// every 2 seconds, takes a snapshot, prints it as CSV and hands it to the listeners
static void logger_task ( void* arg ) {
    printf("CSV,uptime_ms,temp_c,humidity_pct,mic_peak,ir_cmd\n");

    TickType_t last_wake = xTaskGetTickCount();

    while ( true ) {
        vTaskDelayUntil( &last_wake, pdMS_TO_TICKS( LOG_INTERVAL_MS ) );

        // grab everything and reset the mic peak and IR for the next row
        portENTER_CRITICAL( &log_mux );
        float temp = log_temp;
        float hum = log_hum;
        bool dht_valid = log_dht_valid;
        int mic_peak = log_mic_peak;
        char ir[LOG_IR_LEN];
        memcpy( ir, log_ir, sizeof( ir ) );
        log_mic_peak = 0;
        strlcpy( log_ir, "none", sizeof( log_ir ) );
        portEXIT_CRITICAL( &log_mux );

        // empty fields if the DHT hasn't given us anything yet
        char temp_field[LOG_FIELD_LEN] = "";
        char hum_field[LOG_FIELD_LEN] = "";

        if ( dht_valid ) {
            snprintf( temp_field, sizeof( temp_field ), "%.1f", temp );
            snprintf( hum_field, sizeof( hum_field ), "%.1f", hum );
        }

        printf(
            "CSV,%lld,%s,%s,%d,%s\n",
            esp_timer_get_time() / 1'000,
            temp_field,
            hum_field,
            mic_peak,
            ir
        );

        for ( int i = 0; i < logger_row_cb_count; ++i ) {
            logger_row_cbs[i]( temp, hum, dht_valid, mic_peak, ir );
        }
    }
}

// starts the logger in its own task
static void start_logger_loop () {
    xTaskCreate( logger_task, "logger", 4'096, NULL, 4, NULL );
}

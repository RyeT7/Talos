#include "dht_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include <stdio.h>

static portMUX_TYPE dht_mux = portMUX_INITIALIZER_UNLOCKED;

static struct DhtReading dht_last = { 0.0f, 0.0f };

static int dht_wait_while ( int level ) {
    int64_t start = esp_timer_get_time();

    while ( gpio_get_level( DHT_PIN ) == level ) {
        if ( esp_timer_get_time() - start > DHT_TIMEOUT_US ) {
            return -1;
        }
    }

    return ( int )( esp_timer_get_time() - start );
}

static esp_err_t read_dht ( struct DhtReading* out ) {
    uint8_t data[5] = { 0 };

    gpio_set_level( DHT_PIN, 0 );
    esp_rom_delay_us( 1'200 );

    portENTER_CRITICAL( &dht_mux );

    gpio_set_level( DHT_PIN, 1 );
    esp_rom_delay_us( 30 );

    if ( dht_wait_while( 1 ) < 0
            || dht_wait_while( 0 ) < 0
            || dht_wait_while( 1 ) < 0 ) {
        portEXIT_CRITICAL( &dht_mux );
        return ESP_ERR_TIMEOUT;
    }

    for ( int i = 0; i < 40; ++i ) {
        if ( dht_wait_while( 0 ) < 0 ) {
            portEXIT_CRITICAL( &dht_mux );
            return ESP_ERR_TIMEOUT;
        }

        int high_us = dht_wait_while( 1 );

        if ( high_us < 0 ) {
            portEXIT_CRITICAL( &dht_mux );
            return ESP_ERR_TIMEOUT;
        }

        data[i / 8] <<= 1;

        if ( high_us > 40 ) {
            data[i / 8] |= 1;
        }
    }

    portEXIT_CRITICAL( &dht_mux );

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];

    if ( checksum != data[4] ) {
        return ESP_ERR_INVALID_CRC;
    }

    out->humidity = ( ( data[0] << 8 ) | data[1] ) / 10.0f;
    out->temperature = ( ( ( data[2] & 0x7f ) << 8 ) | data[3] ) / 10.0f;

    if ( data[2] & 0x80 ) {
        out->temperature = -out->temperature;
    }

    return ESP_OK;
}

static void dht_task ( void* arg ) {
    vTaskDelay( pdMS_TO_TICKS( 1'000 ) );

    while ( true ) {
        struct DhtReading reading;
        esp_err_t err = read_dht( &reading );

        if ( err == ESP_OK ) {
            dht_last = reading;
            printf("DHT22: %.1f C  %.1f %%\n", reading.temperature, reading.humidity);
        } else {
            printf("DHT22: read failed (%s)\n", esp_err_to_name( err ));
        }

        vTaskDelay( pdMS_TO_TICKS( DHT_INTERVAL_MS ) );
    }
}

static void start_dht_loop () {
    xTaskCreate( dht_task, "dht", 4'096, NULL, 5, NULL );
}

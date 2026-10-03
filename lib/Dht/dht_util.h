#include "dht_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include <stdio.h>

// used to turn off interrupts while reading since the timing is super tight
static portMUX_TYPE dht_mux = portMUX_INITIALIZER_UNLOCKED;

// the last good reading, used to check if temp or humidity went up
static struct DhtReading dht_last = { 0.0f, 0.0f };

// waits while the pin stays at this level and returns how long it took in microseconds, or -1 if it timed out
static int dht_wait_while ( int level ) {
    int64_t start = esp_timer_get_time();

    while ( gpio_get_level( DHT_PIN ) == level ) {
        if ( esp_timer_get_time() - start > DHT_TIMEOUT_MICROSECONDS ) {
            return -1;
        }
    }

    return ( int )( esp_timer_get_time() - start );
}

// reads temp and humidity straight off the wire without any library
static esp_err_t read_dht ( struct DhtReading* out ) {
    uint8_t data[5] = { 0 };

    // pull it low for a bit to tell the sensor we want a reading
    gpio_set_level( DHT_PIN, 0 );
    esp_rom_delay_us( 1'200 );

    portENTER_CRITICAL( &dht_mux );

    // let go of the line and wait for the sensor to answer
    gpio_set_level( DHT_PIN, 1 );
    esp_rom_delay_us( 30 );

    // sensor responds with low then high before it starts sending bits
    if ( dht_wait_while( 1 ) < 0
            || dht_wait_while( 0 ) < 0
            || dht_wait_while( 1 ) < 0 ) {
        portEXIT_CRITICAL( &dht_mux );
        return ESP_ERR_TIMEOUT;
    }

    // 40 bits total, each bit is a low then a high, a long high means 1 and a short one means 0
    for ( int i = 0; i < 40; ++i ) {
        if ( dht_wait_while( 0 ) < 0 ) {
            portEXIT_CRITICAL( &dht_mux );
            return ESP_ERR_TIMEOUT;
        }

        int high_microseconds = dht_wait_while( 1 );

        if ( high_microseconds < 0 ) {
            portEXIT_CRITICAL( &dht_mux );
            return ESP_ERR_TIMEOUT;
        }

        data[i / 8] <<= 1;

        if ( high_microseconds > 40 ) {
            data[i / 8] |= 1;
        }
    }

    portEXIT_CRITICAL( &dht_mux );

    // last byte is the sum of the first 4, if it doesn't match the read is garbage
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];

    if ( checksum != data[4] ) {
        return ESP_ERR_INVALID_CRC;
    }

    // both values come in tenths, and the top bit of temp is the minus sign
    out->humidity = ( ( data[0] << 8 ) | data[1] ) / 10.0f;
    out->temperature = ( ( ( data[2] & 0x7f ) << 8 ) | data[3] ) / 10.0f;

    if ( data[2] & 0x80 ) {
        out->temperature = -out->temperature;
    }

    return ESP_OK;
}

// old task that just reads and prints, main uses dht_speaker_task now
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

// starts the print only DHT task
static void start_dht_loop () {
    xTaskCreate( dht_task, "dht", 4'096, NULL, 5, NULL );
}

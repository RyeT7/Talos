#include "oled_init.h"
#include "oled_display.h"

#include "mic_init.h"
#include "mic_util.h"

#include "speaker_init.h"
#include "speaker_util.h"

#include "dht_init.h"
#include "dht_util.h"

#include "blynk_init.h"
#include "blynk_util.h"

#include <stdio.h>

#define SOUND_THRESHOLD 60'000

static void dht_speaker_task ( void* arg ) {
    vTaskDelay( pdMS_TO_TICKS( 1'000 ) );

    while ( true ) {
        struct DhtReading reading;
        esp_err_t err = read_dht( &reading );

        if ( err != ESP_OK ) {
            vTaskDelay( pdMS_TO_TICKS( DHT_INTERVAL_MS ) );
            continue;
        }

        blynk_send_float( BLYNK_DS_TEMPERATURE, reading.temperature );
        blynk_send_float( BLYNK_DS_HUMIDITY, reading.humidity );

        if (
            reading.temperature > dht_last.temperature
            || reading.humidity > dht_last.humidity
        ) {
            startup_chime();
        }
        
        dht_last = reading;

        vTaskDelay( pdMS_TO_TICKS( DHT_INTERVAL_MS ) );
    }
}

static void on_blynk_downlink ( const char* datastream, const char* value ) {
    printf("Blynk: %s = %s\n", datastream, value);
}

static void start_dht_speaker_loop () {
    xTaskCreate( dht_speaker_task, "dht_speaker", 4'096, NULL, 5, NULL );
}

void app_main() {
    oled_cfg();
    oled_startup_commands();
    
    mic_cfg();

    speaker_cfg();

    dht_cfg();

    blynk_cfg( on_blynk_downlink );

    start_dht_speaker_loop();
    
    while (true) {
        struct AudioLevel level = read_audio();
        // printf("L: %d  R: %d\n", level.level_left, level.level_right);

        blynk_send_int( BLYNK_DS_AUDIO_LEVEL, level.level_left );

        if ( level.level_left > SOUND_THRESHOLD
                || level.level_right > SOUND_THRESHOLD ) {
            audio_display(
                to_bar(level.level_left),
                to_bar(level.level_right)
            );
        } else {
            blinking_eyes();
        }
    }
}
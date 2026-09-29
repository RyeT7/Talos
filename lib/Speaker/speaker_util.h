#include "speaker_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

static int16_t spk_buf[SPK_NUM_FRAMES * 2];

static void play_tone ( int freq_hz, int duration_ms, int volume ) {
    if ( volume > 100 ) {
        volume = 100;
    }

    const float amplitude = 32'767.0f * volume / 100.0f;
    const float step = 2.0f * M_PI * freq_hz / SPK_SAMPLE_RATE;
    float phase = 0.0f;

    int total_frames = SPK_SAMPLE_RATE * duration_ms / 1'000;

    while ( total_frames > 0 ) {
        int frames = total_frames < SPK_NUM_FRAMES ? total_frames : SPK_NUM_FRAMES;

        for ( int i = 0; i < frames; ++i ) {
            int16_t s = ( int16_t )( amplitude * sinf( phase ) );

            spk_buf[2 * i] = s;
            spk_buf[2 * i + 1] = s;

            phase += step;
            if ( phase >= 2.0f * M_PI ) {
                phase -= 2.0f * M_PI;
            }
        }

        size_t bytes_written = 0;
        i2s_channel_write( tx, spk_buf, frames * 2 * sizeof( int16_t ), &bytes_written, portMAX_DELAY );

        total_frames -= frames;
    }
}

static void play_silence ( int duration_ms ) {
    play_tone( 0, duration_ms, 0 );
}

static void startup_chime () {
    play_tone( 880, 300, 60 );
    play_silence( 100 );
    play_tone( 1'320, 500, 60 );
    play_silence( 100 );
}

#define CHIME_INTERVAL_MS 1'000

static void chime_task ( void* arg ) {
    while ( true ) {
        startup_chime();
        vTaskDelay( pdMS_TO_TICKS( CHIME_INTERVAL_MS ) );
    }
}

static void start_chime_loop () {
    xTaskCreate( chime_task, "chime", 4'096, NULL, 5, NULL );
}

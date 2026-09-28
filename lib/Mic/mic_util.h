#include "mic_config.h"
#include "freertos/FreeRTOS.h"

int32_t samples[NUM_SAMPLE];

struct AudioLevel {
    int level_left;
    int level_right;
};

static int to_bar( int level ) {
    int w = level / 10'000;
    
    if ( w > 100 ) {
        w = 100;
    }

    return w;
}

struct AudioLevel read_audio () {
    size_t bytes_read = 0;
    i2s_channel_read( rx, samples, sizeof(samples), &bytes_read, portMAX_DELAY );

    int count = bytes_read / sizeof( int32_t );

    if ( count == 0 ) {
        struct AudioLevel level = {
            0,
            0
        };

        return level;
    }

    int64_t mean_left = 0;
    int64_t mean_right = 0;

    int pairs = count / 2;

    for ( int i = 0; i < pairs; ++i ) {
        mean_left += samples[2 * i] >> 8;
        mean_right += samples[2 * i + 1] >> 8;
    }

    mean_left /= pairs;
    mean_right /= pairs;

    int64_t sum_left = 0;
    int64_t sum_right = 0;

    for ( int i = 0; i < pairs; ++i ) {
        sum_left += llabs( (samples[2 * i] >> 8) - mean_left );
        sum_right += llabs( (samples[2 * i + 1] >> 8) - mean_right );
    }

    struct AudioLevel level = {
        ( int )( sum_left / pairs ),
        ( int )( sum_right / pairs )
    };

    return level;
}
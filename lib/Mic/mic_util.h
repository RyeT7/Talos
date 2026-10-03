#include "mic_config.h"
#include "freertos/FreeRTOS.h"

// buffer for the raw samples
int32_t samples[NUM_SAMPLE];

// how loud each side is
struct AudioLevel {
    int level_left;
    int level_right;
};

// turns the audio level into a bar width for the screen, maxes out at 100
static int to_bar( int level ) {
    int w = level / 10'000;
    
    if ( w > 100 ) {
        w = 100;
    }

    return w;
}

// reads a chunk from the mic and works out how loud each side is
struct AudioLevel read_audio () {
    size_t bytes_read = 0;
    i2s_channel_read( rx, samples, sizeof(samples), &bytes_read, portMAX_DELAY );

    int count = bytes_read / sizeof( int32_t );

    // got nothing, so it's just silence
    if ( count == 0 ) {
        struct AudioLevel level = {
            0,
            0
        };

        return level;
    }

    int64_t mean_left = 0;
    int64_t mean_right = 0;

    // samples go left right left right, so each pair is one frame
    int pairs = count / 2;

    // get the average first so we can get rid of the DC offset, the shift drops the useless low 8 bits
    for ( int i = 0; i < pairs; ++i ) {
        mean_left += samples[2 * i] >> 8;
        mean_right += samples[2 * i + 1] >> 8;
    }

    mean_left /= pairs;
    mean_right /= pairs;

    // then the loudness is just the average distance from that
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
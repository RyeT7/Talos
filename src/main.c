#include "oled_init.h"
#include "oled_display.h"

#include "mic_init.h"
#include "mic_util.h"

#include <stdio.h>

#define SOUND_THRESHOLD 60'000

void app_main() {
    oled_cfg();
    oled_startup_commands();
    
    mic_cfg();
    
    while (true) {
        struct AudioLevel level = read_audio();
        // printf("L: %d  R: %d\n", level.level_left, level.level_right);

        if ( level.level_left > SOUND_THRESHOLD
                || level.level_right > SOUND_THRESHOLD ) {
            audio_display(
                to_bar(level.level_left),
                to_bar(level.level_right)
            );
        } else {
            audio_display(
                0,
                0
            );
        }
    }
}
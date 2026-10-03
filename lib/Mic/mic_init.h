#include "mic_config.h"

// handle for the mic channel
i2s_chan_handle_t rx;

// sets up the mic channel and starts it, reading both left and right
static void mic_cfg () {
    ESP_ERROR_CHECK ( i2s_new_channel( &mic_chan_cfg, NULL, &rx ) );

    // while I know currently, our mic only uses the left slot this is purposefully left to use both for better scalability in the future
    mic_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    ESP_ERROR_CHECK( i2s_channel_init_std_mode( rx, &mic_std_cfg ) );
    ESP_ERROR_CHECK( i2s_channel_enable( rx ) );
}
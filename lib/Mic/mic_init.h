#include "mic_config.h"

i2s_chan_handle_t rx;

static void mic_cfg () {
    ESP_ERROR_CHECK ( i2s_new_channel( &mic_chan_cfg, NULL, &rx ) );

    mic_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    ESP_ERROR_CHECK( i2s_channel_init_std_mode( rx, &mic_std_cfg ) );
    ESP_ERROR_CHECK( i2s_channel_enable( rx ) );
}
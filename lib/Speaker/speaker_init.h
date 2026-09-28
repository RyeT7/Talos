#include "speaker_config.h"

i2s_chan_handle_t tx;

static void speaker_cfg () {
    spk_chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK( i2s_new_channel( &spk_chan_cfg, &tx, NULL ) );
    ESP_ERROR_CHECK( i2s_channel_init_std_mode( tx, &spk_std_cfg ) );
    ESP_ERROR_CHECK( i2s_channel_enable( tx ) );
}

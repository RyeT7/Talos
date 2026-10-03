#pragma once

#include "driver/i2s_std.h"

// I2S pins for the speaker amp
#define SPK_BCLK_PIN GPIO_NUM_15
#define SPK_LRC_PIN GPIO_NUM_16
#define SPK_DIN_PIN GPIO_NUM_7

// 16 kHz is fine for beeps
#define SPK_SAMPLE_RATE 16'000
// how many frames we fill up before sending them out
#define SPK_NUM_FRAMES 256

extern i2s_chan_handle_t tx;

// speaker is on I2S port 1 since the mic already uses port 0
i2s_chan_config_t spk_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
    I2S_NUM_1,
    I2S_ROLE_MASTER
);

// 16 bit stereo, send only so no mclk and no din
i2s_std_config_t spk_std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPK_SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO
    ),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = SPK_BCLK_PIN,
        .ws = SPK_LRC_PIN,
        .dout = SPK_DIN_PIN,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false
        },
    },
};

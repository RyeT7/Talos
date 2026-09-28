#pragma once

#include "driver/i2s_std.h"

#define WS_PIN GPIO_NUM_4
#define SCK_PIN GPIO_NUM_5
#define SD_PIN GPIO_NUM_6

#define SAMPLE_RATE 16'000
#define NUM_SAMPLE 3'200

extern int32_t samples[NUM_SAMPLE];

extern i2s_chan_handle_t rx;

i2s_chan_config_t mic_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
    I2S_NUM_0,
    I2S_ROLE_MASTER
);

i2s_std_config_t mic_std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_STEREO
    ),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = SCK_PIN,
        .ws = WS_PIN,
        .dout = I2S_GPIO_UNUSED,
        .din = SD_PIN,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false
        },
    },
};
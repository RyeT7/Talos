#pragma once

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define IR_TX_PIN GPIO_NUM_10
#define IR_RX_PIN GPIO_NUM_11

#define IR_TX_DRIVE GPIO_DRIVE_CAP_1

#define IR_RESOLUTION_HZ 1'000'000
#define IR_CARRIER_HZ 38'000
#define IR_CARRIER_DUTY 0.33f

#define IR_TX_MEM_SYMBOLS 192
#define IR_RX_MEM_SYMBOLS 192
#define IR_MAX_SYMBOLS 256
#define IR_MIN_FRAME_SYMBOLS 16

#define IR_RX_MIN_PULSE_NS 1'250
#define IR_RX_MAX_PULSE_NS 12'000'000

#define IR_TOLERANCE_PCT 30
#define IR_TOLERANCE_US 150

#define IR_TX_TIMEOUT_MS 1'000

struct IrTiming {
    uint16_t hdr_mark;
    uint16_t hdr_space;
    uint16_t bit_mark;
    uint16_t zero_space;
    uint16_t one_space;
    uint16_t gap;
};

typedef void ( *ir_frame_cb_t )( const rmt_symbol_word_t* symbols, size_t count );

static rmt_channel_handle_t ir_tx = NULL;
static rmt_channel_handle_t ir_rx = NULL;
static rmt_encoder_handle_t ir_encoder = NULL;
static QueueHandle_t ir_rx_queue = NULL;
static ir_frame_cb_t ir_frame_cb = NULL;

static rmt_symbol_word_t ir_rx_buf[IR_MAX_SYMBOLS];
static rmt_symbol_word_t ir_tx_buf[IR_MAX_SYMBOLS];

static rmt_symbol_word_t ir_last_frame[IR_MAX_SYMBOLS];
static size_t ir_last_len = 0;

rmt_tx_channel_config_t ir_tx_cfg = {
    .gpio_num = IR_TX_PIN,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = IR_RESOLUTION_HZ,
    .mem_block_symbols = IR_TX_MEM_SYMBOLS,
    .trans_queue_depth = 4,
};

rmt_rx_channel_config_t ir_rx_cfg = {
    .gpio_num = IR_RX_PIN,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = IR_RESOLUTION_HZ,
    .mem_block_symbols = IR_RX_MEM_SYMBOLS,
};

rmt_carrier_config_t ir_carrier_cfg = {
    .frequency_hz = IR_CARRIER_HZ,
    .duty_cycle = IR_CARRIER_DUTY,
};

rmt_receive_config_t ir_receive_cfg = {
    .signal_range_min_ns = IR_RX_MIN_PULSE_NS,
    .signal_range_max_ns = IR_RX_MAX_PULSE_NS,
};

rmt_transmit_config_t ir_transmit_cfg = {
    .loop_count = 0,
};

rmt_copy_encoder_config_t ir_encoder_cfg = {};

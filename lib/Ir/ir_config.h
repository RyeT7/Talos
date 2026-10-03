#pragma once

#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// IR LED goes on TX, the receiver module goes on RX
#define IR_TX_PIN GPIO_NUM_10
#define IR_RX_PIN GPIO_NUM_11

// how hard the pin drives the LED, kept low so the LED doesn't go kablooey
#define IR_TX_DRIVE GPIO_DRIVE_CAP_1

// 1 MHz so 1 tick is 1 microsecond, makes the timings easy to read
#define IR_RESOLUTION_HZ 1'000'000
// most AC remotes use a 38 kHz carrier
#define IR_CARRIER_HZ 38'000
#define IR_CARRIER_DUTY 0.33f

// RMT memory for TX and RX, and the biggest frame we'll ever deal with
#define IR_TX_MEM_SYMBOLS 192
#define IR_RX_MEM_SYMBOLS 192
#define IR_MAX_SYMBOLS 256
// anything shorter than this is probably noise
#define IR_MIN_FRAME_SYMBOLS 16

// pulses shorter than min get ignored, a gap longer than max means the frame is done
#define IR_RX_MIN_PULSE_NS 1'250
#define IR_RX_MAX_PULSE_NS 12'000'000

// how far off a pulse can be and still count as a match
#define IR_TOLERANCE_PCT 30
#define IR_TOLERANCE_MICROSECONDS 150

// max wait for a send to finish
#define IR_TX_TIMEOUT_MS 1'000

// timings for a pulse distance protocol, all in microseconds
struct IrTiming {
    uint16_t hdr_mark;
    uint16_t hdr_space;
    uint16_t bit_mark;
    uint16_t zero_space;
    uint16_t one_space;
    uint16_t gap;
};

// callback type for when we receive a full frame
typedef void ( *ir_frame_cb_t )( const rmt_symbol_word_t* symbols, size_t count );

// RMT channels, the encoder, and the queue the RX interrupt pushes into
static rmt_channel_handle_t ir_tx = NULL;
static rmt_channel_handle_t ir_rx = NULL;
static rmt_encoder_handle_t ir_encoder = NULL;
static QueueHandle_t ir_rx_queue = NULL;
static ir_frame_cb_t ir_frame_cb = NULL;

// buffers for receiving and sending
static rmt_symbol_word_t ir_rx_buf[IR_MAX_SYMBOLS];
static rmt_symbol_word_t ir_tx_buf[IR_MAX_SYMBOLS];

// copy of the last frame we got so we can replay it
static rmt_symbol_word_t ir_last_frame[IR_MAX_SYMBOLS];
static size_t ir_last_len = 0;

// TX channel config
rmt_tx_channel_config_t ir_tx_cfg = {
    .gpio_num = IR_TX_PIN,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = IR_RESOLUTION_HZ,
    .mem_block_symbols = IR_TX_MEM_SYMBOLS,
    .trans_queue_depth = 4,
};

// RX channel config
rmt_rx_channel_config_t ir_rx_cfg = {
    .gpio_num = IR_RX_PIN,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = IR_RESOLUTION_HZ,
    .mem_block_symbols = IR_RX_MEM_SYMBOLS,
};

// the 38 kHz carrier that gets mixed into the marks
rmt_carrier_config_t ir_carrier_cfg = {
    .frequency_hz = IR_CARRIER_HZ,
    .duty_cycle = IR_CARRIER_DUTY,
};

// what counts as a valid pulse when receiving
rmt_receive_config_t ir_receive_cfg = {
    .signal_range_min_ns = IR_RX_MIN_PULSE_NS,
    .signal_range_max_ns = IR_RX_MAX_PULSE_NS,
};

// send each frame once, no looping
rmt_transmit_config_t ir_transmit_cfg = {
    .loop_count = 0,
};

// copy encoder just sends the symbols as is, nothing fancy
rmt_copy_encoder_config_t ir_encoder_cfg = {};

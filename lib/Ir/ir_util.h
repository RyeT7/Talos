#include "ir_config.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

// makes one symbol, a mark (LED on) followed by a space (LED off)
static rmt_symbol_word_t ir_symbol ( uint16_t mark_microseconds, uint16_t space_microseconds ) {
    rmt_symbol_word_t s = {
        .duration0 = mark_microseconds,
        .level0 = 1,
        .duration1 = space_microseconds,
        .level1 = 0,
    };

    return s;
}

// checks if a measured pulse is close enough to what we expect
static bool ir_match ( uint32_t measured, uint32_t expected ) {
    uint32_t tolerance = expected * IR_TOLERANCE_PCT / 100 + IR_TOLERANCE_MICROSECONDS;

    return measured + tolerance >= expected && measured <= expected + tolerance;
}

// sends raw symbols and waits until it's actually done
static esp_err_t ir_send_symbols ( const rmt_symbol_word_t* symbols, size_t count ) {
    esp_err_t err = rmt_transmit(
        ir_tx,
        ir_encoder,
        symbols,
        count * sizeof( rmt_symbol_word_t ),
        &ir_transmit_cfg
    );

    if ( err != ESP_OK ) {
        return err;
    }

    return rmt_tx_wait_all_done( ir_tx, IR_TX_TIMEOUT_MS );
}

// turns bytes into a pulse distance frame and sends it, LSB first
static esp_err_t ir_send_pulse_distance (
    const struct IrTiming* t,
    const uint8_t* data,
    int bits
) {
    // header and footer take 2 symbols so make sure everything fits
    if ( bits + 2 > IR_MAX_SYMBOLS ) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t n = 0;
    ir_tx_buf[n++] = ir_symbol( t->hdr_mark, t->hdr_space );

    // the space length after each mark is what decides if it's a 0 or a 1
    for ( int i = 0; i < bits; ++i ) {
        bool bit = data[i / 8] & ( 1 << ( i % 8 ) );
        ir_tx_buf[n++] = ir_symbol( t->bit_mark, bit ? t->one_space : t->zero_space );
    }

    // one last mark to end it, then the gap
    ir_tx_buf[n++] = ir_symbol( t->bit_mark, t->gap );

    return ir_send_symbols( ir_tx_buf, n );
}

// opposite of the one above, turns received symbols back into bytes and returns how many bits it got
static int ir_decode_pulse_distance (
    const struct IrTiming* t,
    const rmt_symbol_word_t* symbols,
    size_t count,
    uint8_t* out,
    int max_bits
) {
    // header doesn't match so it's not the protocol we're looking for
    if (
        count < 2 ||
        !ir_match( symbols[0].duration0, t->hdr_mark ) ||
        !ir_match( symbols[0].duration1, t->hdr_space )
    ) {
        return 0;
    }

    memset( out, 0, ( max_bits + 7 ) / 8 );

    // anything longer than halfway between the 0 and 1 space counts as a 1
    uint32_t threshold = ( t->zero_space + t->one_space ) / 2;
    int bits = 0;

    for ( size_t i = 1; i < count && bits < max_bits; ++i ) {
        // stop if the mark looks off or we hit the end of the frame
        if (
            !ir_match( symbols[i].duration0, t->bit_mark ) ||
            symbols[i].duration1 == 0
        ) {
            break;
        }

        if ( symbols[i].duration1 > threshold ) {
            out[bits / 8] |= 1 << ( bits % 8 );
        }

        ++bits;
    }

    return bits;
}

// saves a copy of the frame so we can replay it later
static void ir_store_last ( const rmt_symbol_word_t* symbols, size_t count ) {
    if ( count > IR_MAX_SYMBOLS ) {
        count = IR_MAX_SYMBOLS;
    }

    // the last symbol has a 0 space since the receiver just stops there, so give it a proper gap
    for ( size_t i = 0; i < count; ++i ) {
        uint16_t space = symbols[i].duration1;

        ir_last_frame[i] = ir_symbol( symbols[i].duration0, space ? space : 20'000 );
    }

    ir_last_len = count;
}

// dumps the raw timings, handy for figuring out unknown remotes
static void ir_print_raw ( const rmt_symbol_word_t* symbols, size_t count ) {
    printf("IR RAW (%d symbols, mark/space microseconds):", ( int )count);

    for ( size_t i = 0; i < count; ++i ) {
        printf(" %d/%d", symbols[i].duration0, symbols[i].duration1);
    }

    printf("\n");
}

// sends the last captured frame again exactly as it came in
static esp_err_t ir_replay_last () {
    if ( ir_last_len == 0 ) {
        return ESP_ERR_NOT_FOUND;
    }

    return ir_send_symbols( ir_last_frame, ir_last_len );
}

// waits for frames from the interrupt, saves them, and passes them to the callback
static void ir_listen_task ( void* arg ) {
    rmt_rx_done_event_data_t rx_data;

    ESP_ERROR_CHECK( rmt_receive( ir_rx, ir_rx_buf, sizeof( ir_rx_buf ), &ir_receive_cfg ) );

    while ( true ) {
        if ( xQueueReceive( ir_rx_queue, &rx_data, portMAX_DELAY ) != pdTRUE ) {
            continue;
        }

        // skip short stuff since it's probably just noise
        if ( rx_data.num_symbols >= IR_MIN_FRAME_SYMBOLS ) {
            ir_store_last( rx_data.received_symbols, rx_data.num_symbols );

            if ( ir_frame_cb ) {
                ir_frame_cb( rx_data.received_symbols, rx_data.num_symbols );
            }
        }

        // RMT stops after each frame so we have to start listening again
        ESP_ERROR_CHECK( rmt_receive( ir_rx, ir_rx_buf, sizeof( ir_rx_buf ), &ir_receive_cfg ) );
    }
}

// starts the listen task with whatever should handle the frames
static void start_ir_listen_loop ( ir_frame_cb_t on_frame ) {
    ir_frame_cb = on_frame;
    xTaskCreate( ir_listen_task, "ir_listen", 4'096, NULL, 5, NULL );
}

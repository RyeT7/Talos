#include "ir_config.h"
#include "driver/gpio.h"

static bool ir_rx_done_cb (
    rmt_channel_handle_t chan,
    const rmt_rx_done_event_data_t* edata,
    void* ctx
) {
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR( ir_rx_queue, edata, &woken );

    return woken == pdTRUE;
}

static void ir_cfg () {
    ESP_ERROR_CHECK( rmt_new_tx_channel( &ir_tx_cfg, &ir_tx ) );
    ESP_ERROR_CHECK( rmt_apply_carrier( ir_tx, &ir_carrier_cfg ) );
    ESP_ERROR_CHECK( rmt_new_copy_encoder( &ir_encoder_cfg, &ir_encoder ) );
    ESP_ERROR_CHECK( rmt_enable( ir_tx ) );

    ESP_ERROR_CHECK( gpio_set_drive_capability( IR_TX_PIN, IR_TX_DRIVE ) );

    ir_rx_queue = xQueueCreate( 1, sizeof( rmt_rx_done_event_data_t ) );

    rmt_rx_event_callbacks_t ir_rx_cbs = {
        .on_recv_done = ir_rx_done_cb,
    };

    ESP_ERROR_CHECK( rmt_new_rx_channel( &ir_rx_cfg, &ir_rx ) );
    ESP_ERROR_CHECK( rmt_rx_register_event_callbacks( ir_rx, &ir_rx_cbs, NULL ) );
    ESP_ERROR_CHECK( rmt_enable( ir_rx ) );
}

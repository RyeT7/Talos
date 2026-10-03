#pragma once

#include "driver/i2c_master.h"
#include <stdint.h>

// sends one command byte to the screen, the 0x00 in front tells it this is a command and not pixel data
esp_err_t run_dev_cmd ( i2c_master_dev_handle_t dev, uint8_t cmd ) {
    uint8_t buf[2] = { 0x00, cmd };
    return i2c_master_transmit( dev, buf, sizeof( buf ), 100 );
}
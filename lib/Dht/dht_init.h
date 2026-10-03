#include "dht_config.h"

// sets up the pin and leaves it high since that's the idle state
static void dht_cfg () {
    ESP_ERROR_CHECK( gpio_config( &dht_io_cfg ) );
    gpio_set_level( DHT_PIN, 1 );
}

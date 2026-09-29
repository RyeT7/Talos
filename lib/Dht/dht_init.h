#include "dht_config.h"

static void dht_cfg () {
    ESP_ERROR_CHECK( gpio_config( &dht_io_cfg ) );
    gpio_set_level( DHT_PIN, 1 );
}

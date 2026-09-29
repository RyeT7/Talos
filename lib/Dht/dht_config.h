#pragma once

#include "driver/gpio.h"

#define DHT_PIN GPIO_NUM_17

#define DHT_INTERVAL_MS 2'000
#define DHT_TIMEOUT_US 200

struct DhtReading {
    float temperature;
    float humidity;
};

gpio_config_t dht_io_cfg = {
    .pin_bit_mask = 1ULL << DHT_PIN,
    .mode = GPIO_MODE_INPUT_OUTPUT_OD,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

#pragma once

#include "driver/gpio.h"

// data pin for the DHT22
#define DHT_PIN GPIO_NUM_17

// DHT22 can't be read faster than every 2 seconds
#define DHT_INTERVAL_MS 2'000
// if the pin doesn't change for this long something's wrong
#define DHT_TIMEOUT_MICROSECONDS 200

// one reading from the sensor
struct DhtReading {
    float temperature;
    float humidity;
};

// open drain with pull up so we can both pull it low and read from the same pin
gpio_config_t dht_io_cfg = {
    .pin_bit_mask = 1ULL << DHT_PIN,
    .mode = GPIO_MODE_INPUT_OUTPUT_OD,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};

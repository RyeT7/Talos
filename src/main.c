#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_init.h"
#include "oled_display.h"
#include <stdio.h>

void app_main() {
    i2c_master_bus_handle_t bus;

    ESP_ERROR_CHECK(i2c_new_master_bus(&oled_bus_cfg, &bus));
    
    bool oled_probe_res = probe_oled_device(bus);

    if ( !oled_probe_res ) {
        return;
    }

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &oled_dev_cfg, &oled));

    oled_init();
    blinking_eyes();
}
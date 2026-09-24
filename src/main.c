#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_init.h"
#include "oled_display.h"
#include <stdio.h>

#define SDA_PIN GPIO_NUM_8
#define SCL_PIN GPIO_NUM_9
#define OLED_ADDR 0x3c
#define OLED_SPEED_HZ 400'000

void app_main() {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,  
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    if ( i2c_master_probe(bus, OLED_ADDR, 500) != ESP_OK ) {
        printf("Device not found at 0x%02X\n", OLED_ADDR);

        while ( true ) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    printf("Found device at 0x%02X\n", OLED_ADDR);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_7,
        .device_address = OLED_ADDR,
        .scl_speed_hz = OLED_SPEED_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &oled));

    oled_init();

    while ( true ) {
        draw_eyes(true);
        vTaskDelay(pdMS_TO_TICKS(3000));
        draw_eyes(false);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_init.h"
#include "oled_display.h"
#include <stdio.h>

void app_main() {
    oled_configs();
    oled_startup_commands();
    blinking_eyes();
}
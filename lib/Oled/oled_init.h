#include "oled_config.h"
#include "commands.h"
#include <stdint.h>
#include <stdbool.h>

static void oled_configs() {
    i2c_master_bus_handle_t bus;

    ESP_ERROR_CHECK(i2c_new_master_bus(&oled_bus_cfg, &bus));
    
    bool oled_probe_res = probe_oled_device(bus);

    if ( !oled_probe_res ) {
        return;
    }

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &oled_dev_cfg, &oled));
}

static void oled_startup_commands() {
    const uint8_t startup_cmds[] = {
        DISPLAY_ON                          ,
        SET_DISPLAY_CLOCK_DIVIDE            , 0x80  ,
        SET_MULTIPLEX_RATIO                 , 0x3f  ,
        SET_DISPLAY_OFFSET                  , 0x00  ,
        SET_DISPLAY_START_LINE_ZERO         ,
        SET_CHARGE_PUMP                     , 0x14  ,
        SET_ADDRESSING_MODE                 , 0x00  ,
        SET_SEGMENT_REMAP                   ,
        SET_COM_OUTPUT_SCAN_DIRECTION       ,
        SET_COM_PINS                        , 0x12  ,
        SET_CONTRAST                        , 0xcf  ,
        SET_PRE_CHARGE_PERIOD               , 0xf1  ,
        SET_VCOMH_DESELECT_LEVEL            , 0x40  ,
        RAM_CONTENT_DISPLAY                 ,
        NORMAL_DISPLAY                      ,
        DISPLAY_ON                          ,
    };

    for ( size_t i = 0; i < sizeof( startup_cmds ); ++i ) {
        run_dev_cmd( oled, startup_cmds[i] );
    }
}

bool probe_oled_device (i2c_master_bus_handle_t bus) {
    if ( i2c_master_probe(bus, OLED_ADDR, 500) != ESP_OK ) {
        printf("Device not found at 0x%02X\n", OLED_ADDR);

        return false;
    }

    printf("Found device at 0x%02X\n", OLED_ADDR);
    return true;
}
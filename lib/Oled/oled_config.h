#include "driver/i2c_master.h"

#define W 128
#define H 64

static i2c_master_dev_handle_t oled;
static uint8_t fb[W * H / 8];

enum SSD1306_commands {
    SET_CONTRAST                            = 0x81,
    RAM_CONTENT_DISPLAY                     = 0xa4,
    ENTIRE_DISPLAY                          = 0xa5,
    NORMAL_DISPLAY                          = 0xa6,
    INVERSE_DISPLAY                         = 0xa7,
    DISPLAY_OFF                             = 0xae,
    DISPLAY_ON                              = 0xaf,
    RIGHT_HORIZONTAL_SCROLL                 = 0x26,
    LEFT_HORIZONTAL_SCROLL                  = 0x27,
    VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL    = 0x29,
    HORIZONTAL_AND_RIGHT_HORIZONTAL_SCROLL  = 0x2a,
    DEACTIVATE_SCROLL                       = 0x2e,
    ACTIVATE_SCROLL                         = 0x2f,
    SET_VERTICAL_SCROLL_AREA                = 0xa3,
    SET_MULTIPLEX_RATIO                     = 0xa8,
    SET_DISPLAY_OFFSET                      = 0xd3,
    SET_DISPLAY_CLOCK_DIVIDE                = 0xd5,
    SET_COM_PINS                            = 0xda,
    SET_PRE_CHARGE_PERIOD                   = 0xd9,
    SET_DISPLAY_START_LINE_ZERO             = 0x40,
    SET_DISPLAY_START_LINE_MAX              = 0x7f,
    SET_ADDRESSING_MODE                     = 0x20,
    SET_SEGMENT_REMAP                       = 0xa1,
    SET_SEGMENT_REMAP_RESET                 = 0xa0,
    SET_COM_OUTPUT_SCAN_DIRECTION           = 0xc8,
    SET_COM_OUTPUT_SCAN_DIRECTION_RESET     = 0xc0,
    SET_CHARGE_PUMP                         = 0x8d,
    SET_VCOMH_DESELECT_LEVEL                = 0xdb,
    SET_COLUMN_ADDRESS                      = 0x21,
    SET_PAGE_ADDRESS                        = 0x22,
};
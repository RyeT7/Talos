#include "driver/i2c_master.h"
#include "oled_config.h"
#include "commands.h"
#include <string.h>

static void set_pixel ( int x, int y ) {
    if ( x < 0 || x >= W || y < 0 || y >= H ) {
        return;
    }

    fb[x + (y / 8) * W] |= ( 1 << ( y % 8 ) );
}

static void fill_circle ( int x, int y, int r ) {
    for ( int dy = -r; dy <= r; ++dy ) {
        for ( int dx = -r; dx <= r; ++dx ) {
            if ( dx * dx + dy * dy <= r * r ) {
                set_pixel(x + dx, y + dy);
            }
        }
    }
}

static void fill_rect ( int x, int y, int w, int h ) {
    for ( int j = y; j < y + h; ++j ) {
        for ( int i = x; i < x + w; ++i ) {
            set_pixel( i, j );
        }
    }
}

static void oled_flush () {
    run_dev_cmd( oled, SET_COLUMN_ADDRESS ); run_dev_cmd( oled, 0 ); run_dev_cmd( oled, W - 1 );
    run_dev_cmd( oled, SET_PAGE_ADDRESS ); run_dev_cmd( oled, 0 ); run_dev_cmd( oled, 7 );
    
    static uint8_t buf[1 + W];
    buf[0] = 0x40;

    for ( int i = 0; i < 8; ++i ) {
        memcpy( &buf[1], &fb[i * W], W );
        i2c_master_transmit( oled, buf, sizeof(buf), 100 );
    }
}

static void clear () {
    memset( fb, 0, sizeof(fb) );
}

static void draw_eyes ( bool open ) {
    clear();

    if ( open ) {
        fill_circle(40, 32, 12);
        fill_circle(88, 32, 12);
    } else {
        fill_rect(28, 31, 25, 3);
        fill_rect(76, 31, 25, 3);
    }

    oled_flush();
}
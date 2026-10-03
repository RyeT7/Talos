#include "driver/i2c_master.h"
#include "oled_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "commands.h"
#include "esp_timer.h"
#include <string.h>

// turns on one pixel in the frame buffer, anything off screen gets ignored
static void set_pixel ( int x, int y ) {
    if ( x < 0 || x >= W || y < 0 || y >= H ) {
        return;
    }

    fb[x + (y / 8) * W] |= ( 1 << ( y % 8 ) );
}

// draws a filled circle by checking every point in the box around it
static void fill_circle ( int x, int y, int r ) {
    for ( int dy = -r; dy <= r; ++dy ) {
        for ( int dx = -r; dx <= r; ++dx ) {
            if ( dx * dx + dy * dy <= r * r ) {
                set_pixel(x + dx, y + dy);
            }
        }
    }
}

// draws a filled rectangle
static void fill_rect ( int x, int y, int w, int h ) {
    for ( int j = y; j < y + h; ++j ) {
        for ( int i = x; i < x + w; ++i ) {
            set_pixel( i, j );
        }
    }
}

// sends the whole frame buffer to the screen
static void oled_flush () {
    // write to the whole screen, every column and all 8 pages
    run_dev_cmd( oled, SET_COLUMN_ADDRESS ); run_dev_cmd( oled, 0 ); run_dev_cmd( oled, W - 1 );
    run_dev_cmd( oled, SET_PAGE_ADDRESS ); run_dev_cmd( oled, 0 ); run_dev_cmd( oled, 7 );
    
    // 0x40 in front means pixel data
    static uint8_t buf[1 + W];
    buf[0] = 0x40;

    // send it one page at a time
    for ( int i = 0; i < 8; ++i ) {
        memcpy( &buf[1], &fb[i * W], W );
        i2c_master_transmit( oled, buf, sizeof(buf), 100 );
    }
}

// wipes the frame buffer, doesn't touch the screen until the next flush
static void clear () {
    memset( fb, 0, sizeof(fb) );
}

// draws two round eyes when open, or two flat lines when closed
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

// keeps the eyes open and blinks them every 3 seconds
static void blinking_eyes () {
    static int64_t next_blink_microseconds = 0;
    int64_t now = esp_timer_get_time();

    if ( now >= next_blink_microseconds ) {
        draw_eyes(false);
        vTaskDelay(pdMS_TO_TICKS(150));
        next_blink_microseconds = now + 3'000'000;
    }

    draw_eyes(true);
}

// shows two bars, top for left and bottom for right
static void audio_display ( int left_value, int right_value ) {
    clear();

    fill_rect(28, 15, left_value, 3);
    fill_rect(28, 45, right_value, 3);

    oled_flush();
}
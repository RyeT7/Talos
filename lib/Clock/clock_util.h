#pragma once

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

// Jakarta time (UTC+7), and anything before 2025 means SNTP hasn't synced yet
#define CLOCK_TZ "WIB-7"
#define CLOCK_NTP_SERVER "pool.ntp.org"
#define CLOCK_MIN_VALID_YEAR 2025

// only start SNTP once, the IP event fires again on every reconnect
static bool clock_sntp_started = false;

// starts SNTP the first time we get an IP
static void clock_on_ip ( void* arg, esp_event_base_t base, int32_t id, void* data ) {
    if ( clock_sntp_started ) {
        return;
    }

    clock_sntp_started = true;

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG( CLOCK_NTP_SERVER );
    esp_netif_sntp_init( &cfg );
}

// sets the timezone and waits for wifi to start SNTP
static void clock_cfg () {
    setenv( "TZ", CLOCK_TZ, 1 );
    tzset();

    ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, clock_on_ip, NULL ) );
}

// local time right now, false if the clock isn't synced yet
static bool clock_now ( struct tm* out ) {
    time_t now = time( NULL );
    localtime_r( &now, out );

    return out->tm_year + 1900 >= CLOCK_MIN_VALID_YEAR;
}

// current hour, or -1 if the clock isn't synced yet
static int clock_hour () {
    struct tm tm;

    return clock_now( &tm ) ? tm.tm_hour : -1;
}

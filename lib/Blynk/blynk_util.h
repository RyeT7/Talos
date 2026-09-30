#include "blynk_config.h"

#include <stdio.h>

static bool blynk_connected () {
    return mqtt_ok;
}

static void blynk_send_str ( const char* datastream, const char* value ) {
    if ( !mqtt_ok ) {
        return;
    }

    char topic[BLYNK_TOPIC_LEN];
    snprintf( topic, sizeof( topic ), BLYNK_UPLINK_DS_PREFIX "%s", datastream );

    esp_mqtt_client_publish( blynk_client, topic, value, 0, 0, 0 );
}

static void blynk_send_float ( const char* datastream, float value ) {
    char payload[BLYNK_VALUE_LEN];
    snprintf( payload, sizeof( payload ), "%.2f", value );

    blynk_send_str( datastream, payload );
}

static void blynk_send_int ( const char* datastream, int value ) {
    char payload[BLYNK_VALUE_LEN];
    snprintf( payload, sizeof( payload ), "%d", value );

    blynk_send_str( datastream, payload );
}

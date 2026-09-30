#pragma once

#include "secrets.h"

#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"

#include <stdbool.h>

#define BLYNK_BROKER_URI "mqtts://blynk.cloud:8883"
#define BLYNK_USERNAME "device"
#define BLYNK_KEEPALIVE_S 45

#define BLYNK_DOWNLINK_TOPIC "downlink/#"
#define BLYNK_DOWNLINK_DS_PREFIX "downlink/ds/"
#define BLYNK_UPLINK_DS_PREFIX "ds/"

#define BLYNK_TOPIC_LEN 96
#define BLYNK_VALUE_LEN 64

#define BLYNK_DS_TEMPERATURE "Temperature"
#define BLYNK_DS_HUMIDITY "Humidity"
#define BLYNK_DS_AUDIO_LEVEL "Noise Level"

static const char* BLYNK_TAG = "Blynk";

typedef void ( *blynk_downlink_cb_t )( const char* datastream, const char* value );

static esp_mqtt_client_handle_t blynk_client = NULL;
static volatile bool mqtt_ok = false;
static blynk_downlink_cb_t downlink_cb = NULL;

wifi_config_t blynk_wifi_cfg = {
    .sta = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    },
};

esp_mqtt_client_config_t blynk_mqtt_cfg = {
    .broker.address.uri = BLYNK_BROKER_URI,
    .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    .credentials.username = BLYNK_USERNAME,
    .credentials.authentication.password = BLYNK_AUTH_TOKEN,
    .session.keepalive = BLYNK_KEEPALIVE_S,
};

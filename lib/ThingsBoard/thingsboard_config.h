#pragma once

#include "thingsboard_secrets.h"

#include "mqtt_client.h"
#include "esp_crt_bundle.h"

// ThingsBoard Cloud over TLS, the username is the device access token
#define TB_BROKER_URI "mqtts://eu.thingsboard.cloud:8883"
#define TB_KEEPALIVE_S 60
// the sensor telemetry goes out every 8 rows (16 seconds), the AC state at least every 150 rows (5 minutes)
#define TB_ROWS_PER_PUBLISH 8
#define TB_ACTUATOR_HEARTBEAT_ROWS 150

// device topics, RPCs come in on request/<id> and we answer on response/<id>
#define TB_TELEMETRY_TOPIC "v1/devices/me/telemetry"
#define TB_ATTRIBUTES_TOPIC "v1/devices/me/attributes"
#define TB_RPC_REQUEST_SUB "v1/devices/me/rpc/request/+"
#define TB_RPC_REQUEST_PREFIX "v1/devices/me/rpc/request/"
#define TB_RPC_RESPONSE_PREFIX "v1/devices/me/rpc/response/"

// RPC method names, these have to match the switch widgets on the dashboard
#define TB_RPC_GET_AUTOMATION "getAutomation"
#define TB_RPC_SET_AUTOMATION "setAutomation"
#define TB_RPC_GET_USE_MODEL "getUseModel"
#define TB_RPC_SET_USE_MODEL "setUseModel"

// buffer sizes for topic names and payloads
#define TB_TOPIC_LEN 96
#define TB_PAYLOAD_LEN 192

// tag for the ESP logs
static const char* TB_TAG = "ThingsBoard";

// MQTT config, uses the built in cert bundle so TLS just works
static esp_mqtt_client_config_t tb_mqtt_cfg = {
    .broker.address.uri = TB_BROKER_URI,
    .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    .credentials.username = THINGSBOARD_ACCESS_TOKEN,
    .session.keepalive = TB_KEEPALIVE_S,
};

#pragma once

// the mochi broker running in the Go server on the laptop
#define ADDRESS "mqtt://192.168.0.102:4092"

// readings go up, status is the online/offline will, predictions come back down from the model service
#define DB_CLIENT_ID "talos"
#define DB_TOPIC "talos/readings"
#define DB_STATUS_TOPIC "talos/status"
#define DB_PREDICTION_TOPIC "talos/predictions"
// QoS 1 so readings aren't lost, and keep pinging every 30 seconds
#define DB_PREDICTION_LEN 256
#define DB_SETTING_LEN 8
#define DB_QOS 1
// rows waiting while the broker is down (300 rows = 10 minutes), and the cap on the MQTT outbox
#define DB_KEEPALIVE_S 30
#define DB_BUFFER_ROWS 300
#define DB_OUTBOX_MAX 8'192
// size of one row as JSON
#define DB_ROW_JSON_LEN 288
// how often the uploader task tries to flush the buffer, and its stack size
#define DB_RETRY_MS 2'000
#define DB_TASK_STACK 4'096

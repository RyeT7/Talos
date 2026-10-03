#include "blynk_config.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static int get_min ( int a, int b ) {
    return a < b ? a : b;
}

// handles everything that happens on the MQTT side
static void mqtt_event_handler (
    void* arg,
    esp_event_base_t base,
    int32_t id,
    void* data
) {
    esp_mqtt_event_handle_t event = data;

    switch ( ( esp_mqtt_event_id_t ) id ) {
    // connected, so subscribe to downlink so we get stuff from the dashboard
    case MQTT_EVENT_CONNECTED:
        mqtt_ok = true;
        ESP_LOGI(BLYNK_TAG, "Connected to Blynk");
        esp_mqtt_client_subscribe( blynk_client, BLYNK_DOWNLINK_TOPIC, 0 );
        break;

    // lost the connection, the client will try to reconnect by itself
    case MQTT_EVENT_DISCONNECTED:
        mqtt_ok = false;
        ESP_LOGW(BLYNK_TAG, "Disconnected from Blynk");
        printf("Blynk: disconnected\n");
        break;

    // got a message from the dashboard
    case MQTT_EVENT_DATA: {
        char topic[BLYNK_TOPIC_LEN];
        char value[BLYNK_VALUE_LEN];

        // topic and data aren't null terminated so copy them out, and cut them off if they're too long
        int t_len = get_min( event->topic_len, sizeof( topic ) - 1 );
        int v_len = get_min( event->data_len, sizeof( value ) - 1 );

        memcpy( topic, event->topic, t_len );
        topic[t_len] = '\0';

        memcpy( value, event->data, v_len );
        value[v_len] = '\0';

        const char* prefix = BLYNK_DOWNLINK_DS_PREFIX;

        // only care about datastream messages, strip the prefix so the callback just gets the name
        if (
            downlink_cb &&
            ( strncmp( topic, prefix, strlen( prefix ) ) == 0 )
        ) {
            downlink_cb( topic + strlen( prefix ), value );
        }

        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(BLYNK_TAG, "MQTT error");
        break;

    default:
        break;
    }
}

// starts the MQTT client, only once though
static void mqtt_start () {
    if ( blynk_client ) {
        return;
    }

    blynk_client = esp_mqtt_client_init( &blynk_mqtt_cfg );

    esp_mqtt_client_register_event( blynk_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL );

    esp_mqtt_client_start( blynk_client );
}

// handles wifi events, connects, keeps reconnecting, and starts MQTT once we get an IP
static void wifi_handler (
    void* arg,
    esp_event_base_t base,
    int32_t id,
    void* data
) {
    // wifi just started so try to connect
    if (
        base == WIFI_EVENT &&
        id == WIFI_EVENT_STA_START
    ) {
        ESP_LOGI("WIFI", "Yooooo, I'm connected");
        esp_wifi_connect();
    // if disconnected, just try again
    } else if (
        base == WIFI_EVENT &&
        id == WIFI_EVENT_STA_DISCONNECTED
    ) {
        wifi_event_sta_disconnected_t* dc_event = data;
        ESP_LOGW("WIFI", "Yooooo, I disconnected, reconnecting: %d", dc_event->reason);
        esp_wifi_connect();
    // got an IP so now we can actually talk to Blynk
    } else if ( base == IP_EVENT && id == IP_EVENT_STA_GOT_IP ) {
        ip_event_got_ip_t* ip_event = data;
        ESP_LOGI("WIFI", "Yooo, I got an IP: " IPSTR, IP2STR(&ip_event->ip_info.ip));

        mqtt_start();
    }
}

// wifi needs NVS, if it's full or outdated just wipe it and init again
static void nvs_cfg () {
    esp_err_t err = nvs_flash_init();

    if ( err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND ) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK( err );
}

// sets up wifi, MQTT starts later by itself once we get an IP
static void blynk_cfg ( blynk_downlink_cb_t on_downlink ) {
    downlink_cb = on_downlink;

    nvs_cfg();

    // network stack and the default event loop
    ESP_ERROR_CHECK( esp_netif_init() );
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init( &wifi_init_cfg ) );

    // hook up the wifi and IP events to our handler
    ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL ) );
    ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL ) );

    // station mode with our creds, then start it
    ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_STA ) );
    ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_STA, &blynk_wifi_cfg ) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

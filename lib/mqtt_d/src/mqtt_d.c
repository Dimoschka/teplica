
#define MQTTD_MAX_TOPICS 8   // Максимальное количество топиков

#include <stdbool.h>
#include <stddef.h>   // size_t
#include <string.h>   // memcpy, strcmp, strlen
#include <stdlib.h>   // malloc, free
#include <stdio.h>    // snprintf
#include "mqtt_d.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>



static const char *TAG = "mqtt_d";

static esp_mqtt_client_handle_t mqtt_client = NULL;

// Структура входящего топика
typedef struct {
    char* topic;
    mqttd_receive_cb_t cb;
} mqttd_rx_topic_t;

// Структура исходящего топика
typedef struct {
    char* topic;
    mqttd_tx_type_t type;
} mqttd_tx_topic_t;

static mqttd_rx_topic_t rx_topics[MQTTD_MAX_TOPICS];
static int rx_topics_count = 0;
static mqttd_tx_topic_t tx_topics[MQTTD_MAX_TOPICS];
static int tx_topics_count = 0;
static mqttd_config_t current_cfg = {0};

// Вспомогательные функции
// Безопасное дублирование строки
static char* safe_strdup(const char* src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char* dst = (char*)malloc(len + 1);
    if (dst) {
        memcpy(dst, src, len + 1);
    }
    return dst;
}

// Проверка существования топика
static bool topic_exists_rx(const char* topic) {
    for (int i = 0; i < rx_topics_count; i++) {
        if (strcmp(rx_topics[i].topic, topic) == 0) return true;
    }
    return false;
}

//  Проверка существования топика
static bool topic_exists_tx(const char* topic) {
    for (int i = 0; i < tx_topics_count; i++) {
        if (strcmp(tx_topics[i].topic, topic) == 0) return true;
    }
    return false;
}

// Инициализация MQTT с конфигурацией
bool mqttd_init(const mqttd_config_t* cfg)
{
    if (!cfg || !cfg->server) return false;
    current_cfg = *cfg;
    return true;
}






// Добавить исходящий топик
bool mqttd_add_rx_topic(const char* topic, mqttd_receive_cb_t cb) {
    if (!topic || rx_topics_count >= MQTTD_MAX_TOPICS || topic_exists_rx(topic)) return false;
    char* dup = safe_strdup(topic);
    if (!dup) {
        ESP_LOGE(TAG, "Failed to allocate memory for RX topic: %s", topic);
        return false;
    }
    rx_topics[rx_topics_count].topic = dup;
    rx_topics[rx_topics_count].cb = cb;
    rx_topics_count++;
    return true;
}

// Добавить входящий топик
bool mqttd_add_tx_topic(const char* topic) {
    if (!topic || tx_topics_count >= MQTTD_MAX_TOPICS || topic_exists_tx(topic)) return false;
    char* dup = safe_strdup(topic);
    if (!dup) {
        ESP_LOGE(TAG, "Failed to allocate memory for TX topic: %s", topic);
        return false;
    }
    tx_topics[tx_topics_count].topic = dup;
    tx_topics_count++;
    return true;
}



// Добавить исходящий топик с типом
bool mqttd_add_tx_topic_ex(const char* topic, mqttd_tx_type_t type) {
    if (!topic || tx_topics_count >= MQTTD_MAX_TOPICS || topic_exists_tx(topic)) return false;
    char* dup = safe_strdup(topic);
    if (!dup) {
        ESP_LOGE(TAG, "Failed to allocate memory for TX topic: %s", topic);
        return false;
    }
    tx_topics[tx_topics_count].topic = dup;
    tx_topics_count++;
    return true;
}

// Обработка входящих сообщений
static void handle_incoming(esp_mqtt_event_handle_t event)
{
    if (!event || event->topic_len == 0) return;
    char topic_buf[128];
    int tlen = event->topic_len < 127 ? event->topic_len : 127;
    memcpy(topic_buf, event->topic, tlen);
    topic_buf[tlen] = '\0';
    for (int i = 0; i < rx_topics_count; ++i) {
        if (strcmp(rx_topics[i].topic, topic_buf) == 0) {
            if (rx_topics[i].cb) {
                char* data_str = (char*)malloc(event->data_len + 1);
                if (data_str) {
                    memcpy(data_str, event->data, event->data_len);
                    data_str[event->data_len] = '\0';
                    rx_topics[i].cb(topic_buf, data_str, event->data_len);
                    free(data_str);
                }
            }
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to broker");
            for (int i = 0; i < tx_topics_count; ++i) {

                // esp_mqtt_client_publish(client, tx_topics[i].topic, "online", 0, 1, 1);
            }
            for (int i = 0; i < rx_topics_count; ++i) {
                esp_mqtt_client_subscribe(mqtt_client, rx_topics[i].topic, 0);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from broker");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Incoming message: topic=%.*s, data=%.*s", event->topic_len, event->topic, event->data_len, event->data);
            handle_incoming(event);
            break;
        default:
            break;
    }
}

bool mqttd_start(void)
{
    if (mqtt_client) return true; // already started
    if (!current_cfg.server) {
        ESP_LOGE(TAG, "MQTT config not initialized");
        return false;
    }

    esp_mqtt_client_config_t mqttCfg = {0};
    mqttCfg.broker.address.hostname = current_cfg.server;
    mqttCfg.broker.address.port = current_cfg.port;
    mqttCfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    mqttCfg.credentials.username = current_cfg.user;
    mqttCfg.credentials.authentication.password = current_cfg.pass;
    mqttCfg.credentials.client_id = current_cfg.client_id;
    mqttCfg.network.disable_auto_reconnect = false;
    mqttCfg.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    mqttCfg.session.keepalive = 120;

    // Если есть хотя бы один исходящий топик, используем его для last_will
    if (tx_topics_count > 0) {
    mqttCfg.session.last_will.topic = tx_topics[0].topic;
    const char* will_msg = "offline";
    mqttCfg.session.last_will.msg = will_msg;
    mqttCfg.session.last_will.msg_len = strlen(will_msg);
    mqttCfg.session.last_will.qos = 1;
    mqttCfg.session.last_will.retain = true;
    }


    mqtt_client = esp_mqtt_client_init(&mqttCfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return false;
    }

    esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client, err=%d", err);
        return false;
    }
    ESP_LOGI(TAG, "MQTT client started");
    return true;
}

void mqttd_stop(void)
{
    if (!mqtt_client) return;
    esp_mqtt_client_stop(mqtt_client);
    esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
    // Очистка всех топиков
    for (int i = 0; i < tx_topics_count; ++i) {
        if (tx_topics[i].topic) { free(tx_topics[i].topic); tx_topics[i].topic = NULL; }
    }
    tx_topics_count = 0;
    for (int i = 0; i < rx_topics_count; ++i) {
        if (rx_topics[i].topic) { free(rx_topics[i].topic); rx_topics[i].topic = NULL; }
        rx_topics[i].cb = NULL;
    }
    rx_topics_count = 0;
    memset(&current_cfg, 0, sizeof(current_cfg));
    ESP_LOGI(TAG, "MQTT client stopped");
}


// Публикаация по типу
 void mqttd_publish_type(const char* topic, mqttd_tx_type_t type, const void* value)
 {
     if (!mqtt_client || !topic || !value) return;
     int found = 0;
     for (int i = 0; i < tx_topics_count; ++i) {
         if (strcmp(tx_topics[i].topic, topic) == 0) { found = 1; break; }
     }
     if (!found) return;
     char buf[32]; // Буфер для форматирования значения
     switch (type) {
         case TYPE_INT:
             snprintf(buf, sizeof(buf), "%d", *(int*)value);
             // mqttd_publish_int(topic, *(int*)value);
             break;
         case TYPE_FLOAT:
             snprintf(buf, sizeof(buf), "%.2f", *(float*)value);
             // mqttd_publish_float(topic, *(float*)value);
             break;
         case TYPE_CHAR:
             snprintf(buf, sizeof(buf), "%s", (const char*)value);
             // mqttd_publish_str(topic, (const char*)value);
             break;
         default:
             return;
     }
     esp_mqtt_client_publish(mqtt_client, topic, buf, 0, 1, 0);
 }

// Публикация по типу
/*bool mqttd_publish_status(const char* topic, bool online)
{
    if (!mqtt_client || !topic) return false;
    int found = 0;
    for (int i = 0; i < tx_topics_count; ++i) {
        if (strcmp(tx_topics[i].topic, topic) == 0) { found = 1; break; }
    }
    if (!found) return false;
    const char* msg = online ? "online" : "offline";
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, msg, 0, 1, 1);
    return msg_id >= 0;
}


// Публикация режима
bool mqttd_publish_mode(const char* topic, bool on)
{
    if (!mqtt_client || !topic) return false;
    int found = 0;
    for (int i = 0; i < tx_topics_count; ++i) {
        if (strcmp(tx_topics[i].topic, topic) == 0) { found = 1; break; }
    }
    if (!found) return false;
    const char* msg = on ? "on" : "off";
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, msg, 0, 1, 0);
    return msg_id >= 0;
}*/

// Публикация строкового значения
bool mqttd_publish_str(const char* topic, const char* str)
{
    if (!mqtt_client || !topic || !str) return false;
    int found = 0;
    for (int i = 0; i < tx_topics_count; ++i) {
        if (strcmp(tx_topics[i].topic, topic) == 0) { found = 1; break; }
    }
    if (!found) return false;
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, str, 0, 1, 0);
    return msg_id >= 0;
}




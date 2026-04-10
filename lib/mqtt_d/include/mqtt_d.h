// mqtt_d.h - simple MQTT wrapper library
#ifndef MQTT_D_H
#define MQTT_D_H

#include <stdbool.h>

typedef void (*mqttd_receive_cb_t)(const char* topic, const char* data, int data_len);

// Регистрация топика для подписки (входящий)
bool mqttd_add_rx_topic(const char* topic, mqttd_receive_cb_t cb);

// Типы публикации исходящих топиков

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_CHAR
} mqttd_tx_type_t;



// Структура исходящего топика
typedef struct {
    char* topic;
    mqttd_tx_type_t type;
} mqttd_tx_topic_ex_t;

void mqttd_publish_type(const char* topic, mqttd_tx_type_t type, const void* value);

/*// Публикация строкового значения
bool mqttd_publish_str(const char* topic, const char* str);
// Публикация по типу
bool mqttd_publish_status(const char* topic, bool online);
bool mqttd_publish_mode(const char* topic, bool on);
bool mqttd_publish_int(const char* topic, int value);
bool mqttd_publish_float(const char* topic, float value);*/

typedef struct {
    const char* server;
    int port;
    const char* user;
    const char* pass;
    const char* client_id;
} mqttd_config_t;

bool mqttd_init(const mqttd_config_t* cfg);
// Установка топиков
bool mqttd_add_tx_topic(const char* tx_topic);
bool mqttd_add_rx_topic(const char* rx_topic, mqttd_receive_cb_t cb);
// Добавить исходящий топик с типом
bool mqttd_add_tx_topic_ex(const char* topic, mqttd_tx_type_t type);
bool mqttd_start(void);
void mqttd_stop(void);


#endif // MQTT_D_H

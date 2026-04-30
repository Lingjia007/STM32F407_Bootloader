#ifndef PLATFORM_MQTT_H
#define PLATFORM_MQTT_H

#include <stdint.h>
#include <stddef.h>

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#define PLATFORM_MQTT_MAX_TOPIC_LEN 128
#define PLATFORM_MQTT_MAX_PAYLOAD_LEN 512
#define PLATFORM_MQTT_MAX_CLIENT_ID_LEN 64
#define PLATFORM_MQTT_MAX_USERNAME_LEN 256
#define PLATFORM_MQTT_MAX_PASSWORD_LEN 256

typedef enum
{
    PLATFORM_MQTT_OK = 0,
    PLATFORM_MQTT_ERROR,
    PLATFORM_MQTT_TIMEOUT,
    PLATFORM_MQTT_INVALID_PARAM,
    PLATFORM_MQTT_NOT_CONNECTED
} platform_mqtt_status_t;

typedef enum
{
    PLATFORM_MQTT_VALUE_INT = 0,
    PLATFORM_MQTT_VALUE_FLOAT,
    PLATFORM_MQTT_VALUE_BOOL,
    PLATFORM_MQTT_VALUE_STRING
} platform_mqtt_value_type_t;

typedef struct
{
    char client_id[PLATFORM_MQTT_MAX_CLIENT_ID_LEN];
    char username[PLATFORM_MQTT_MAX_USERNAME_LEN];
    char password[PLATFORM_MQTT_MAX_PASSWORD_LEN];
} platform_mqtt_user_config_t;

typedef struct
{
    char id[32];
    char key[64];
    int value_int;
    float value_float;
    uint8_t value_type;
} platform_mqtt_property_t;

typedef struct
{
    int16_t (*usercfg)(void *ctx, uint8_t link_id, const platform_mqtt_user_config_t *config);
    int16_t (*connect)(void *ctx, uint8_t link_id, const char *host, uint16_t port, uint8_t reconnect);
    int16_t (*disconnect)(void *ctx, uint8_t link_id);
    int16_t (*subscribe)(void *ctx, uint8_t link_id, const char *topic, uint8_t qos);
    int16_t (*unsubscribe)(void *ctx, uint8_t link_id, const char *topic);
    int16_t (*publish_raw)(void *ctx, uint8_t link_id, const char *topic, const char *payload, uint16_t payload_len, uint8_t qos, uint8_t retain);
    int16_t (*publish_property)(void *ctx, uint8_t link_id, const char *product_id, const char *device_name, const platform_mqtt_property_t *props, uint8_t prop_count, const char *msg_id);
    int16_t (*publish_set_reply)(void *ctx, uint8_t link_id, const char *product_id, const char *device_name, const char *msg_id, int code, const char *msg);
    int16_t (*check_connected)(void *ctx, uint8_t link_id);
    int16_t (*recv_property_set)(void *ctx, char *topic, char *payload, uint16_t max_payload_len, char *msg_id, platform_mqtt_property_t *props, uint8_t max_props, uint8_t *prop_count);
    int16_t (*check_property_set_recv)(void *ctx, char *topic, char *payload, uint16_t max_payload_len, char *msg_id);
} platform_mqtt_ops_t;

typedef struct
{
    const platform_mqtt_ops_t *ops;
    const char *name;
    void *user_data;
} platform_mqtt_base_t;

#define MQTT_USERCFG(mqtt, link_id, config) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->usercfg ? (mqtt)->ops->usercfg((mqtt), (link_id), (config)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_CONNECT(mqtt, link_id, host, port, reconnect) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->connect ? (mqtt)->ops->connect((mqtt), (link_id), (host), (port), (reconnect)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_DISCONNECT(mqtt, link_id) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->disconnect ? (mqtt)->ops->disconnect((mqtt), (link_id)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_SUBSCRIBE(mqtt, link_id, topic, qos) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->subscribe ? (mqtt)->ops->subscribe((mqtt), (link_id), (topic), (qos)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_UNSUBSCRIBE(mqtt, link_id, topic) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->unsubscribe ? (mqtt)->ops->unsubscribe((mqtt), (link_id), (topic)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_PUBLISH_RAW(mqtt, link_id, topic, payload, len, qos, retain) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->publish_raw ? (mqtt)->ops->publish_raw((mqtt), (link_id), (topic), (payload), (len), (qos), (retain)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_PUBLISH_PROPERTY(mqtt, link_id, pid, dn, props, count, mid) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->publish_property ? (mqtt)->ops->publish_property((mqtt), (link_id), (pid), (dn), (props), (count), (mid)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_PUBLISH_SET_REPLY(mqtt, link_id, pid, dn, mid, code, msg) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->publish_set_reply ? (mqtt)->ops->publish_set_reply((mqtt), (link_id), (pid), (dn), (mid), (code), (msg)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_CHECK_CONNECTED(mqtt, link_id) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->check_connected ? (mqtt)->ops->check_connected((mqtt), (link_id)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_RECV_PROPERTY_SET(mqtt, topic, payload, max_len, msg_id, props, max_props, count) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->recv_property_set ? (mqtt)->ops->recv_property_set((mqtt), (topic), (payload), (max_len), (msg_id), (props), (max_props), (count)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_CHECK_PROPERTY_SET_RECV(mqtt, topic, payload, max_len, msg_id) \
    ((mqtt) && (mqtt)->ops && (mqtt)->ops->check_property_set_recv ? (mqtt)->ops->check_property_set_recv((mqtt), (topic), (payload), (max_len), (msg_id)) : (int16_t)PLATFORM_MQTT_ERROR)

#define MQTT_INIT_BASE(mqtt_ptr, ops_ptr, mqtt_name) \
    do \
    { \
        (mqtt_ptr)->ops = (ops_ptr); \
        (mqtt_ptr)->name = (mqtt_name); \
        (mqtt_ptr)->user_data = NULL; \
    } while (0)

#endif

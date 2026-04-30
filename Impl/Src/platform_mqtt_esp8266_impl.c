#include "platform_mqtt_esp8266_impl.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static char g_mqtt_cmd[512];
static char g_mqtt_payload[PLATFORM_MQTT_MAX_PAYLOAD_LEN];

static int16_t esp8266_mqtt_usercfg(void *ctx, uint8_t link_id, const platform_mqtt_user_config_t *config)
{
    mqtt_esp8266_t *self = container_of(ctx, mqtt_esp8266_t, base);
    if (config == NULL)
        return PLATFORM_MQTT_INVALID_PARAM;

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTUSERCFG=%d,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
             link_id, config->client_id, config->username, config->password);

    return WIFI_SEND_AT_CMD(&self->wifi->base, g_mqtt_cmd, "OK", 2000);
}

static int16_t esp8266_mqtt_connect(void *ctx, uint8_t link_id, const char *host, uint16_t port, uint8_t reconnect)
{
    mqtt_esp8266_t *self = container_of(ctx, mqtt_esp8266_t, base);
    if (host == NULL)
        return PLATFORM_MQTT_INVALID_PARAM;

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTCONN=%d,\"%s\",%u,%d",
             link_id, host, port, reconnect ? 1 : 0);

    return WIFI_SEND_AT_CMD(&self->wifi->base, g_mqtt_cmd, "OK", 5000);
}

static int16_t esp8266_mqtt_disconnect(void *ctx, uint8_t link_id)
{
    mqtt_esp8266_t *self = container_of(ctx, mqtt_esp8266_t, base);
    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd), "AT+MQTTCLEAN=%d", link_id);
    return WIFI_SEND_AT_CMD(&self->wifi->base, g_mqtt_cmd, "OK", 2000);
}

static int16_t esp8266_mqtt_subscribe(void *ctx, uint8_t link_id, const char *topic, uint8_t qos)
{
    mqtt_esp8266_t *self = container_of(ctx, mqtt_esp8266_t, base);
    if (topic == NULL)
        return PLATFORM_MQTT_INVALID_PARAM;

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTSUB=%d,\"%s\",%d", link_id, topic, qos);
    return WIFI_SEND_AT_CMD(&self->wifi->base, g_mqtt_cmd, "OK", 2000);
}

static int16_t esp8266_mqtt_unsubscribe(void *ctx, uint8_t link_id, const char *topic)
{
    mqtt_esp8266_t *self = container_of(ctx, mqtt_esp8266_t, base);
    if (topic == NULL)
        return PLATFORM_MQTT_INVALID_PARAM;

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTUNSUB=%d,\"%s\"", link_id, topic);
    return WIFI_SEND_AT_CMD(&self->wifi->base, g_mqtt_cmd, "OK", 2000);
}

static int16_t esp8266_mqtt_publish_raw(void *ctx, uint8_t link_id, const char *topic, const char *payload, uint16_t payload_len, uint8_t qos, uint8_t retain)
{
    mqtt_esp8266_t *self = container_of(ctx, mqtt_esp8266_t, base);
    if (topic == NULL || payload == NULL || payload_len == 0)
        return PLATFORM_MQTT_INVALID_PARAM;

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTPUBRAW=%d,\"%s\",%u,%d,%d",
             link_id, topic, payload_len, qos, retain);

    int16_t ret = WIFI_SEND_AT_CMD(&self->wifi->base, g_mqtt_cmd, ">", 2000);
    if (ret != PLATFORM_WIFI_OK)
        return PLATFORM_MQTT_ERROR;

    wifi_esp8266_rx_restart(self->wifi);
    wifi_esp8266_uart_printf(self->wifi, "%s", payload);

    uint32_t timeout = 3000;
    uint8_t *resp = NULL;
    while (timeout > 0)
    {
        resp = wifi_esp8266_rx_get_frame(self->wifi);
        if (resp != NULL)
        {
            if (strstr((const char *)resp, "+MQTTPUB:OK") != NULL ||
                strstr((const char *)resp, "OK") != NULL)
            {
                return PLATFORM_MQTT_OK;
            }
            else if (strstr((const char *)resp, "ERROR") != NULL ||
                     strstr((const char *)resp, "FAIL") != NULL)
            {
                return PLATFORM_MQTT_ERROR;
            }
            wifi_esp8266_rx_restart(self->wifi);
        }
        timeout--;
        HAL_Delay(1);
    }

    return PLATFORM_MQTT_TIMEOUT;
}

static int16_t esp8266_mqtt_publish_property(void *ctx, uint8_t link_id, const char *product_id, const char *device_name, const platform_mqtt_property_t *props, uint8_t prop_count, const char *msg_id)
{
    if (product_id == NULL || device_name == NULL || props == NULL || prop_count == 0)
        return PLATFORM_MQTT_INVALID_PARAM;

    char topic[PLATFORM_MQTT_MAX_TOPIC_LEN];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post", product_id, device_name);

    uint16_t offset = 0;
    offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                       "{\"id\":\"%s\",\"params\":{", msg_id ? msg_id : "123456");

    for (uint8_t i = 0; i < prop_count; i++)
    {
        if (i > 0)
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset, ",");

        switch (props[i].value_type)
        {
        case PLATFORM_MQTT_VALUE_INT:
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                               "\"%s\":{\"value\":%d}", props[i].key, props[i].value_int);
            break;
        case PLATFORM_MQTT_VALUE_FLOAT:
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                               "\"%s\":{\"value\":%.2f}", props[i].key, props[i].value_float);
            break;
        case PLATFORM_MQTT_VALUE_BOOL:
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                               "\"%s\":{\"value\":%s}", props[i].key, props[i].value_int ? "true" : "false");
            break;
        case PLATFORM_MQTT_VALUE_STRING:
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                               "\"%s\":{\"value\":\"%s\"}", props[i].key, props[i].id);
            break;
        default:
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                               "\"%s\":{\"value\":%d}", props[i].key, props[i].value_int);
            break;
        }
    }

    offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset, "}}");

    return esp8266_mqtt_publish_raw(ctx, link_id, topic, g_mqtt_payload, strlen(g_mqtt_payload), 0, 0);
}

static int16_t esp8266_mqtt_publish_set_reply(void *ctx, uint8_t link_id, const char *product_id, const char *device_name, const char *msg_id, int code, const char *msg)
{
    if (product_id == NULL || device_name == NULL || msg_id == NULL)
        return PLATFORM_MQTT_INVALID_PARAM;

    char topic[PLATFORM_MQTT_MAX_TOPIC_LEN];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set_reply", product_id, device_name);

    snprintf(g_mqtt_payload, sizeof(g_mqtt_payload),
             "{\"id\":\"%s\",\"code\":%d,\"msg\":\"%s\"}",
             msg_id, code, msg ? msg : "user_succ");

    return esp8266_mqtt_publish_raw(ctx, link_id, topic, g_mqtt_payload, strlen(g_mqtt_payload), 0, 0);
}

static int16_t esp8266_mqtt_check_connected(void *ctx, uint8_t link_id)
{
    mqtt_esp8266_t *self = container_of(ctx, mqtt_esp8266_t, base);

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd), "AT+MQTTCONN?");
    int16_t ret = WIFI_SEND_AT_CMD(&self->wifi->base, g_mqtt_cmd, "OK", 1000);
    if (ret == PLATFORM_WIFI_OK)
    {
        uint8_t *resp = wifi_esp8266_rx_get_frame(self->wifi);
        if (resp != NULL)
        {
            char search_str[16];
            snprintf(search_str, sizeof(search_str), "+MQTTCONN:%d,1", link_id);
            if (strstr((const char *)resp, search_str) != NULL)
            {
                return PLATFORM_MQTT_OK;
            }
        }
    }
    return PLATFORM_MQTT_NOT_CONNECTED;
}

static int16_t esp8266_mqtt_recv_property_set(void *ctx, char *topic, char *payload, uint16_t max_payload_len, char *msg_id, platform_mqtt_property_t *props, uint8_t max_props, uint8_t *prop_count)
{
    mqtt_esp8266_t *self = container_of(ctx, mqtt_esp8266_t, base);
    if (topic == NULL || payload == NULL || props == NULL || prop_count == NULL)
        return PLATFORM_MQTT_INVALID_PARAM;

    uint8_t *resp = wifi_esp8266_rx_get_frame(self->wifi);
    if (resp == NULL)
        return PLATFORM_MQTT_ERROR;

    char *recv_marker = strstr((const char *)resp, "+MQTTSUBRECV:");
    if (recv_marker == NULL)
        return PLATFORM_MQTT_ERROR;

    char *topic_start = strchr(recv_marker, '"');
    if (topic_start == NULL)
        return PLATFORM_MQTT_ERROR;
    topic_start++;
    char *topic_end = strchr(topic_start, '"');
    if (topic_end == NULL)
        return PLATFORM_MQTT_ERROR;

    uint16_t topic_len = topic_end - topic_start;
    if (topic_len >= PLATFORM_MQTT_MAX_TOPIC_LEN)
        topic_len = PLATFORM_MQTT_MAX_TOPIC_LEN - 1;
    strncpy(topic, topic_start, topic_len);
    topic[topic_len] = '\0';

    char *len_start = strchr(topic_end + 1, ',');
    if (len_start == NULL)
        return PLATFORM_MQTT_ERROR;
    len_start++;
    uint16_t payload_len = atoi(len_start);
    if (payload_len == 0 || payload_len > max_payload_len)
        payload_len = max_payload_len - 1;

    char *payload_start = strchr(len_start, ',');
    if (payload_start == NULL)
        return PLATFORM_MQTT_ERROR;
    payload_start++;
    strncpy(payload, payload_start, payload_len);
    payload[payload_len] = '\0';

    if (msg_id != NULL)
    {
        char *id_start = strstr(payload, "\"id\":\"");
        if (id_start != NULL)
        {
            id_start += 6;
            char *id_end = strchr(id_start, '"');
            if (id_end != NULL)
            {
                uint16_t id_len = id_end - id_start;
                if (id_len >= 32)
                    id_len = 31;
                strncpy(msg_id, id_start, id_len);
                msg_id[id_len] = '\0';
            }
        }
    }

    *prop_count = 0;
    return PLATFORM_MQTT_OK;
}

static int16_t esp8266_mqtt_check_property_set_recv(void *ctx, char *topic, char *payload, uint16_t max_payload_len, char *msg_id)
{
    mqtt_esp8266_t *self = container_of(ctx, mqtt_esp8266_t, base);

    uint8_t *resp = wifi_esp8266_rx_get_frame(self->wifi);
    if (resp == NULL)
        return PLATFORM_MQTT_ERROR;

    char *recv_marker = strstr((const char *)resp, "+MQTTSUBRECV:");
    if (recv_marker == NULL)
        return PLATFORM_MQTT_ERROR;

    char *topic_start = strchr(recv_marker, '"');
    if (topic_start == NULL)
        return PLATFORM_MQTT_ERROR;
    topic_start++;
    char *topic_end = strchr(topic_start, '"');
    if (topic_end == NULL)
        return PLATFORM_MQTT_ERROR;

    uint16_t topic_len = topic_end - topic_start;
    if (topic_len >= PLATFORM_MQTT_MAX_TOPIC_LEN)
        topic_len = PLATFORM_MQTT_MAX_TOPIC_LEN - 1;
    strncpy(topic, topic_start, topic_len);
    topic[topic_len] = '\0';

    char *len_start = strchr(topic_end + 1, ',');
    if (len_start == NULL)
        return PLATFORM_MQTT_ERROR;
    len_start++;
    uint16_t payload_len = atoi(len_start);
    if (payload_len == 0 || payload_len > max_payload_len)
        payload_len = max_payload_len - 1;

    char *payload_start = strchr(len_start, ',');
    if (payload_start == NULL)
        return PLATFORM_MQTT_ERROR;
    payload_start++;
    strncpy(payload, payload_start, payload_len);
    payload[payload_len] = '\0';

    if (msg_id != NULL)
    {
        char *id_start = strstr(payload, "\"id\"");
        if (id_start != NULL)
        {
            id_start = strchr(id_start, ':');
            if (id_start != NULL)
            {
                id_start++;
                while (*id_start == ' ' || *id_start == '"')
                    id_start++;
                char *id_end = id_start;
                while (*id_end != '"' && *id_end != ',' && *id_end != '}' && *id_end != '\0')
                    id_end++;
                uint16_t id_len = id_end - id_start;
                if (id_len >= 32)
                    id_len = 31;
                strncpy(msg_id, id_start, id_len);
                msg_id[id_len] = '\0';
            }
        }
    }

    return PLATFORM_MQTT_OK;
}

static const platform_mqtt_ops_t esp8266_mqtt_ops = {
    .usercfg = esp8266_mqtt_usercfg,
    .connect = esp8266_mqtt_connect,
    .disconnect = esp8266_mqtt_disconnect,
    .subscribe = esp8266_mqtt_subscribe,
    .unsubscribe = esp8266_mqtt_unsubscribe,
    .publish_raw = esp8266_mqtt_publish_raw,
    .publish_property = esp8266_mqtt_publish_property,
    .publish_set_reply = esp8266_mqtt_publish_set_reply,
    .check_connected = esp8266_mqtt_check_connected,
    .recv_property_set = esp8266_mqtt_recv_property_set,
    .check_property_set_recv = esp8266_mqtt_check_property_set_recv,
};

void platform_mqtt_esp8266_register(mqtt_esp8266_t *mqtt, wifi_esp8266_t *wifi, const char *name)
{
    if (mqtt == NULL || wifi == NULL)
    {
        return;
    }

    mqtt->wifi = wifi;
    MQTT_INIT_BASE(&mqtt->base, &esp8266_mqtt_ops, name);
}

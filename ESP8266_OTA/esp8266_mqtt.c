#include "esp8266_mqtt.h"
#include "esp8266_driver.h"
#include "esp8266_uart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static char g_mqtt_cmd[512];
static char g_mqtt_payload[MQTT_MAX_PAYLOAD_LEN];

uint8_t esp8266_mqtt_usercfg(uint8_t link_id, mqtt_user_config_t *config)
{
    if (config == NULL)
    {
        return ESP8266_EINVAL;
    }

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTUSERCFG=%d,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
             link_id,
             config->client_id,
             config->username,
             config->password);

    uint8_t ret = esp8266_send_at_cmd(g_mqtt_cmd, "OK", 2000);
    if (ret == ESP8266_EOK)
    {
        printf("MQTT: User config OK\r\n");
        return ESP8266_EOK;
    }
    else
    {
        printf("MQTT: User config failed\r\n");
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_mqtt_connect(uint8_t link_id, const char *host, uint16_t port, uint8_t reconnect)
{
    if (host == NULL)
    {
        return ESP8266_EINVAL;
    }

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTCONN=%d,\"%s\",%u,%d",
             link_id, host, port, reconnect ? 1 : 0);

    uint8_t ret = esp8266_send_at_cmd(g_mqtt_cmd, "OK", 5000);
    if (ret == ESP8266_EOK)
    {
        printf("MQTT: Connected to %s:%u\r\n", host, port);
        return ESP8266_EOK;
    }
    else
    {
        printf("MQTT: Connect failed\r\n");
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_mqtt_disconnect(uint8_t link_id)
{
    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd), "AT+MQTTCLEAN=%d", link_id);

    uint8_t ret = esp8266_send_at_cmd(g_mqtt_cmd, "OK", 2000);
    if (ret == ESP8266_EOK)
    {
        printf("MQTT: Disconnected\r\n");
        return ESP8266_EOK;
    }
    else
    {
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_mqtt_subscribe(uint8_t link_id, const char *topic, uint8_t qos)
{
    if (topic == NULL)
    {
        return ESP8266_EINVAL;
    }

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTSUB=%d,\"%s\",%d",
             link_id, topic, qos);

    uint8_t ret = esp8266_send_at_cmd(g_mqtt_cmd, "OK", 2000);
    if (ret == ESP8266_EOK)
    {
        printf("MQTT: Subscribed to %s\r\n", topic);
        return ESP8266_EOK;
    }
    else
    {
        printf("MQTT: Subscribe failed\r\n");
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_mqtt_unsubscribe(uint8_t link_id, const char *topic)
{
    if (topic == NULL)
    {
        return ESP8266_EINVAL;
    }

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTUNSUB=%d,\"%s\"",
             link_id, topic);

    uint8_t ret = esp8266_send_at_cmd(g_mqtt_cmd, "OK", 2000);
    if (ret == ESP8266_EOK)
    {
        printf("MQTT: Unsubscribed from %s\r\n", topic);
        return ESP8266_EOK;
    }
    else
    {
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_mqtt_publish_raw(uint8_t link_id, const char *topic, const char *payload, uint16_t payload_len, uint8_t qos, uint8_t retain)
{
    if (topic == NULL || payload == NULL || payload_len == 0)
    {
        return ESP8266_EINVAL;
    }

    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd),
             "AT+MQTTPUBRAW=%d,\"%s\",%u,%d,%d",
             link_id, topic, payload_len, qos, retain);

    uint8_t ret = esp8266_send_at_cmd(g_mqtt_cmd, ">", 2000);
    if (ret != ESP8266_EOK)
    {
        printf("MQTT: Publish raw failed (no > prompt)\r\n");
        return ESP8266_ERROR;
    }

    esp8266_uart_rx_restart();

    esp8266_uart_printf("%s", payload);

    uint32_t timeout = 3000;
    uint8_t *resp = NULL;
    while (timeout > 0)
    {
        resp = esp8266_uart_rx_get_frame();
        if (resp != NULL)
        {
            if (strstr((const char *)resp, "+MQTTPUB:OK") != NULL ||
                strstr((const char *)resp, "OK") != NULL)
            {
                printf("MQTT: Published %u bytes to %s\r\n", payload_len, topic);
                return ESP8266_EOK;
            }
            else if (strstr((const char *)resp, "ERROR") != NULL ||
                     strstr((const char *)resp, "FAIL") != NULL)
            {
                printf("MQTT: Publish failed\r\n");
                return ESP8266_ERROR;
            }
            esp8266_uart_rx_restart();
        }
        timeout--;
        HAL_Delay(1);
    }

    printf("MQTT: Publish timeout\r\n");
    return ESP8266_ETIMEOUT;
}

uint8_t esp8266_mqtt_publish_property(uint8_t link_id, const char *product_id, const char *device_name, mqtt_property_t *props, uint8_t prop_count, const char *msg_id)
{
    if (product_id == NULL || device_name == NULL || props == NULL || prop_count == 0)
    {
        return ESP8266_EINVAL;
    }

    char topic[MQTT_MAX_TOPIC_LEN];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post", product_id, device_name);

    uint16_t offset = 0;
    offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                       "{\"id\":\"%s\",\"params\":{", msg_id ? msg_id : "123456");

    for (uint8_t i = 0; i < prop_count; i++)
    {
        if (i > 0)
        {
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset, ",");
        }

        switch (props[i].value_type)
        {
        case MQTT_VALUE_TYPE_INT:
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                               "\"%s\":{\"value\":%d}", props[i].key, props[i].value_int);
            break;
        case MQTT_VALUE_TYPE_FLOAT:
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                               "\"%s\":{\"value\":%.2f}", props[i].key, props[i].value_float);
            break;
        case MQTT_VALUE_TYPE_BOOL:
            offset += snprintf(g_mqtt_payload + offset, sizeof(g_mqtt_payload) - offset,
                               "\"%s\":{\"value\":%s}", props[i].key, props[i].value_int ? "true" : "false");
            break;
        case MQTT_VALUE_TYPE_STRING:
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

    uint16_t actual_len = strlen(g_mqtt_payload);
    printf("MQTT: Property post payload (%d bytes): %s\r\n", actual_len, g_mqtt_payload);

    return esp8266_mqtt_publish_raw(link_id, topic, g_mqtt_payload, actual_len, 0, 0);
}

uint8_t esp8266_mqtt_publish_set_reply(uint8_t link_id, const char *product_id, const char *device_name, const char *msg_id, int code, const char *msg)
{
    if (product_id == NULL || device_name == NULL || msg_id == NULL)
    {
        return ESP8266_EINVAL;
    }

    char topic[MQTT_MAX_TOPIC_LEN];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set_reply", product_id, device_name);

    uint16_t offset = snprintf(g_mqtt_payload, sizeof(g_mqtt_payload),
                               "{\"id\":\"%s\",\"code\":%d,\"msg\":\"%s\"}",
                               msg_id, code, msg ? msg : "user_succ");

    uint16_t actual_len = strlen(g_mqtt_payload);
    printf("MQTT: Set reply payload (%d bytes): %s\r\n", actual_len, g_mqtt_payload);

    return esp8266_mqtt_publish_raw(link_id, topic, g_mqtt_payload, actual_len, 0, 0);
}

uint8_t esp8266_mqtt_check_connected(uint8_t link_id)
{
    snprintf(g_mqtt_cmd, sizeof(g_mqtt_cmd), "AT+MQTTCONN?");

    uint8_t ret = esp8266_send_at_cmd(g_mqtt_cmd, "OK", 1000);
    if (ret == ESP8266_EOK)
    {
        uint8_t *resp = esp8266_uart_rx_get_frame();
        if (resp != NULL)
        {
            char search_str[16];
            snprintf(search_str, sizeof(search_str), "+MQTTCONN:%d,1", link_id);
            if (strstr((const char *)resp, search_str) != NULL)
            {
                return ESP8266_EOK;
            }
        }
    }
    return ESP8266_ERROR;
}

uint8_t esp8266_mqtt_recv_property_set(char *topic, char *payload, uint16_t max_payload_len, char *msg_id, mqtt_property_t *props, uint8_t max_props, uint8_t *prop_count)
{
    if (topic == NULL || payload == NULL || props == NULL || prop_count == NULL)
    {
        return ESP8266_EINVAL;
    }

    uint8_t *resp = esp8266_uart_rx_get_frame();
    if (resp == NULL)
    {
        return ESP8266_ERROR;
    }

    char *recv_marker = strstr((const char *)resp, "+MQTTSUBRECV:");
    if (recv_marker == NULL)
    {
        return ESP8266_ERROR;
    }

    char *topic_start = strchr(recv_marker, '"');
    if (topic_start == NULL)
    {
        return ESP8266_ERROR;
    }
    topic_start++;

    char *topic_end = strchr(topic_start, '"');
    if (topic_end == NULL)
    {
        return ESP8266_ERROR;
    }

    uint16_t topic_len = topic_end - topic_start;
    if (topic_len >= MQTT_MAX_TOPIC_LEN)
    {
        topic_len = MQTT_MAX_TOPIC_LEN - 1;
    }
    strncpy(topic, topic_start, topic_len);
    topic[topic_len] = '\0';

    char *len_start = strchr(topic_end + 1, ',');
    if (len_start == NULL)
    {
        return ESP8266_ERROR;
    }
    len_start++;

    uint16_t payload_len = atoi(len_start);
    if (payload_len == 0 || payload_len > max_payload_len)
    {
        payload_len = max_payload_len - 1;
    }

    char *payload_start = strchr(len_start, ',');
    if (payload_start == NULL)
    {
        return ESP8266_ERROR;
    }
    payload_start++;

    strncpy(payload, payload_start, payload_len);
    payload[payload_len] = '\0';

    printf("MQTT: Received on topic: %s\r\n", topic);
    printf("MQTT: Payload: %s\r\n", payload);

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
                {
                    id_len = 31;
                }
                strncpy(msg_id, id_start, id_len);
                msg_id[id_len] = '\0';
            }
        }
    }

    *prop_count = 0;

    return ESP8266_EOK;
}

uint8_t esp8266_mqtt_check_property_set_recv(char *topic, char *payload, uint16_t max_payload_len, char *msg_id)
{
    uint8_t *resp = esp8266_uart_rx_get_frame();
    if (resp == NULL)
    {
        return ESP8266_ERROR;
    }

    char *recv_marker = strstr((const char *)resp, "+MQTTSUBRECV:");
    if (recv_marker == NULL)
    {
        return ESP8266_ERROR;
    }

    char *topic_start = strchr(recv_marker, '"');
    if (topic_start == NULL)
    {
        return ESP8266_ERROR;
    }
    topic_start++;

    char *topic_end = strchr(topic_start, '"');
    if (topic_end == NULL)
    {
        return ESP8266_ERROR;
    }

    uint16_t topic_len = topic_end - topic_start;
    if (topic_len >= MQTT_MAX_TOPIC_LEN)
    {
        topic_len = MQTT_MAX_TOPIC_LEN - 1;
    }
    strncpy(topic, topic_start, topic_len);
    topic[topic_len] = '\0';

    char *len_start = strchr(topic_end + 1, ',');
    if (len_start == NULL)
    {
        return ESP8266_ERROR;
    }
    len_start++;

    uint16_t payload_len = atoi(len_start);
    if (payload_len == 0 || payload_len > max_payload_len)
    {
        payload_len = max_payload_len - 1;
    }

    char *payload_start = strchr(len_start, ',');
    if (payload_start == NULL)
    {
        return ESP8266_ERROR;
    }
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
                {
                    id_start++;
                }
                char *id_end = id_start;
                while (*id_end != '"' && *id_end != ',' && *id_end != '}' && *id_end != '\0')
                {
                    id_end++;
                }
                uint16_t id_len = id_end - id_start;
                if (id_len >= 32)
                {
                    id_len = 31;
                }
                strncpy(msg_id, id_start, id_len);
                msg_id[id_len] = '\0';
            }
        }
    }

    return ESP8266_EOK;
}

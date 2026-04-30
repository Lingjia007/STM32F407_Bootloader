#ifndef PLATFORM_WIFI_H
#define PLATFORM_WIFI_H

#include <stdint.h>
#include <stddef.h>

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

typedef enum
{
    PLATFORM_WIFI_OK = 0,
    PLATFORM_WIFI_ERROR,
    PLATFORM_WIFI_TIMEOUT,
    PLATFORM_WIFI_INVALID_PARAM,
    PLATFORM_WIFI_NOT_CONNECTED,
    PLATFORM_WIFI_MODE_STA = 1,
    PLATFORM_WIFI_MODE_AP = 2,
    PLATFORM_WIFI_MODE_STA_AP = 3
} platform_wifi_status_t;

typedef struct
{
    int16_t (*init)(void *ctx);
    int16_t (*deinit)(void *ctx);
    int16_t (*at_test)(void *ctx);
    int16_t (*set_mode)(void *ctx, uint8_t mode);
    int16_t (*join_ap)(void *ctx, const char *ssid, const char *pwd);
    int16_t (*get_ip)(void *ctx, char *buf, uint16_t buf_len);
    int16_t (*connect_tcp)(void *ctx, const char *host, uint16_t port);
    int16_t (*disconnect_tcp)(void *ctx);
    int16_t (*enter_transparent)(void *ctx);
    int16_t (*exit_transparent)(void *ctx);
    int16_t (*send_at_cmd)(void *ctx, const char *cmd, const char *ack, uint32_t timeout);
    int16_t (*sw_reset)(void *ctx);
    int16_t (*hw_reset)(void *ctx);
} platform_wifi_ops_t;

typedef struct
{
    const platform_wifi_ops_t *ops;
    const char *name;
    void *user_data;
} platform_wifi_base_t;

#define WIFI_INIT(wifi) \
    ((wifi) && (wifi)->ops && (wifi)->ops->init ? (wifi)->ops->init((wifi)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_DEINIT(wifi) \
    ((wifi) && (wifi)->ops && (wifi)->ops->deinit ? (wifi)->ops->deinit((wifi)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_AT_TEST(wifi) \
    ((wifi) && (wifi)->ops && (wifi)->ops->at_test ? (wifi)->ops->at_test((wifi)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_SET_MODE(wifi, mode) \
    ((wifi) && (wifi)->ops && (wifi)->ops->set_mode ? (wifi)->ops->set_mode((wifi), (mode)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_JOIN_AP(wifi, ssid, pwd) \
    ((wifi) && (wifi)->ops && (wifi)->ops->join_ap ? (wifi)->ops->join_ap((wifi), (ssid), (pwd)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_GET_IP(wifi, buf, len) \
    ((wifi) && (wifi)->ops && (wifi)->ops->get_ip ? (wifi)->ops->get_ip((wifi), (buf), (len)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_CONNECT_TCP(wifi, host, port) \
    ((wifi) && (wifi)->ops && (wifi)->ops->connect_tcp ? (wifi)->ops->connect_tcp((wifi), (host), (port)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_DISCONNECT_TCP(wifi) \
    ((wifi) && (wifi)->ops && (wifi)->ops->disconnect_tcp ? (wifi)->ops->disconnect_tcp((wifi)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_ENTER_TRANSPARENT(wifi) \
    ((wifi) && (wifi)->ops && (wifi)->ops->enter_transparent ? (wifi)->ops->enter_transparent((wifi)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_EXIT_TRANSPARENT(wifi) \
    ((wifi) && (wifi)->ops && (wifi)->ops->exit_transparent ? (wifi)->ops->exit_transparent((wifi)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_SEND_AT_CMD(wifi, cmd, ack, timeout) \
    ((wifi) && (wifi)->ops && (wifi)->ops->send_at_cmd ? (wifi)->ops->send_at_cmd((wifi), (cmd), (ack), (timeout)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_SW_RESET(wifi) \
    ((wifi) && (wifi)->ops && (wifi)->ops->sw_reset ? (wifi)->ops->sw_reset((wifi)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_HW_RESET(wifi) \
    ((wifi) && (wifi)->ops && (wifi)->ops->hw_reset ? (wifi)->ops->hw_reset((wifi)) : (int16_t)PLATFORM_WIFI_ERROR)

#define WIFI_INIT_BASE(wifi_ptr, ops_ptr, wifi_name) \
    do \
    { \
        (wifi_ptr)->ops = (ops_ptr); \
        (wifi_ptr)->name = (wifi_name); \
        (wifi_ptr)->user_data = NULL; \
    } while (0)

#endif

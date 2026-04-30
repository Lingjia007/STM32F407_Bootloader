#ifndef PLATFORM_UART_H
#define PLATFORM_UART_H

#include <stdint.h>
#include <stddef.h>

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

typedef enum {
    UART_STATUS_OK = 0,
    UART_STATUS_ERROR,
    UART_STATUS_BUSY,
    UART_STATUS_TIMEOUT,
    UART_STATUS_PARAM,
} platform_uart_status_t;

typedef enum {
    UART_TYPE_UART = 0,
    UART_TYPE_USART,
    UART_TYPE_LPUART,
    UART_TYPE_UNKNOWN
} platform_uart_type_t;

typedef enum {
    PLATFORM_UART_FLAG_RXNE = 0x01,
    PLATFORM_UART_FLAG_TXE = 0x02,
    PLATFORM_UART_FLAG_TC = 0x04,
    PLATFORM_UART_FLAG_ORE = 0x08,
    PLATFORM_UART_FLAG_IDLE = 0x10,
} platform_uart_flag_t;

typedef enum {
    PLATFORM_UART_IT_RXNE = 0x01,
    PLATFORM_UART_IT_TXE = 0x02,
    PLATFORM_UART_IT_TC = 0x04,
    PLATFORM_UART_IT_IDLE = 0x10,
} platform_uart_it_t;

typedef struct {
    int16_t (*init)(void* ctx);
    int16_t (*deinit)(void* ctx);
    int16_t (*transmit)(void* ctx, const uint8_t* data, uint16_t size, uint32_t timeout);
    int16_t (*receive)(void* ctx, uint8_t* data, uint16_t size, uint32_t timeout);
    int16_t (*transmit_it)(void* ctx, const uint8_t* data, uint16_t size);
    int16_t (*receive_it)(void* ctx, uint8_t* data, uint16_t size);
    int16_t (*flush)(void* ctx);
    int16_t (*abort)(void* ctx);
    uint8_t (*get_flag)(void* ctx, platform_uart_flag_t flag);
    void (*clear_flag)(void* ctx, platform_uart_flag_t flag);
    void (*enable_it)(void* ctx, platform_uart_it_t it);
    void (*disable_it)(void* ctx, platform_uart_it_t it);
    uint8_t (*read_byte)(void* ctx);
    void (*write_byte)(void* ctx, uint8_t data);
} platform_uart_ops_t;

typedef struct {
    const platform_uart_ops_t* ops;
    const char* name;
    platform_uart_type_t type;
    void* user_data;
} platform_uart_base_t;

#define UART_TRANSMIT(uart, data, size, timeout) \
    ((uart) && (uart)->ops && (uart)->ops->transmit ? \
     (uart)->ops->transmit((uart), (data), (size), (timeout)) : (int16_t)UART_STATUS_ERROR)

#define UART_RECEIVE(uart, data, size, timeout) \
    ((uart) && (uart)->ops && (uart)->ops->receive ? \
     (uart)->ops->receive((uart), (data), (size), (timeout)) : (int16_t)UART_STATUS_ERROR)

#define UART_TRANSMIT_IT(uart, data, size) \
    ((uart) && (uart)->ops && (uart)->ops->transmit_it ? \
     (uart)->ops->transmit_it((uart), (data), (size)) : (int16_t)UART_STATUS_ERROR)

#define UART_RECEIVE_IT(uart, data, size) \
    ((uart) && (uart)->ops && (uart)->ops->receive_it ? \
     (uart)->ops->receive_it((uart), (data), (size)) : (int16_t)UART_STATUS_ERROR)

#define UART_FLUSH(uart) \
    ((uart) && (uart)->ops && (uart)->ops->flush ? \
     (uart)->ops->flush((uart)) : (int16_t)UART_STATUS_ERROR)

#define UART_ABORT(uart) \
    ((uart) && (uart)->ops && (uart)->ops->abort ? \
     (uart)->ops->abort((uart)) : (int16_t)UART_STATUS_ERROR)

#define UART_INIT(uart) \
    ((uart) && (uart)->ops && (uart)->ops->init ? \
     (uart)->ops->init((uart)) : (int16_t)UART_STATUS_ERROR)

#define UART_DEINIT(uart) \
    ((uart) && (uart)->ops && (uart)->ops->deinit ? \
     (uart)->ops->deinit((uart)) : (int16_t)UART_STATUS_ERROR)

#define UART_GET_FLAG(uart, flag) \
    ((uart) && (uart)->ops && (uart)->ops->get_flag ? \
     (uart)->ops->get_flag((uart), (flag)) : 0)

#define UART_CLEAR_FLAG(uart, flag) \
    ((uart) && (uart)->ops && (uart)->ops->clear_flag ? \
     (void)(uart)->ops->clear_flag((uart), (flag)) : (void)0)

#define UART_ENABLE_IT(uart, it) \
    ((uart) && (uart)->ops && (uart)->ops->enable_it ? \
     (void)(uart)->ops->enable_it((uart), (it)) : (void)0)

#define UART_DISABLE_IT(uart, it) \
    ((uart) && (uart)->ops && (uart)->ops->disable_it ? \
     (void)(uart)->ops->disable_it((uart), (it)) : (void)0)

#define UART_READ_BYTE(uart) \
    ((uart) && (uart)->ops && (uart)->ops->read_byte ? \
     (uart)->ops->read_byte((uart)) : 0)

#define UART_WRITE_BYTE(uart, data) \
    ((uart) && (uart)->ops && (uart)->ops->write_byte ? \
     (void)(uart)->ops->write_byte((uart), (data)) : (void)0)

#define UART_INIT_BASE(uart_ptr, ops_ptr, uart_name, uart_type) \
    do { \
        (uart_ptr)->ops = (ops_ptr); \
        (uart_ptr)->name = (uart_name); \
        (uart_ptr)->type = (uart_type); \
        (uart_ptr)->user_data = NULL; \
    } while(0)

#endif

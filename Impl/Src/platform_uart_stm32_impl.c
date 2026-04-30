#include "platform_uart_stm32_impl.h"

static int16_t uart_stm32_init(void* ctx)
{
    uart_stm32_t* self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    return (int16_t)UART_STATUS_OK;
}

static int16_t uart_stm32_deinit(void* ctx)
{
    uart_stm32_t* self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    HAL_UART_DeInit(self->huart);
    return (int16_t)UART_STATUS_OK;
}

static int16_t uart_stm32_transmit(void* ctx, const uint8_t* data, uint16_t size, uint32_t timeout)
{
    uart_stm32_t* self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL || data == NULL || size == 0)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    
    HAL_StatusTypeDef status = HAL_UART_Transmit(self->huart, (uint8_t*)data, size, timeout);
    
    switch (status)
    {
        case HAL_OK:
            return (int16_t)UART_STATUS_OK;
        case HAL_BUSY:
            return (int16_t)UART_STATUS_BUSY;
        case HAL_TIMEOUT:
            return (int16_t)UART_STATUS_TIMEOUT;
        default:
            return (int16_t)UART_STATUS_ERROR;
    }
}

static int16_t uart_stm32_receive(void* ctx, uint8_t* data, uint16_t size, uint32_t timeout)
{
    uart_stm32_t* self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL || data == NULL || size == 0)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    
    HAL_StatusTypeDef status = HAL_UART_Receive(self->huart, data, size, timeout);
    
    switch (status)
    {
        case HAL_OK:
            return (int16_t)UART_STATUS_OK;
        case HAL_BUSY:
            return (int16_t)UART_STATUS_BUSY;
        case HAL_TIMEOUT:
            return (int16_t)UART_STATUS_TIMEOUT;
        default:
            return (int16_t)UART_STATUS_ERROR;
    }
}

static int16_t uart_stm32_transmit_it(void* ctx, const uint8_t* data, uint16_t size)
{
    uart_stm32_t* self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL || data == NULL || size == 0)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    
    HAL_StatusTypeDef status = HAL_UART_Transmit_IT(self->huart, (uint8_t*)data, size);
    
    return (status == HAL_OK) ? (int16_t)UART_STATUS_OK : (int16_t)UART_STATUS_ERROR;
}

static int16_t uart_stm32_receive_it(void* ctx, uint8_t* data, uint16_t size)
{
    uart_stm32_t* self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL || data == NULL || size == 0)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    
    HAL_StatusTypeDef status = HAL_UART_Receive_IT(self->huart, data, size);
    
    return (status == HAL_OK) ? (int16_t)UART_STATUS_OK : (int16_t)UART_STATUS_ERROR;
}

static int16_t uart_stm32_flush(void* ctx)
{
    uart_stm32_t* self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    
    __HAL_UART_FLUSH_DRREGISTER(self->huart);
    
    return (int16_t)UART_STATUS_OK;
}

static int16_t uart_stm32_abort(void* ctx)
{
    uart_stm32_t* self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    
    HAL_UART_Abort(self->huart);
    
    return (int16_t)UART_STATUS_OK;
}

static const platform_uart_ops_t uart_stm32_ops = {
    .init = uart_stm32_init,
    .deinit = uart_stm32_deinit,
    .transmit = uart_stm32_transmit,
    .receive = uart_stm32_receive,
    .transmit_it = uart_stm32_transmit_it,
    .receive_it = uart_stm32_receive_it,
    .flush = uart_stm32_flush,
    .abort = uart_stm32_abort,
};

void platform_uart_stm32_register(uart_stm32_t* uart, UART_HandleTypeDef* huart, const char* name)
{
    if (uart == NULL || huart == NULL)
    {
        return;
    }
    
    uart->huart = huart;
    UART_INIT_BASE(&uart->base, &uart_stm32_ops, name, UART_TYPE_UART);
}

uart_stm32_t g_uart4_console = {
    .base = {
        .ops = &uart_stm32_ops,
        .name = "uart4_console",
        .type = UART_TYPE_UART,
        .user_data = NULL,
    },
    .huart = NULL,
};

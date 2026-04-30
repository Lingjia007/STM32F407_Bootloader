#include "platform_uart_stm32_impl.h"

static int16_t uart_stm32_init(void *ctx)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    return (int16_t)UART_STATUS_OK;
}

static int16_t uart_stm32_deinit(void *ctx)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return (int16_t)UART_STATUS_PARAM;
    }
    HAL_UART_DeInit(self->huart);
    return (int16_t)UART_STATUS_OK;
}

static int16_t uart_stm32_transmit(void *ctx, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL || data == NULL || size == 0)
    {
        return (int16_t)UART_STATUS_PARAM;
    }

    HAL_StatusTypeDef status = HAL_UART_Transmit(self->huart, (uint8_t *)data, size, timeout);

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

static int16_t uart_stm32_receive(void *ctx, uint8_t *data, uint16_t size, uint32_t timeout)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
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

static int16_t uart_stm32_transmit_it(void *ctx, const uint8_t *data, uint16_t size)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL || data == NULL || size == 0)
    {
        return (int16_t)UART_STATUS_PARAM;
    }

    HAL_StatusTypeDef status = HAL_UART_Transmit_IT(self->huart, (uint8_t *)data, size);

    return (status == HAL_OK) ? (int16_t)UART_STATUS_OK : (int16_t)UART_STATUS_ERROR;
}

static int16_t uart_stm32_receive_it(void *ctx, uint8_t *data, uint16_t size)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL || data == NULL || size == 0)
    {
        return (int16_t)UART_STATUS_PARAM;
    }

    HAL_StatusTypeDef status = HAL_UART_Receive_IT(self->huart, data, size);

    return (status == HAL_OK) ? (int16_t)UART_STATUS_OK : (int16_t)UART_STATUS_ERROR;
}

static int16_t uart_stm32_flush(void *ctx)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return (int16_t)UART_STATUS_PARAM;
    }

    __HAL_UART_FLUSH_DRREGISTER(self->huart);

    return (int16_t)UART_STATUS_OK;
}

static int16_t uart_stm32_abort(void *ctx)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return (int16_t)UART_STATUS_PARAM;
    }

    HAL_UART_Abort(self->huart);

    return (int16_t)UART_STATUS_OK;
}

static uint8_t uart_stm32_get_flag(void *ctx, platform_uart_flag_t flag)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return 0;
    }

    uint32_t hal_flag = 0;
    switch (flag)
    {
    case PLATFORM_UART_FLAG_RXNE:
        hal_flag = UART_FLAG_RXNE;
        break;
    case PLATFORM_UART_FLAG_TXE:
        hal_flag = UART_FLAG_TXE;
        break;
    case PLATFORM_UART_FLAG_TC:
        hal_flag = UART_FLAG_TC;
        break;
    case PLATFORM_UART_FLAG_ORE:
        hal_flag = UART_FLAG_ORE;
        break;
    case PLATFORM_UART_FLAG_IDLE:
        hal_flag = UART_FLAG_IDLE;
        break;
    default:
        return 0;
    }

    return (__HAL_UART_GET_FLAG(self->huart, hal_flag) != RESET) ? 1 : 0;
}

static void uart_stm32_clear_flag(void *ctx, platform_uart_flag_t flag)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return;
    }

    switch (flag)
    {
    case PLATFORM_UART_FLAG_ORE:
        __HAL_UART_CLEAR_OREFLAG(self->huart);
        break;
    case PLATFORM_UART_FLAG_IDLE:
        __HAL_UART_CLEAR_IDLEFLAG(self->huart);
        break;
    default:
        break;
    }
}

static void uart_stm32_enable_it(void *ctx, platform_uart_it_t it)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return;
    }

    switch (it)
    {
    case PLATFORM_UART_IT_RXNE:
        __HAL_UART_ENABLE_IT(self->huart, UART_IT_RXNE);
        break;
    case PLATFORM_UART_IT_TXE:
        __HAL_UART_ENABLE_IT(self->huart, UART_IT_TXE);
        break;
    case PLATFORM_UART_IT_TC:
        __HAL_UART_ENABLE_IT(self->huart, UART_IT_TC);
        break;
    case PLATFORM_UART_IT_IDLE:
        __HAL_UART_ENABLE_IT(self->huart, UART_IT_IDLE);
        break;
    default:
        break;
    }
}

static void uart_stm32_disable_it(void *ctx, platform_uart_it_t it)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return;
    }

    switch (it)
    {
    case PLATFORM_UART_IT_RXNE:
        __HAL_UART_DISABLE_IT(self->huart, UART_IT_RXNE);
        break;
    case PLATFORM_UART_IT_TXE:
        __HAL_UART_DISABLE_IT(self->huart, UART_IT_TXE);
        break;
    case PLATFORM_UART_IT_TC:
        __HAL_UART_DISABLE_IT(self->huart, UART_IT_TC);
        break;
    case PLATFORM_UART_IT_IDLE:
        __HAL_UART_DISABLE_IT(self->huart, UART_IT_IDLE);
        break;
    default:
        break;
    }
}

static uint8_t uart_stm32_read_byte(void *ctx)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return 0;
    }

    return (uint8_t)(self->huart->Instance->DR & 0xFF);
}

static void uart_stm32_write_byte(void *ctx, uint8_t data)
{
    uart_stm32_t *self = container_of(ctx, uart_stm32_t, base);
    if (self->huart == NULL)
    {
        return;
    }

    self->huart->Instance->DR = data;
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
    .get_flag = uart_stm32_get_flag,
    .clear_flag = uart_stm32_clear_flag,
    .enable_it = uart_stm32_enable_it,
    .disable_it = uart_stm32_disable_it,
    .read_byte = uart_stm32_read_byte,
    .write_byte = uart_stm32_write_byte,
};

void platform_uart_stm32_register(uart_stm32_t *uart, UART_HandleTypeDef *huart, const char *name)
{
    if (uart == NULL || huart == NULL)
    {
        return;
    }

    uart->huart = huart;
    UART_INIT_BASE(&uart->base, &uart_stm32_ops, name, UART_TYPE_UART);
}

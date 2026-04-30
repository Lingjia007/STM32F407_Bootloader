#include "platform_gpio_led_stm32_impl.h"

static void gpio_led_on(void *ctx)
{
    gpio_led_stm32_t *self = container_of(ctx, gpio_led_stm32_t, base);
    HAL_GPIO_WritePin(self->port, self->pin, GPIO_PIN_SET);
}

static void gpio_led_off(void *ctx)
{
    gpio_led_stm32_t *self = container_of(ctx, gpio_led_stm32_t, base);
    HAL_GPIO_WritePin(self->port, self->pin, GPIO_PIN_RESET);
}

static void gpio_led_toggle(void *ctx)
{
    gpio_led_stm32_t *self = container_of(ctx, gpio_led_stm32_t, base);
    HAL_GPIO_TogglePin(self->port, self->pin);
}

static int16_t gpio_led_get_state(void *ctx, platform_led_state_t *state)
{
    gpio_led_stm32_t *self = container_of(ctx, gpio_led_stm32_t, base);
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(self->port, self->pin);
    *state = (pin_state == GPIO_PIN_SET) ? LED_STATE_ON : LED_STATE_OFF;
    return 0;
}

static const platform_led_ops_t gpio_led_ops = {
    .on = gpio_led_on,
    .off = gpio_led_off,
    .toggle = gpio_led_toggle,
    .set_brightness = NULL,
    .set_rgb = NULL,
    .get_state = gpio_led_get_state,
    .get_brightness = NULL,
    .get_rgb = NULL,
};

void platform_gpio_led_stm32_register(gpio_led_stm32_t *led, GPIO_TypeDef *port, uint16_t pin, const char *name)
{
    if (led == NULL || port == NULL)
    {
        return;
    }

    led->port = port;
    led->pin = pin;
    LED_INIT_BASE(&led->base, &gpio_led_ops, name, LED_TYPE_GPIO);
}

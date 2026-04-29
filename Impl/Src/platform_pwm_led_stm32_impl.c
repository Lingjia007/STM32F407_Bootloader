#include "platform_pwm_led_stm32_impl.h"

static void pwm_led_on(void *ctx)
{
    pwm_led_stm32_t *self = container_of(ctx, pwm_led_stm32_t, base);
    __HAL_TIM_SET_COMPARE(self->htim, self->channel, self->htim->Init.Period);
    self->brightness = 100;
}

static void pwm_led_off(void *ctx)
{
    pwm_led_stm32_t *self = container_of(ctx, pwm_led_stm32_t, base);
    __HAL_TIM_SET_COMPARE(self->htim, self->channel, 0);
    self->brightness = 0;
}

static int16_t pwm_led_set_brightness(void *ctx, uint8_t percent)
{
    pwm_led_stm32_t *self = container_of(ctx, pwm_led_stm32_t, base);
    if (percent > 100)
        return -1;
    uint32_t pulse = (self->htim->Init.Period * percent) / 100;
    __HAL_TIM_SET_COMPARE(self->htim, self->channel, pulse);
    self->brightness = percent;
    return 0;
}

static int16_t pwm_led_get_brightness(void *ctx, uint8_t *percent)
{
    pwm_led_stm32_t *self = container_of(ctx, pwm_led_stm32_t, base);
    *percent = self->brightness;
    return 0;
}

static int16_t pwm_led_get_state(void *ctx, platform_led_state_t *state)
{
    pwm_led_stm32_t *self = container_of(ctx, pwm_led_stm32_t, base);
    *state = (self->brightness > 0) ? LED_STATE_ON : LED_STATE_OFF;
    return 0;
}

static const platform_led_ops_t pwm_led_ops = {
    .on = pwm_led_on,
    .off = pwm_led_off,
    .toggle = NULL,
    .set_brightness = pwm_led_set_brightness,
    .set_rgb = NULL,
    .get_state = pwm_led_get_state,
    .get_brightness = pwm_led_get_brightness,
    .get_rgb = NULL,
};

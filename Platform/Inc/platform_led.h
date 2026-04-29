#ifndef PLATFORM_LED_H
#define PLATFORM_LED_H

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
    LED_TYPE_GPIO = 0,
    LED_TYPE_PWM,
    LED_TYPE_RGB,
    LED_TYPE_UNKNOWN
} platform_led_type_t;

typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_ON,
    LED_STATE_TOGGLE
} platform_led_state_t;

typedef struct {
    void (*on)(void* ctx);
    void (*off)(void* ctx);
    void (*toggle)(void* ctx);
    int16_t (*set_brightness)(void* ctx, uint8_t percent);
    int16_t (*set_rgb)(void* ctx, uint8_t r, uint8_t g, uint8_t b);
    int16_t (*get_state)(void* ctx, platform_led_state_t* state);
    int16_t (*get_brightness)(void* ctx, uint8_t* percent);
    int16_t (*get_rgb)(void* ctx, uint8_t* r, uint8_t* g, uint8_t* b);
} platform_led_ops_t;

typedef struct {
    const platform_led_ops_t* ops;
    const char* name;
    platform_led_type_t type;
    platform_led_state_t state;
    void* user_data;
} platform_led_base_t;

#define LED_ASSERT(expr) ((void)0)

#define LED_ON(led) \
    do { \
        LED_ASSERT((led) && (led)->ops && (led)->ops->on); \
        if ((led)->ops->on) { \
            (led)->ops->on((led)); \
            (led)->state = LED_STATE_ON; \
        } \
    } while(0)

#define LED_OFF(led) \
    do { \
        LED_ASSERT((led) && (led)->ops && (led)->ops->off); \
        if ((led)->ops->off) { \
            (led)->ops->off((led)); \
            (led)->state = LED_STATE_OFF; \
        } \
    } while(0)

#define LED_TOGGLE(led) \
    do { \
        if ((led)->ops->toggle) { \
            (led)->ops->toggle((led)); \
            (led)->state = (led)->state == LED_STATE_ON ? LED_STATE_OFF : LED_STATE_ON; \
        } \
    } while(0)

#define LED_SET_BRIGHTNESS(led, p) \
    do { \
        if ((led)->ops->set_brightness) { \
            (led)->ops->set_brightness((led), (p)); \
        } \
    } while(0)

#define LED_SET_RGB(led, r, g, b) \
    do { \
        if ((led)->ops->set_rgb) { \
            (led)->ops->set_rgb((led), (r), (g), (b)); \
        } \
    } while(0)

#define LED_GET_STATE(led, pstate) \
    ((led) && (led)->ops && (led)->ops->get_state ? (led)->ops->get_state((led), (pstate)) : (int16_t)-1)

#define LED_GET_BRIGHTNESS(led, ppercent) \
    ((led) && (led)->ops && (led)->ops->get_brightness ? (led)->ops->get_brightness((led), (ppercent)) : (int16_t)-1)

#define LED_GET_RGB(led, pr, pg, pb) \
    ((led) && (led)->ops && (led)->ops->get_rgb ? (led)->ops->get_rgb((led), (pr), (pg), (pb)) : (int16_t)-1)

#define LED_INIT_BASE(led_ptr, ops_ptr, led_name, led_type) \
    do { \
        (led_ptr)->ops = (ops_ptr); \
        (led_ptr)->name = (led_name); \
        (led_ptr)->type = (led_type); \
        (led_ptr)->state = LED_STATE_OFF; \
        (led_ptr)->user_data = NULL; \
    } while(0)

typedef struct {
    platform_led_base_t base;
} platform_gpio_led_t;

typedef struct {
    platform_led_base_t base;
    uint8_t brightness;
} platform_pwm_led_t;

typedef struct {
    platform_led_base_t base;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} platform_rgb_led_t;

#endif

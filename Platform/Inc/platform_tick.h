#ifndef PLATFORM_TICK_H
#define PLATFORM_TICK_H

#include <stdint.h>

typedef struct
{
    void (*delay_ms)(uint32_t ms);
    uint32_t (*get_tick)(void);
} platform_tick_ops_t;

typedef struct
{
    const platform_tick_ops_t *ops;
    const char *name;
    void *user_data;
} platform_tick_base_t;

#define PLATFORM_DELAY_MS(tick, ms) \
    ((tick) && (tick)->ops && (tick)->ops->delay_ms ? (tick)->ops->delay_ms((ms)) : (void)0)

#define PLATFORM_GET_TICK(tick) \
    ((tick) && (tick)->ops && (tick)->ops->get_tick ? (tick)->ops->get_tick() : (uint32_t)0)

#define PLATFORM_TICK_INIT_BASE(tick_ptr, ops_ptr, tick_name) \
    do \
    { \
        (tick_ptr)->ops = (ops_ptr); \
        (tick_ptr)->name = (tick_name); \
        (tick_ptr)->user_data = NULL; \
    } while (0)

#endif

#ifndef __LED_H
#define __LED_H

#include "main.h"

extern uint16_t led_timer_counter;

void led_control_task(void);

#endif /* __LED_H */

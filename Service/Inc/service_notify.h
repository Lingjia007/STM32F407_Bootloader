#ifndef SERVICE_NOTIFY_H
#define SERVICE_NOTIFY_H

#include "platform_led.h"

void service_battery_alert(platform_led_base_t* led, uint8_t battery);
void service_led_blink_pattern(platform_led_base_t* led, uint8_t pattern);
const char* service_led_get_name(platform_led_base_t* led);
platform_led_type_t service_led_get_type(platform_led_base_t* led);
platform_led_state_t service_led_get_current_state(platform_led_base_t* led);

#endif

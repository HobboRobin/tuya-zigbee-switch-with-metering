#ifndef _LED_H_
#define _LED_H_

#include "hal/gpio.h"
#include "hal/tasks.h"
#include <stdint.h>

typedef struct {
    hal_gpio_pin_t pin;
    uint8_t        on_high;
    uint8_t        on;
    uint16_t       blink_times_left;
    uint16_t       blink_time_on;
    uint16_t       blink_time_off;
    hal_task_t     blink_task;
    // Optional PWM dimming. Set `dimmable` before led_init(); the PWM channel
    // is derived from the pin by the HAL (cleared again if the pin has none).
    uint8_t        dimmable;
    uint8_t        pwm_channel;
    uint8_t        brightness;    // target on-level 0..255 (255 = full)
    uint16_t       transition_ms; // fade duration for on/off/brightness changes
    uint8_t        cur_duty;      // current PWM duty 0..255
    uint8_t        target_duty;   // fade target
    hal_task_t     fade_task;
} led_t;

/**
 * @brief      Initialize led (set initial state)
 * @param	   *led - Led to use
 * @return     none
 */
void led_init(led_t *led);

/**
 * @brief      Turn on led, canceling any blinking
 * @param	   *led - Led to use
 * @return     none
 */
void led_on(led_t *led);

/**
 * @brief      Turn off led, canceling any blinking
 * @param	   *led - Led to use
 * @return     none
 */
void led_off(led_t *led);

/**
 * @brief      Set the on-brightness of a dimmable led. If the led is currently
 *             on, it fades to the new brightness immediately (transition time).
 * @param	   *led - Led to use
 *             brightness - 0..255 (0 = off, 255 = full)
 * @return     none
 */
void led_set_brightness(led_t *led, uint8_t brightness);

/**
 * @brief      Set the fade duration for on/off/brightness changes (dimmable led)
 * @param	   *led - Led to use
 *             transition_ms - fade duration in milliseconds (0 = instant)
 * @return     none
 */
void led_set_transition(led_t *led, uint16_t transition_ms);

#define LED_BLINK_FOREVER    0xFFFF

/**
 * @brief      Start led blinking, will go to off when finished
 * @param	     *led - Led to use
 *             on_time_ms - Time led should be on in milliseconds
 *             off_time_ms - Time led should be off in milliseconds
 *             times - Times to repeat blink before returning to fixed state,
 *                     0xFFFF - blink forever
 * @return     none
 */
void led_blink(led_t *led, uint16_t on_time_ms, uint16_t off_time_ms,
               uint16_t times);

#endif

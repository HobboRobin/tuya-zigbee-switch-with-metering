#ifndef _HAL_PWM_H_
#define _HAL_PWM_H_

#include <stdint.h>
#include "hal/gpio.h"

// Number of hardware PWM channels available (TLSR8258: PWM0..PWM5).
#define HAL_PWM_CHANNELS    6

/**
 * Route a pin to a PWM channel and start it at duty 0.
 * @param pin       GPIO pin to output the PWM signal on
 * @param channel   PWM channel index (0..HAL_PWM_CHANNELS-1)
 * @param inverted  1 for active-low outputs (LED on when pin low)
 */
void hal_pwm_init(hal_gpio_pin_t pin, uint8_t channel, uint8_t inverted);

/** Set the duty cycle of a PWM channel (0 = off .. 255 = full on). */
void hal_pwm_set_duty(uint8_t channel, uint8_t duty);

#endif /* _HAL_PWM_H_ */

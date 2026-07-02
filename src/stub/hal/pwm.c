#include "hal/pwm.h"

// Host stub: just record the last duty per channel so the firmware logic can be
// exercised without real PWM hardware.
static uint8_t stub_pwm_duty[HAL_PWM_CHANNELS];

void hal_pwm_init(hal_gpio_pin_t pin, uint8_t channel, uint8_t inverted) {
    (void)pin;
    (void)inverted;
    if (channel < HAL_PWM_CHANNELS)
        stub_pwm_duty[channel] = 0;
}

void hal_pwm_set_duty(uint8_t channel, uint8_t duty) {
    if (channel < HAL_PWM_CHANNELS)
        stub_pwm_duty[channel] = duty;
}

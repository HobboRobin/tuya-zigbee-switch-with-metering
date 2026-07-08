#include "hal/pwm.h"

// Silabs devices do not currently use PWM-dimmable LEDs. Report "no PWM on
// this pin" so the led driver falls back to plain GPIO; a real implementation
// (EFR32 TIMER/PRS) can be added if a Silabs board ever needs a dimmable
// indicator.
int8_t hal_pwm_init(hal_gpio_pin_t pin, uint8_t inverted) {
    (void)pin;
    (void)inverted;
    return -1;
}

void hal_pwm_set_duty(uint8_t channel, uint8_t duty) {
    (void)channel;
    (void)duty;
}

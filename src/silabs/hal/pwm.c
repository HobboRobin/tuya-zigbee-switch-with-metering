#include "hal/pwm.h"

// Silabs devices do not currently use PWM-dimmable LEDs. Provide no-op stubs so
// the shared led/config code links; a real implementation (EFR32 TIMER/PRS)
// can be added if a Silabs board ever needs a dimmable indicator.
void hal_pwm_init(hal_gpio_pin_t pin, uint8_t channel, uint8_t inverted) {
    (void)pin;
    (void)channel;
    (void)inverted;
}

void hal_pwm_set_duty(uint8_t channel, uint8_t duty) {
    (void)channel;
    (void)duty;
}

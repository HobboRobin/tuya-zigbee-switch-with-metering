#include "hal/pwm.h"

// Host stub: hand out channels sequentially and record the last duty per
// channel so the firmware logic can be exercised without real PWM hardware.
// Re-initializing the same pin is idempotent (led_init may run more than once).
static uint8_t        stub_pwm_duty[HAL_PWM_CHANNELS];
static hal_gpio_pin_t stub_channel_owner[HAL_PWM_CHANNELS];
static uint8_t        stub_next_channel = 0;

int8_t hal_pwm_init(hal_gpio_pin_t pin, uint8_t inverted) {
    (void)inverted;

    for (uint8_t c = 0; c < stub_next_channel; c++) {
        if (stub_channel_owner[c] == pin)
            return (int8_t)c;
    }
    if (stub_next_channel >= HAL_PWM_CHANNELS)
        return -1;

    uint8_t channel = stub_next_channel++;
    stub_channel_owner[channel] = pin;
    stub_pwm_duty[channel]      = 0;
    return (int8_t)channel;
}

void hal_pwm_set_duty(uint8_t channel, uint8_t duty) {
    if (channel < HAL_PWM_CHANNELS)
        stub_pwm_duty[channel] = duty;
}

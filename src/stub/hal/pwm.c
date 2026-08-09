#include "hal/pwm.h"

// Host stub: hand out channels sequentially and record the last duty per
// channel so the firmware logic can be exercised without real PWM hardware.
// Re-initializing the same pin is idempotent (led_init may run more than once).
static uint8_t        stub_pwm_duty[HAL_PWM_CHANNELS];
static uint8_t        stub_pwm_inverted[HAL_PWM_CHANNELS];
static hal_gpio_pin_t stub_channel_owner[HAL_PWM_CHANNELS];
static uint8_t        stub_next_channel = 0;

int8_t hal_pwm_init(hal_gpio_pin_t pin, uint8_t inverted) {
    for (uint8_t c = 0; c < stub_next_channel; c++) {
        if (stub_channel_owner[c] == pin)
            return (int8_t)c;
    }
    if (stub_next_channel >= HAL_PWM_CHANNELS)
        return -1;

    uint8_t channel = stub_next_channel++;
    stub_channel_owner[channel] = pin;
    stub_pwm_duty[channel]      = 0;
    // Recorded rather than applied: on real hardware the polarity lives in the
    // PWM block, so the duty the firmware asks for stays the same either way.
    // Keeping it readable is what lets a test tell the two wirings apart.
    stub_pwm_inverted[channel] = inverted ? 1 : 0;
    return (int8_t)channel;
}

void hal_pwm_set_duty(uint8_t channel, uint8_t duty) {
    if (channel < HAL_PWM_CHANNELS)
        stub_pwm_duty[channel] = duty;
}

// Read back what a pin is currently driving, so tests can assert the mix of a
// tunable white instead of someone having to watch an actual strip.
int16_t stub_pwm_get_duty_for_pin(hal_gpio_pin_t pin) {
    for (uint8_t c = 0; c < stub_next_channel; c++) {
        if (stub_channel_owner[c] == pin)
            return (int16_t)stub_pwm_duty[c];
    }
    return -1;
}

// Polarity the pin was set up with (-1 if the pin has no PWM channel).
int8_t stub_pwm_get_inverted_for_pin(hal_gpio_pin_t pin) {
    for (uint8_t c = 0; c < stub_next_channel; c++) {
        if (stub_channel_owner[c] == pin)
            return (int8_t)stub_pwm_inverted[c];
    }
    return -1;
}

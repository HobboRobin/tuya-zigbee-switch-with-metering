#include "hal/pwm.h"
#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)

// Period in PWM ticks. The PWM clock is set to the system clock below, so this
// gives ~1 kHz (flicker-free for an LED) with plenty of duty resolution.
#define HAL_PWM_CYCLE_TICKS    (CLOCK_SYS_CLOCK_HZ / 1000)

static uint8_t pwm_clk_initialized = 0;

void hal_pwm_init(hal_gpio_pin_t pin, uint8_t channel, uint8_t inverted) {
    if (channel >= HAL_PWM_CHANNELS)
        return;

    if (!pwm_clk_initialized) {
        // Run the PWM counter at the system clock (divider 1).
        pwm_set_clk(CLOCK_SYS_CLOCK_HZ, CLOCK_SYS_CLOCK_HZ);
        pwm_clk_initialized = 1;
    }

    pwm_set_mode((pwm_id)channel, PWM_NORMAL_MODE);
    pwm_set_phase((pwm_id)channel, 0);
    pwm_set_cycle_and_duty((pwm_id)channel, HAL_PWM_CYCLE_TICKS, 0);

    // Route the pin to this channel. The *_N (inverted) function drives an
    // active-low LED correctly, so a higher duty always means brighter.
    gpio_set_func((GPIO_PinTypeDef)pin,
                  (GPIO_FuncTypeDef)((inverted ? AS_PWM0_N : AS_PWM0) + channel));

    pwm_start((pwm_id)channel);
}

void hal_pwm_set_duty(uint8_t channel, uint8_t duty) {
    if (channel >= HAL_PWM_CHANNELS)
        return;

    uint16_t cmp = (uint16_t)(((uint32_t)duty * HAL_PWM_CYCLE_TICKS) / 255u);
    pwm_set_cmp((pwm_id)channel, cmp);
}

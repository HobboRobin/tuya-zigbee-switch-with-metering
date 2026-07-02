#include "hal/pwm.h"
#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)

// Period in PWM ticks. The PWM clock is set to the system clock below, so this
// gives ~1 kHz (flicker-free for an LED) with plenty of duty resolution.
#define HAL_PWM_CYCLE_TICKS    (CLOCK_SYS_CLOCK_HZ / 1000)

// TLSR8258 pin-function mux: every GPIO supports at most one PWM function
// (datasheet "GPIO lookup table" / SDK gpio_set_func). `is_n` marks pins that
// only expose the complementary PWMx_N output. Pins not listed here have no
// PWM function at all.
typedef struct {
    uint16_t pin;     // GPIO_PinTypeDef value
    uint8_t  channel; // PWM0..PWM5
    uint8_t  is_n;    // 1 = pin carries PWMx_N (inverted output)
} pwm_pin_map_t;

static const pwm_pin_map_t pwm_pin_map[] = {
    { GPIO_PA0, 0, 1 },
    { GPIO_PA2, 0, 0 },
    { GPIO_PA3, 1, 0 },
    { GPIO_PA4, 2, 0 },
    { GPIO_PB0, 3, 0 },
    { GPIO_PB1, 4, 0 },
    { GPIO_PB2, 5, 0 },
    { GPIO_PB3, 0, 1 },
    { GPIO_PB4, 4, 0 },
    { GPIO_PB5, 5, 0 },
    { GPIO_PC0, 4, 1 },
    { GPIO_PC1, 0, 0 },
    { GPIO_PC2, 0, 0 },
    { GPIO_PC3, 1, 0 },
    { GPIO_PC4, 2, 0 },
    { GPIO_PC5, 3, 1 },
    { GPIO_PC6, 4, 1 },
    { GPIO_PC7, 5, 1 },
    { GPIO_PD2, 3, 0 },
    { GPIO_PD3, 1, 1 },
    { GPIO_PD4, 2, 1 },
    { GPIO_PD5, 0, 0 },
};

static uint8_t pwm_clk_initialized = 0;
static uint8_t pwm_channels_used   = 0; // bitmask of allocated channels

int8_t hal_pwm_init(hal_gpio_pin_t pin, uint8_t inverted) {
    const pwm_pin_map_t *map = NULL;

    for (uint8_t i = 0; i < sizeof(pwm_pin_map) / sizeof(pwm_pin_map[0]); i++) {
        if (pwm_pin_map[i].pin == pin) {
            map = &pwm_pin_map[i];
            break;
        }
    }
    if (map == NULL)
        return -1; // pin has no PWM function

    uint8_t channel = map->channel;
    if (pwm_channels_used & (1u << channel))
        return -1; // channel already driven by another pin

    pwm_channels_used |= (1u << channel);

    if (!pwm_clk_initialized) {
        // Gate the PWM peripheral clock on (off by default) and run the PWM
        // counter at the system clock (divider 1). Without the clock gate the
        // PWM block produces no output and the muxed pin stays stuck.
        reg_clk_en0 |= FLD_CLK0_PWM_EN;
        pwm_set_clk(CLOCK_SYS_CLOCK_HZ, CLOCK_SYS_CLOCK_HZ);
        pwm_clk_initialized = 1;
    }

    pwm_set_mode((pwm_id)channel, PWM_NORMAL_MODE);
    pwm_set_cycle_and_duty((pwm_id)channel, HAL_PWM_CYCLE_TICKS, 0);

    // The base PWM signal is high for `cmp` out of `cycle` ticks; a PWMx_N pin
    // outputs its complement. Fix up the polarity so that a higher duty always
    // means a brighter LED (`inverted` LEDs are on while the pin is low).
    if (inverted && !map->is_n) {
        BM_SET(reg_pwm_invert, BIT(channel));
    } else if (!inverted && map->is_n) {
        BM_SET(reg_pwm_n_invert, BIT(channel));
    }

    // Route the pin to its PWM function (this unmaps it from plain GPIO).
    gpio_set_func((GPIO_PinTypeDef)pin,
                  (GPIO_FuncTypeDef)((map->is_n ? AS_PWM0_N : AS_PWM0)
                                     + channel));

    pwm_start((pwm_id)channel);
    return (int8_t)channel;
}

void hal_pwm_set_duty(uint8_t channel, uint8_t duty) {
    if (channel >= HAL_PWM_CHANNELS)
        return;

    uint16_t cmp = (uint16_t)(((uint32_t)duty * HAL_PWM_CYCLE_TICKS) / 255u);
    pwm_set_cmp((pwm_id)channel, cmp);
}

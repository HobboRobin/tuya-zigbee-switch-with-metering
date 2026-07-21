#include "hal/pwm.h"

#include "em_cmu.h"
#include "em_gpio.h"
#include "em_timer.h"

#include "silabs/hal/silabs_gpio_utils.h"

// PWM for dimmable LEDs on TIMER1 (TIMER0 is left free; the radio uses its own
// PROTIMER and the sleeptimer runs on the RTCC). EFR32 series 2 routes any
// TIMER compare channel to any GPIO, so unlike the TLSR8258 there is no pin
// mux table — channels are simply handed out in order.

#define PWM_TIMER          TIMER1
#define PWM_TIMER_CLOCK    cmuClock_TIMER1
#define PWM_TIMER_IDX      1
#define PWM_CHANNELS       3     // TIMER1 has CC0..CC2
#define PWM_FREQ_HZ        1000u // flicker-free for an LED, same as telink

static uint8_t pwm_initialized = 0;
static uint32_t       pwm_top           = 0;
static uint8_t        pwm_channels_used = 0;              // bitmask
static hal_gpio_pin_t pwm_channel_owner[PWM_CHANNELS];

static void pwm_timer_init_once(void) {
    if (pwm_initialized)
        return;

    CMU_ClockEnable(cmuClock_GPIO, true);
    CMU_ClockEnable(PWM_TIMER_CLOCK, true);

    TIMER_Init_TypeDef init = TIMER_INIT_DEFAULT;
    init.prescale = timerPrescale64;
    init.enable   = false;
    TIMER_Init(PWM_TIMER, &init);

    // Period in timer ticks for the target PWM frequency.
    pwm_top = CMU_ClockFreqGet(PWM_TIMER_CLOCK) / 64u / PWM_FREQ_HZ;
    TIMER_TopSet(PWM_TIMER, pwm_top);
    TIMER_Enable(PWM_TIMER, true);

    pwm_initialized = 1;
}

int8_t hal_pwm_init(hal_gpio_pin_t pin, uint8_t inverted) {
    // Re-init of the same pin returns its existing channel (mirrors telink).
    for (uint8_t i = 0; i < PWM_CHANNELS; i++) {
        if ((pwm_channels_used & (1u << i)) && pwm_channel_owner[i] == pin)
            return (int8_t)i;
    }

    int8_t channel = -1;
    for (uint8_t i = 0; i < PWM_CHANNELS; i++) {
        if (!(pwm_channels_used & (1u << i))) {
            channel = (int8_t)i;
            break;
        }
    }
    if (channel < 0)
        return -1; // all channels taken; led driver falls back to plain GPIO

    pwm_timer_init_once();

    GPIO_Port_TypeDef port   = silabs_hal_gpio_port(pin);
    uint8_t           pin_no = silabs_hal_gpio_pin_number(pin);

    TIMER_InitCC_TypeDef cc_init = TIMER_INITCC_DEFAULT;
    cc_init.mode      = timerCCModePWM;
    cc_init.outInvert = inverted ? true : false;
    TIMER_InitCC(PWM_TIMER, channel, &cc_init);

    // Start at duty 0 (LED off, respecting polarity via outInvert).
    TIMER_CompareSet(PWM_TIMER, channel, 0);

    // Route the compare output to the pin (CC0ROUTE..CC2ROUTE are consecutive
    // registers, so index off CC0ROUTE).
    (&GPIO->TIMERROUTE[PWM_TIMER_IDX].CC0ROUTE)[channel] =
        ((uint32_t)port << _GPIO_TIMER_CC0ROUTE_PORT_SHIFT) |
        ((uint32_t)pin_no << _GPIO_TIMER_CC0ROUTE_PIN_SHIFT);
    GPIO->TIMERROUTE[PWM_TIMER_IDX].ROUTEEN |=
        (GPIO_TIMER_ROUTEEN_CC0PEN << channel);

    GPIO_PinModeSet(port, pin_no, gpioModePushPull, inverted ? 1 : 0);

    pwm_channels_used         |= (1u << channel);
    pwm_channel_owner[channel] = pin;
    return channel;
}

void hal_pwm_set_duty(uint8_t channel, uint8_t duty) {
    if (channel >= PWM_CHANNELS || !(pwm_channels_used & (1u << channel)))
        return;

    // duty 255 must be a solid 100% on, so scale over top+1.
    uint32_t cmp = ((pwm_top + 1u) * (uint32_t)duty) / 255u;
    TIMER_CompareBufSet(PWM_TIMER, channel, cmp);
}

#ifndef _NETWORK_INDICATOR_H_
#define _NETWORK_INDICATOR_H_

#include "led.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    led_t * leds[4];
    bool    has_dedicated_led;
    bool    manual_state_when_connected;
    // Which of the leds take part, one bit each. A single status LED leaves
    // this at "all". A multi-colour indicator (Y token) registers one led per
    // colour and lights only the combination standing for the configured mode,
    // so the same three leds can show white, yellow, blue, green or red.
    // Unmasked leds are driven off rather than left alone, otherwise a mode
    // change would leave the previous colour's leds burning.
    uint8_t mask;
} network_indicator_t;

#define NETWORK_INDICATOR_MASK_ALL    0x0F

void network_indicator_connected(network_indicator_t *indicator);

void network_indicator_from_manual_state(network_indicator_t *indicator);

void network_indicator_commission_success(network_indicator_t *indicator);

void network_indicator_not_connected(network_indicator_t *indicator);

#endif

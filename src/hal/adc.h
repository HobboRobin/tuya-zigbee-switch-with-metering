#ifndef _HAL_ADC_H_
#define _HAL_ADC_H_

#include "hal/gpio.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HAL_ADC_INPUT_PIN,  // ADC from external input
    HAL_ADC_INPUT_VBAT, // ADC from internal voltage bus
} hal_adc_input_t;

/**
 * @brief  Can this pin be used as an ADC channel at all?
 *
 * Only a handful of pins are wired to the converter. Asking the chip to
 * measure any other pin does not fail - it quietly selects "no input" and
 * reports a meaningless voltage, which is how a battery level of zero ends up
 * on a device with a full cell.
 */
bool hal_adc_pin_has_channel(hal_gpio_pin_t pin);

/**
 * @brief  An ADC-capable pin that nothing else in this config has claimed,
 *         or HAL_INVALID_PIN if there is none.
 *
 * Battery sensing drives the pin high and measures it against ground, so the
 * reading is the supply voltage whichever pin is used - but the pin must be
 * free, or the measurement fights whatever else drives it.
 */
hal_gpio_pin_t hal_adc_find_free_channel_pin(void);

void hal_adc_init(hal_adc_input_t input, hal_gpio_pin_t pin);

uint16_t hal_adc_read_mv();

#endif

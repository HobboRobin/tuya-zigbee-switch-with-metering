#include "hal/adc.h"

static uint16_t stub_adc_voltage_mv = 3000;

// Mirrors the Telink chip's ADC_GPIO_tab, in the same preference order, so a
// config that would measure nothing on the real device measures nothing here
// too rather than passing its tests and failing in the field.
static const char *const adc_capable_pins[] = {
    "B7", "B6", "B5", "B4", "B3", "B2", "B1", "B0", "C5", "C4",
};

#define ADC_CAPABLE_PIN_CNT \
        (sizeof(adc_capable_pins) / sizeof(adc_capable_pins[0]))

bool hal_adc_pin_has_channel(hal_gpio_pin_t pin) {
    for (unsigned i = 0; i < ADC_CAPABLE_PIN_CNT; i++) {
        if (hal_gpio_parse_pin(adc_capable_pins[i]) == pin) {
            return true;
        }
    }
    return false;
}

hal_gpio_pin_t hal_adc_find_free_channel_pin(void) {
    for (unsigned i = 0; i < ADC_CAPABLE_PIN_CNT; i++) {
        hal_gpio_pin_t pin = hal_gpio_parse_pin(adc_capable_pins[i]);
        if (!hal_gpio_is_claimed(pin)) {
            return pin;
        }
    }
    return HAL_INVALID_PIN;
}

void hal_adc_init(hal_adc_input_t input, hal_gpio_pin_t pin) {
    (void)input;
    (void)pin;
}

uint16_t hal_adc_read_mv() {
    return stub_adc_voltage_mv;
}

void stub_set_adc_voltage_mv(uint16_t voltage_mv) {
    stub_adc_voltage_mv = voltage_mv;
}

void stub_set_battery_voltage_mv(uint16_t voltage_mv) {
    stub_set_adc_voltage_mv(voltage_mv);
}

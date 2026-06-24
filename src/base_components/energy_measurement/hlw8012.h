#ifndef _HLW8012_H_
#define _HLW8012_H_

#include <stdint.h>
#include "hal/gpio.h"
#include "base_components/energy_meter.h"
#include "hal/tasks.h"

#define HLW8012_FIXED_POINT_SCALE            65536
#define HLW8012_POWER_MULTIPLIER             132777
#define HLW8012_VOLTAGE_MULTIPLIER           12190
#define HLW8012_CURRENT_MULTIPLIER           843
#define HLW8012_SEL_TOGGLE_CYCLE_INTERVAL    5
#define HLW8012_PULSE_TIMEOUT_MS             20000
#define HLW8012_SAMPLE_INTERVAL_MS           5000

typedef struct {
    uint32_t cf_pulse_count;
    uint32_t cf_last_pulse_time;
    uint32_t cf_total_pulse_count;
    uint32_t cf1_pulse_count;
    uint32_t cf1_last_pulse_time;
    uint32_t cf1_total_pulse_count;
    uint32_t last_sample_time;
    uint32_t cf_tick_pulse_count;
    uint32_t cf1_tick_pulse_count;
    uint8_t  cf_last_gpio_state;
    uint8_t  cf1_last_gpio_state;
    uint16_t voltage;
    uint16_t current;
    int16_t  power;
    uint32_t energy;
    uint8_t  sel_state;
    uint8_t  valid;
    uint32_t freq_cf;
    uint32_t freq_cf1;
} hlw8012_data_t;

typedef struct {
    hal_gpio_pin_t cf_pin;
    hal_gpio_pin_t cf1_pin;
    hal_gpio_pin_t sel_pin;
    hlw8012_data_t data;
    hal_task_t     update_task;
    uint8_t        cycle_count;
    uint8_t        initialized;
    energy_meter_t meter;
} hlw8012_t;

int            hlw8012_init(hlw8012_t *dev, hal_gpio_pin_t cf_pin,
                            hal_gpio_pin_t cf1_pin, hal_gpio_pin_t sel_pin);
hlw8012_data_t *hlw8012_get_data(hlw8012_t *dev);
void            hlw8012_reset_energy(hlw8012_t *dev);
energy_meter_t *hlw8012_as_energy_meter(hlw8012_t *dev);
void            hlw8012_tick(hlw8012_t *dev);

#endif /* _HLW8012_H_ */

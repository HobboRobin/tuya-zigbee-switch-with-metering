#ifndef _HLW8012_H_
#define _HLW8012_H_

#include <stdint.h>
#include "hal/gpio.h"
#include "base_components/energy_meter.h"
#include "hal/tasks.h"

// Calibration for TS011F-BS-PM-2 (BSEED), derived from hardware measurement:
//   236.5 V <-> 9733 CF1 pulses/5s  (voltage mode)
//   10 W    <-> ~2.5 CF pulses/5s   (power)
//   42.3 mA <-> 5 CF1 pulses/5s     (current mode)
// Physical value = pulses * MULTIPLIER / FIXED_POINT_SCALE.
// Units: power in W, voltage in V, current in mA.
#define HLW8012_FIXED_POINT_SCALE            65536
#define HLW8012_POWER_MULTIPLIER             262144 // 4 W per pulse
#define HLW8012_VOLTAGE_MULTIPLIER           1593   // ~0.0243 V per pulse
#define HLW8012_CURRENT_MULTIPLIER           554196 // ~8.46 mA per pulse
#define HLW8012_SEL_TOGGLE_CYCLE_INTERVAL    5
#define HLW8012_PULSE_TIMEOUT_MS             20000
#define HLW8012_SAMPLE_INTERVAL_MS           5000

// Energy accumulates pulses*POWER_MULTIPLIER per sample; this many sub-units
// equal 1 Wh: FIXED_POINT_SCALE * 3600s / sample_seconds. Kept under 2^32 so
// energy can be derived with 32-bit subtraction (TC32 has no 64-bit divide).
#define HLW8012_ENERGY_WH_SUBUNIT \
        (HLW8012_FIXED_POINT_SCALE * 3600u / (HLW8012_SAMPLE_INTERVAL_MS / 1000u))

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
    uint32_t energy_acc; // energy sub-unit remainder (pulses*MULT, < 1 Wh)
    uint8_t  sel_state;
    uint8_t  valid;
    uint32_t freq_cf;
    uint32_t freq_cf1;
} hlw8012_data_t;

typedef struct {
    hal_gpio_pin_t     cf_pin;
    hal_gpio_pin_t     cf1_pin;
    hal_gpio_pin_t     sel_pin;
    hal_gpio_counter_t cf_counter;
    hal_gpio_counter_t cf1_counter;
    hlw8012_data_t     data;
    hal_task_t         update_task;
    uint8_t            cycle_count;
    uint8_t            initialized;
    energy_meter_t     meter;
} hlw8012_t;

int            hlw8012_init(hlw8012_t *dev, hal_gpio_pin_t cf_pin,
                            hal_gpio_pin_t cf1_pin, hal_gpio_pin_t sel_pin);
hlw8012_data_t *hlw8012_get_data(hlw8012_t *dev);
void            hlw8012_reset_energy(hlw8012_t *dev);
energy_meter_t *hlw8012_as_energy_meter(hlw8012_t *dev);
void            hlw8012_tick(hlw8012_t *dev);

#endif /* _HLW8012_H_ */

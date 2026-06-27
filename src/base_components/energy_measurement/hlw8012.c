#include "hlw8012.h"
#include "hal/timer.h"
#include "hal/printf_selector.h"
#include <string.h>

static void hlw8012_meter_get_data(void *ctx, energy_meter_data_t *data);
static void hlw8012_meter_reset_energy(void *ctx);
static void hlw8012_meter_tick(void *ctx);
void        _update_measurement_handler(void *arg);
void        _cycle_sel_pin(hlw8012_t *dev);

static const energy_meter_ops_t hlw8012_energy_meter_ops = {
    .get_data     = hlw8012_meter_get_data,
    .reset_energy = hlw8012_meter_reset_energy,
    .tick         = hlw8012_meter_tick,
};

static uint32_t pulses_to_frequency(uint32_t pulse_count) {
    /* Avoid 64-bit division: 1000000 / HLW8012_SAMPLE_INTERVAL_MS is exact at compile time */
    return pulse_count * (1000000u / HLW8012_SAMPLE_INTERVAL_MS);
}

int hlw8012_init(hlw8012_t *dev, hal_gpio_pin_t cf_pin, hal_gpio_pin_t cf1_pin,
                 hal_gpio_pin_t sel_pin) {
    if (!dev)
        return -1;

    memset(dev, 0, sizeof(hlw8012_t));
    dev->cf_pin  = cf_pin;
    dev->cf1_pin = cf1_pin;
    dev->sel_pin = sel_pin;

    // CF (power) and CF1 (voltage/current) are counted by hardware pulse
    // counters: the CF1 voltage signal is too high-frequency for the software
    // polling loop to catch reliably.
    dev->cf_counter =
        hal_gpio_counter_init(cf_pin, HAL_GPIO_COUNTER_RISING, HAL_GPIO_PULL_NONE);
    dev->cf1_counter =
        hal_gpio_counter_init(cf1_pin, HAL_GPIO_COUNTER_RISING, HAL_GPIO_PULL_NONE);

    hal_gpio_init(sel_pin, 0, HAL_GPIO_PULL_NONE);
    hal_gpio_set(sel_pin);

    dev->data.sel_state        = 1;
    dev->data.last_sample_time = hal_millis();

    dev->initialized         = 1;
    dev->update_task.handler = _update_measurement_handler;
    dev->update_task.arg     = dev;
    hal_tasks_init(&dev->update_task);

    energy_meter_init(&dev->meter, &hlw8012_energy_meter_ops, dev,
                      ENERGY_METER_HLW8012);

    printf("HLW8012: Initialized on CF=%04x CF1=%04x SEL=%04x\r\n", cf_pin,
           cf1_pin, sel_pin);

    hal_tasks_schedule(&dev->update_task, HLW8012_SAMPLE_INTERVAL_MS);
    return 0;
}

void _update_measurement_handler(void *arg) {
    hlw8012_t *dev = (hlw8012_t *)arg;

    if (!dev || !dev->initialized)
        return;

    uint32_t now = hal_millis();

    // Hardware pulse counters accumulate edges since the last sample.
    uint32_t cf_pulses  = hal_gpio_counter_read_and_reset(dev->cf_counter);
    uint32_t cf1_pulses = hal_gpio_counter_read_and_reset(dev->cf1_counter);

    uint32_t freq_cf_mhz  = pulses_to_frequency(cf_pulses);
    uint32_t freq_cf1_mhz = pulses_to_frequency(cf1_pulses);
    dev->data.freq_cf  = freq_cf_mhz;
    dev->data.freq_cf1 = freq_cf1_mhz;

    if (cf_pulses == 0) {
        dev->data.power = 0;
    } else {
        dev->data.power = (int16_t)((freq_cf_mhz * HLW8012_POWER_MULTIPLIER) /
                                    HLW8012_FIXED_POINT_SCALE);
        dev->data.energy +=
            ((uint32_t)cf_pulses * HLW8012_POWER_MULTIPLIER) /
            (HLW8012_FIXED_POINT_SCALE * 3600);
    }

    // Skip the sample right after a SEL toggle (cycle_count == 0): CF1 needs
    // time to settle to the newly selected measurement.
    if (dev->cycle_count != 0) {
        if (dev->data.sel_state)
            dev->data.voltage =
                (cf1_pulses == 0)
                  ? 0
                  : (uint16_t)((freq_cf1_mhz * HLW8012_VOLTAGE_MULTIPLIER) /
                               HLW8012_FIXED_POINT_SCALE);
        else
            dev->data.current =
                (cf1_pulses == 0)
                  ? 0
                  : (uint16_t)((freq_cf1_mhz * HLW8012_CURRENT_MULTIPLIER) /
                               HLW8012_FIXED_POINT_SCALE);
    }

    dev->data.valid            = 1;
    dev->data.last_sample_time = now;
    dev->cycle_count++;

    _cycle_sel_pin(dev);

    hal_tasks_schedule(&dev->update_task, HLW8012_SAMPLE_INTERVAL_MS);
}

void _cycle_sel_pin(hlw8012_t *dev) {
    if (dev->cycle_count == HLW8012_SEL_TOGGLE_CYCLE_INTERVAL) {
        if (dev->data.sel_state) {
            hal_gpio_clear(dev->sel_pin);
            dev->data.sel_state = 0;
        } else {
            hal_gpio_set(dev->sel_pin);
            dev->data.sel_state = 1;
        }
        dev->cycle_count = 0;
    }
}

hlw8012_data_t *hlw8012_get_data(hlw8012_t *dev) {
    if (!dev)
        return NULL;

    return &dev->data;
}

void hlw8012_reset_energy(hlw8012_t *dev) {
    if (!dev)
        return;

    dev->data.energy = 0;
    hal_gpio_counter_read_and_reset(dev->cf_counter);
    hal_gpio_counter_read_and_reset(dev->cf1_counter);
}

static void hlw8012_meter_get_data(void *ctx, energy_meter_data_t *data) {
    hlw8012_t *dev = (hlw8012_t *)ctx;

    if (!dev || !data)
        return;

    data->voltage   = dev->data.voltage;
    data->current   = dev->data.current;
    data->power     = dev->data.power;
    data->energy    = dev->data.energy;
    data->freq_cf   = dev->data.freq_cf;
    data->freq_cf1  = dev->data.freq_cf1;
    data->sel_state = dev->data.sel_state;
    data->valid     = dev->data.valid;
}

static void hlw8012_meter_reset_energy(void *ctx) {
    hlw8012_reset_energy((hlw8012_t *)ctx);
}

static void hlw8012_meter_tick(void *ctx) {
    hlw8012_tick((hlw8012_t *)ctx);
}

energy_meter_t *hlw8012_as_energy_meter(hlw8012_t *dev) {
    if (!dev || !dev->initialized)
        return NULL;

    return &dev->meter;
}

void hlw8012_tick(hlw8012_t *dev) {
    // No-op: CF/CF1 pulses are counted by hardware counters, so no per-loop
    // software polling is needed. Kept for API/main-loop compatibility.
    (void)dev;
}

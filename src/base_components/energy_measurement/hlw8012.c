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

    hal_gpio_init(cf_pin, 1, HAL_GPIO_PULL_NONE);
    hal_gpio_init(cf1_pin, 1, HAL_GPIO_PULL_NONE);
    hal_gpio_init(sel_pin, 0, HAL_GPIO_PULL_NONE);
    hal_gpio_set(sel_pin);

    dev->data.sel_state          = 1;
    dev->data.last_sample_time   = hal_millis();
    dev->data.cf_tick_pulse_count  = 0;
    dev->data.cf1_tick_pulse_count = 0;
    dev->data.cf_last_gpio_state   = hal_gpio_read(cf_pin);
    dev->data.cf1_last_gpio_state  = hal_gpio_read(cf1_pin);

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

    uint32_t cf_total_pulses  = dev->data.cf_tick_pulse_count;
    uint32_t cf1_total_pulses = dev->data.cf1_tick_pulse_count;

    uint32_t cf_pulses  = cf_total_pulses - dev->data.cf_total_pulse_count;
    uint32_t cf1_pulses = cf1_total_pulses - dev->data.cf1_total_pulse_count;

    dev->data.cf_total_pulse_count  = cf_total_pulses;
    dev->data.cf1_total_pulse_count = cf1_total_pulses;

    if (dev->data.cf_last_pulse_time > 0 &&
        (now - dev->data.cf_last_pulse_time) > HLW8012_PULSE_TIMEOUT_MS) {
        dev->data.power  = 0;
        dev->data.freq_cf = 0;
    }

    if (dev->data.cf1_last_pulse_time > 0 &&
        (now - dev->data.cf1_last_pulse_time) > HLW8012_PULSE_TIMEOUT_MS) {
        if (dev->data.sel_state)
            dev->data.voltage = 0;
        else
            dev->data.current = 0;
        dev->data.freq_cf1 = 0;
    }

    uint32_t freq_cf_mhz = pulses_to_frequency(cf_pulses);
    if (freq_cf_mhz <= 1)
        freq_cf_mhz = 0;
    dev->data.freq_cf = freq_cf_mhz;

    uint32_t freq_cf1_mhz = pulses_to_frequency(cf1_pulses);
    if (freq_cf1_mhz <= 1)
        freq_cf1_mhz = 0;
    dev->data.freq_cf1 = freq_cf1_mhz;

    if (freq_cf_mhz != 0) {
        dev->data.power = (int16_t)((freq_cf_mhz * HLW8012_POWER_MULTIPLIER) /
                                    HLW8012_FIXED_POINT_SCALE);
        dev->data.energy +=
            ((uint32_t)cf_pulses * HLW8012_POWER_MULTIPLIER) /
            (HLW8012_FIXED_POINT_SCALE * 3600);
    }

    if (freq_cf1_mhz != 0 && dev->cycle_count != 0) {
        if (dev->data.sel_state)
            dev->data.voltage =
                (uint16_t)((freq_cf1_mhz * HLW8012_VOLTAGE_MULTIPLIER) /
                            HLW8012_FIXED_POINT_SCALE);
        else
            dev->data.current =
                (uint16_t)((freq_cf1_mhz * HLW8012_CURRENT_MULTIPLIER) /
                            HLW8012_FIXED_POINT_SCALE);
    }

    dev->data.valid          = 1;
    dev->data.last_sample_time = now;
    dev->cycle_count++;

    _cycle_sel_pin(dev);

    hal_tasks_schedule(&dev->update_task, HLW8012_SAMPLE_INTERVAL_MS);
}

void _cycle_sel_pin(hlw8012_t *dev) {
    if (dev->cycle_count == HLW8012_SEL_TOGGLE_CYCLE_INTERVAL) {
        dev->data.cf1_last_pulse_time   = 0;
        dev->data.cf1_total_pulse_count = 0;
        dev->data.cf1_pulse_count       = 0;
        dev->data.cf1_tick_pulse_count  = 0;

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
    dev->data.energy              = 0;
    dev->data.cf_pulse_count      = 0;
    dev->data.cf_total_pulse_count  = 0;
    dev->data.cf_tick_pulse_count   = 0;
    dev->data.cf1_pulse_count     = 0;
    dev->data.cf1_total_pulse_count = 0;
    dev->data.cf1_tick_pulse_count  = 0;
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
    if (!dev || !dev->initialized)
        return;

    uint32_t now      = hal_millis();
    uint8_t  cf_state  = hal_gpio_read(dev->cf_pin);
    uint8_t  cf1_state = hal_gpio_read(dev->cf1_pin);

    if (dev->data.cf_last_gpio_state != cf_state) {
        dev->data.cf_tick_pulse_count++;
        dev->data.cf_last_pulse_time = now;
    }
    if (dev->data.cf1_last_gpio_state != cf1_state) {
        dev->data.cf1_tick_pulse_count++;
        dev->data.cf1_last_pulse_time = now;
    }
    dev->data.cf_last_gpio_state  = cf_state;
    dev->data.cf1_last_gpio_state = cf1_state;
}

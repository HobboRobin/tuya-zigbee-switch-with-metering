#include "battery.h"
#include "hal/adc.h"

void battery_init(battery_t *battery) {
    hal_adc_init(HAL_ADC_INPUT_VBAT, battery->pin);
    if (battery->charge_range == 0) {
        battery->charge_range = 200;
    }
}

// A lithium coin cell holds ~2.9 V for most of its life and then drops away
// quickly, so the reported charge has to fall much faster than the voltage
// does near the end. Points are taken off the light-load (sub-mA) discharge
// curve these cells are specified on, which is the load this firmware puts on
// one; between them the charge is interpolated.
//
// The practical difference: at 2.8 V a straight 2.0-3.0 V line claims 80%,
// while the cell has about half its energy left - and at 2.6 V it claims 60%
// for a cell that is nearly done.
typedef struct {
    uint16_t voltage_mv;
    uint8_t  percent;
} battery_curve_point_t;

static const battery_curve_point_t coin_cell_curve[] = {
    { 3000, 100 },
    { 2950,  95 },
    { 2900,  85 },
    { 2850,  70 },
    { 2800,  55 },
    { 2750,  42 },
    { 2700,  30 },
    { 2650,  20 },
    { 2600,  12 },
    { 2500,   6 },
    { 2400,   3 },
    { 2200,   0 },
};

#define COIN_CELL_POINTS \
        (sizeof(coin_cell_curve) / sizeof(coin_cell_curve[0]))

static uint8_t coin_cell_percent(uint16_t voltage_mv) {
    if (voltage_mv >= coin_cell_curve[0].voltage_mv) {
        return coin_cell_curve[0].percent;
    }
    for (uint8_t i = 1; i < COIN_CELL_POINTS; i++) {
        const battery_curve_point_t *high = &coin_cell_curve[i - 1];
        const battery_curve_point_t *low  = &coin_cell_curve[i];
        if (voltage_mv < low->voltage_mv) {
            continue;
        }
        uint32_t span  = high->voltage_mv - low->voltage_mv;
        uint32_t above = voltage_mv - low->voltage_mv;
        return low->percent +
               (uint8_t)((uint32_t)(high->percent - low->percent) * above / span);
    }
    return 0;
}

battery_status_t battery_get_status(battery_t *battery) {
    uint16_t         voltage_mv = hal_adc_read_mv();
    battery_status_t status     = {
        .voltage_mv = voltage_mv
    };

    if (battery->curve == BATTERY_CURVE_COIN_CELL) {
        status.charge = (uint16_t)((uint32_t)coin_cell_percent(voltage_mv) *
                                   battery->charge_range / 100);
        return status;
    }

    if (voltage_mv < battery->voltage_min) {
        status.charge = 0;
    } else if (voltage_mv > battery->voltage_max) {
        status.charge = battery->charge_range;
    } else {
        uint32_t voltage_range  = battery->voltage_max - battery->voltage_min;
        uint32_t voltage_offset = voltage_mv - battery->voltage_min;
        status.charge = voltage_offset * battery->charge_range / voltage_range;
    }
    return status;
}

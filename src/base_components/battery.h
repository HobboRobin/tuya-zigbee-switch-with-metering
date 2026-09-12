#ifndef _BATTERY_H_
#define _BATTERY_H_

#include "hal/gpio.h"
#include <stdint.h>

/** How a cell's voltage maps to what is left of it. */
typedef enum {
    // A lithium coin cell (CR2032, CR2430, CR2450). Almost all of its life is
    // spent on a plateau just under 3 V, then the voltage falls off a cliff -
    // so a straight line reads "90%" on a cell that is nearly empty.
    BATTERY_CURVE_COIN_CELL = 0,
    // Alkaline cells in series (2xAAA and the like), which fall away steadily
    // enough that a straight line between min and max is a fair description.
    BATTERY_CURVE_LINEAR    = 1,
} battery_curve_t;

typedef struct {
    hal_gpio_pin_t pin;
    uint16_t       voltage_min;
    uint16_t       voltage_max;
    uint16_t       charge_range;
    uint8_t        curve;
} battery_t;

typedef struct {
    uint16_t voltage_mv;
    uint16_t charge;
} battery_status_t;

void battery_init(battery_t *battery);

battery_status_t battery_get_status(battery_t *battery);

#endif // _BATTERY_H_

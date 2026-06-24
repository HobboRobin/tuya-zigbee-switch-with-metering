#ifndef _ENERGY_METER_H_
#define _ENERGY_METER_H_

#include <stdint.h>

typedef struct energy_meter energy_meter_t;

typedef enum {
    ENERGY_METER_NONE    = 0,
    ENERGY_METER_HLW8012 = 1,
    ENERGY_METER_BL0937  = 2,
} energy_meter_type_t;

typedef struct {
    uint16_t voltage;
    uint16_t current;
    int16_t  power;
    uint32_t energy;
    uint8_t  valid;
    uint32_t freq_cf;
    uint32_t freq_cf1;
    uint8_t  sel_state;
} energy_meter_data_t;

typedef struct {
    void (*get_data)(void *ctx, energy_meter_data_t *data);
    void (*reset_energy)(void *ctx);
    void (*tick)(void *ctx);
} energy_meter_ops_t;

struct energy_meter {
    const energy_meter_ops_t *ops;
    void *                    ctx;
    energy_meter_type_t       type;
};

static inline void energy_meter_init(energy_meter_t *meter,
                                     const energy_meter_ops_t *ops, void *ctx,
                                     energy_meter_type_t type) {
    if (!meter)
        return;

    meter->ops  = ops;
    meter->ctx  = ctx;
    meter->type = type;
}

static inline void energy_meter_get_data(energy_meter_t *meter,
                                         energy_meter_data_t *data) {
    if (meter && meter->ops && meter->ops->get_data && data)
        meter->ops->get_data(meter->ctx, data);
}

static inline void energy_meter_reset_energy(energy_meter_t *meter) {
    if (meter && meter->ops && meter->ops->reset_energy)
        meter->ops->reset_energy(meter->ctx);
}

static inline void energy_meter_tick(energy_meter_t *meter) {
    if (meter && meter->ops && meter->ops->tick)
        meter->ops->tick(meter->ctx);
}

static inline int energy_meter_is_valid(energy_meter_t *meter) {
    return meter && meter->ops && meter->ctx;
}

#endif /* _ENERGY_METER_H_ */

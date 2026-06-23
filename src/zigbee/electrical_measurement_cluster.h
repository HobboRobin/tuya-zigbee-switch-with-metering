#ifndef _ELECTRICAL_MEASUREMENT_CLUSTER_H_
#define _ELECTRICAL_MEASUREMENT_CLUSTER_H_

#include <stdint.h>
#include "hal/zigbee.h"
#include "base_components/energy_meter.h"

typedef struct {
    uint8_t        endpoint;
    energy_meter_t *meter;
    uint32_t       measurement_type;
    uint16_t       rms_voltage;
    uint16_t       rms_current;
    int16_t        active_power;
    uint16_t       ac_voltage_multiplier;
    uint16_t       ac_voltage_divisor;
    uint16_t       ac_current_multiplier;
    uint16_t       ac_current_divisor;
    uint16_t       ac_power_multiplier;
    uint16_t       ac_power_divisor;
    uint32_t       freq_cf;
    uint32_t       freq_cf1;
    uint8_t        sel_state;
    hal_zigbee_attribute attr_infos[16];
    uint32_t       last_report_time;
    uint16_t       last_reported_voltage;
    uint16_t       last_reported_current;
    int16_t        last_reported_power;
} electrical_measurement_cluster_t;

void electrical_measurement_cluster_init(electrical_measurement_cluster_t *cluster,
                                         energy_meter_t *meter);
void electrical_measurement_cluster_add_to_endpoint(
    electrical_measurement_cluster_t *cluster, hal_zigbee_endpoint *endpoint);
void electrical_measurement_cluster_update(electrical_measurement_cluster_t *cluster);
void electrical_measurement_cluster_report(electrical_measurement_cluster_t *cluster);

#endif /* _ELECTRICAL_MEASUREMENT_CLUSTER_H_ */

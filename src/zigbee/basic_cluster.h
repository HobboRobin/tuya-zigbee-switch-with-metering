#ifndef _BASIC_CLUSTER_H_
#define _BASIC_CLUSTER_H_

#include "hal/zigbee.h"

#include <stddef.h>

typedef struct {
    uint8_t              deviceEnable;
    char                 manuName[32];
    char                 modelId[32];
    hal_zigbee_attribute attr_infos[16];
} zigbee_basic_cluster;

void basic_cluster_add_to_endpoint(zigbee_basic_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint);

void basic_cluster_callback_attr_write_trampoline(uint16_t attribute_id);

/**
 * Fill the energy-monitoring diagnostic markers in swBuildId.
 * @param energy_enabled  1 if energy monitoring configured ('-' markers if 0)
 * @param elec_meas_ok    1 if haElectricalMeasurement registered in the stack
 * @param metering_ok     1 if seMetering registered in the stack
 */
void basic_cluster_set_energy_diag(uint8_t energy_enabled, uint8_t elec_meas_ok,
                                   uint8_t metering_ok);

#endif

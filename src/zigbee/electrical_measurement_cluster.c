#include "electrical_measurement_cluster.h"
#include "cluster_common.h"
#include "consts.h"
#include "hal/timer.h"
#include "hal/printf_selector.h"
#include <string.h>

#define MEAS_TYPE_AC_ACTIVE    (1 << 0)
#define MEAS_TYPE_PHASE_A      (1 << 3)

void electrical_measurement_cluster_init(electrical_measurement_cluster_t *cluster,
                                         energy_meter_t *meter) {
    if (!cluster || !meter)
        return;

    memset(cluster, 0, sizeof(electrical_measurement_cluster_t));
    cluster->meter                 = meter;
    cluster->measurement_type      = MEAS_TYPE_AC_ACTIVE | MEAS_TYPE_PHASE_A;
    cluster->ac_voltage_multiplier = 1;
    cluster->ac_voltage_divisor    = 100;  // firmware reports voltage in centivolts
    cluster->ac_current_multiplier = 1;
    cluster->ac_current_divisor    = 1000; // firmware reports current in mA
    cluster->ac_power_multiplier   = 1;
    cluster->ac_power_divisor      = 1;    // firmware reports power in whole watts
}

void electrical_measurement_cluster_add_to_endpoint(
    electrical_measurement_cluster_t *cluster, hal_zigbee_endpoint *endpoint) {
    if (!cluster || !endpoint)
        return;

    SETUP_ATTR(0, ZCL_ATTR_ELEC_MEAS_MEASUREMENT_TYPE, ZCL_DATA_TYPE_BITMAP32, ATTR_READONLY,
               cluster->measurement_type);
    SETUP_ATTR(1, ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE, ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
               cluster->rms_voltage);
    SETUP_ATTR(2, ZCL_ATTR_ELEC_MEAS_RMS_CURRENT, ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
               cluster->rms_current);
    SETUP_ATTR(3, ZCL_ATTR_ELEC_MEAS_ACTIVE_POWER, ZCL_DATA_TYPE_INT16, ATTR_READONLY,
               cluster->active_power);
    SETUP_ATTR(4, ZCL_ATTR_ELEC_MEAS_AC_VOLTAGE_MULTIPLIER, ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
               cluster->ac_voltage_multiplier);
    SETUP_ATTR(5, ZCL_ATTR_ELEC_MEAS_AC_VOLTAGE_DIVISOR, ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
               cluster->ac_voltage_divisor);
    SETUP_ATTR(6, ZCL_ATTR_ELEC_MEAS_AC_CURRENT_MULTIPLIER, ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
               cluster->ac_current_multiplier);
    SETUP_ATTR(7, ZCL_ATTR_ELEC_MEAS_AC_CURRENT_DIVISOR, ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
               cluster->ac_current_divisor);
    SETUP_ATTR(8, ZCL_ATTR_ELEC_MEAS_AC_POWER_MULTIPLIER, ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
               cluster->ac_power_multiplier);
    SETUP_ATTR(9, ZCL_ATTR_ELEC_MEAS_AC_POWER_DIVISOR, ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
               cluster->ac_power_divisor);
    SETUP_ATTR(10, ZCL_ATTR_ELEC_MEAS_CUST_FREQUENCY_CF, ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
               cluster->freq_cf);
    SETUP_ATTR(11, ZCL_ATTR_ELEC_MEAS_CUST_FREQUENCY_CF1, ZCL_DATA_TYPE_UINT32, ATTR_READONLY,
               cluster->freq_cf1);
    SETUP_ATTR(12, ZCL_ATTR_ELEC_MEAS_CUST_FREQUENCY_SEL_STATE, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               cluster->sel_state);

    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_ELECTRICAL_MEASUREMENT;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 13;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->cluster_count++;
    cluster->endpoint = endpoint->endpoint;
}

void electrical_measurement_cluster_update(electrical_measurement_cluster_t *cluster) {
    if (!cluster || !cluster->meter)
        return;

    energy_meter_data_t data;
    energy_meter_get_data(cluster->meter, &data);
    if (data.valid) {
        cluster->rms_voltage  = data.voltage;
        cluster->rms_current  = data.current;
        cluster->active_power = data.power;
        cluster->freq_cf      = data.freq_cf;
        cluster->freq_cf1     = data.freq_cf1;
        cluster->sel_state    = data.sel_state;
    }
}

void electrical_measurement_cluster_report(electrical_measurement_cluster_t *cluster) {
    // No autonomous reporting: attribute values are kept current by
    // electrical_measurement_cluster_update(), and the SDK's report_handler()
    // sends reports according to the coordinator's configureReporting settings
    // (min/max interval and reportable change), so reporting is fully
    // controllable from Z2M.
    (void)cluster;
}

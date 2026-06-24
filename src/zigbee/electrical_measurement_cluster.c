#include "electrical_measurement_cluster.h"
#include "cluster_common.h"
#include "consts.h"
#include "hal/timer.h"
#include "hal/printf_selector.h"
#include <string.h>

#define VOLTAGE_REPORT_THRESHOLD    5
#define CURRENT_REPORT_THRESHOLD    50
#define POWER_REPORT_THRESHOLD      5
#define MIN_REPORT_INTERVAL_MS      1000
#define MAX_REPORT_INTERVAL_MS      300000

#define MEAS_TYPE_AC_ACTIVE         (1 << 0)
#define MEAS_TYPE_PHASE_A           (1 << 3)

void electrical_measurement_cluster_init(electrical_measurement_cluster_t *cluster,
                                         energy_meter_t *meter) {
    if (!cluster || !meter)
        return;

    memset(cluster, 0, sizeof(electrical_measurement_cluster_t));
    cluster->meter                 = meter;
    cluster->measurement_type      = MEAS_TYPE_AC_ACTIVE | MEAS_TYPE_PHASE_A;
    cluster->ac_voltage_multiplier = 1;
    cluster->ac_voltage_divisor    = 100;
    cluster->ac_current_multiplier = 1;
    cluster->ac_current_divisor    = 1000;
    cluster->ac_power_multiplier   = 1;
    cluster->ac_power_divisor      = 1;
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
    if (!cluster)
        return;

    uint32_t now = hal_millis();
    if (now - cluster->last_report_time < MIN_REPORT_INTERVAL_MS)
        return;

    int16_t vd = (int16_t)cluster->rms_voltage - (int16_t)cluster->last_reported_voltage;
    int16_t cd = (int16_t)cluster->rms_current - (int16_t)cluster->last_reported_current;
    int16_t pd = cluster->active_power - cluster->last_reported_power;
    if (vd < 0) vd = -vd;
    if (cd < 0) cd = -cd;
    if (pd < 0) pd = -pd;

    uint8_t force = (now - cluster->last_report_time >= MAX_REPORT_INTERVAL_MS);

    if (force || vd >= VOLTAGE_REPORT_THRESHOLD) {
        hal_zigbee_send_report_attr(cluster->endpoint, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
                                    ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE, ZCL_DATA_TYPE_UINT16,
                                    &cluster->rms_voltage, sizeof(cluster->rms_voltage));
        cluster->last_reported_voltage = cluster->rms_voltage;
    }
    if (force || cd >= CURRENT_REPORT_THRESHOLD) {
        hal_zigbee_send_report_attr(cluster->endpoint, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
                                    ZCL_ATTR_ELEC_MEAS_RMS_CURRENT, ZCL_DATA_TYPE_UINT16,
                                    &cluster->rms_current, sizeof(cluster->rms_current));
        cluster->last_reported_current = cluster->rms_current;
    }
    if (force || pd >= POWER_REPORT_THRESHOLD) {
        hal_zigbee_send_report_attr(cluster->endpoint, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
                                    ZCL_ATTR_ELEC_MEAS_ACTIVE_POWER, ZCL_DATA_TYPE_INT16,
                                    &cluster->active_power, sizeof(cluster->active_power));
        cluster->last_reported_power = cluster->active_power;
    }
    cluster->last_report_time = now;
}

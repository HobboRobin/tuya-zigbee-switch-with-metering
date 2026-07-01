#include "electrical_measurement_cluster.h"
#include "cluster_common.h"
#include "consts.h"
#include "device_config/nvm_items.h"
#include "hal/nvm.h"
#include "hal/timer.h"
#include "hal/printf_selector.h"
#include <string.h>

#define MEAS_TYPE_AC_ACTIVE       (1 << 0)
#define MEAS_TYPE_PHASE_A         (1 << 3)

// Marks a valid persisted-calibration record.
#define ELEC_MEAS_CAL_NV_MAGIC    0x484C5743u // "HLWC"

typedef struct {
    uint32_t magic;
    uint32_t voltage_multiplier;
    uint32_t current_multiplier;
    uint32_t power_multiplier;
} elec_meas_cal_nv_t;

// Single instance, used to route attribute-write callbacks to calibration.
static electrical_measurement_cluster_t *g_elec_cluster = NULL;

static void elec_meas_save_calibration(electrical_measurement_cluster_t *cluster) {
    energy_meter_calibration_t cal;

    energy_meter_get_calibration(cluster->meter, &cal);
    elec_meas_cal_nv_t nv = {
        .magic              = ELEC_MEAS_CAL_NV_MAGIC,
        .voltage_multiplier = cal.voltage_multiplier,
        .current_multiplier = cal.current_multiplier,
        .power_multiplier   = cal.power_multiplier,
    };
    hal_nvm_write(NV_ITEM_ENERGY_CALIBRATION, sizeof(nv), (uint8_t *)&nv);
}

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
    SETUP_ATTR(13, ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_VOLTAGE, ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
               cluster->calibrate_voltage);
    SETUP_ATTR(14, ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_CURRENT, ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
               cluster->calibrate_current);
    SETUP_ATTR(15, ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_POWER, ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
               cluster->calibrate_power);

    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_ELECTRICAL_MEASUREMENT;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 16;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->cluster_count++;
    cluster->endpoint = endpoint->endpoint;
    g_elec_cluster    = cluster;
    electrical_measurement_cluster_load_calibration(cluster);
}

void electrical_measurement_cluster_load_calibration(
    electrical_measurement_cluster_t *cluster) {
    if (!cluster || !cluster->meter)
        return;

    elec_meas_cal_nv_t nv;
    if (hal_nvm_read(NV_ITEM_ENERGY_CALIBRATION, sizeof(nv), (uint8_t *)&nv) !=
        HAL_NVM_SUCCESS)
        return;

    if (nv.magic != ELEC_MEAS_CAL_NV_MAGIC)
        return;

    // Overrides the compile-time / config_str defaults with the field-tuned
    // values (0 = keep, so a partially-calibrated device is fine).
    energy_meter_set_calibration(cluster->meter, nv.voltage_multiplier,
                                 nv.current_multiplier, nv.power_multiplier);
    printf("ElecMeas: restored calibration V=%u A=%u W=%u\r\n",
           nv.voltage_multiplier, nv.current_multiplier, nv.power_multiplier);
}

void electrical_measurement_cluster_callback_attr_write_trampoline(
    uint8_t endpoint, uint16_t attribute_id) {
    electrical_measurement_cluster_t *cluster = g_elec_cluster;

    if (!cluster || cluster->endpoint != endpoint || !cluster->meter)
        return;

    int       calibrated = -1;
    uint16_t *ref_field  = NULL;
    switch (attribute_id) {
    case ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_VOLTAGE:
        ref_field  = &cluster->calibrate_voltage;
        calibrated = energy_meter_calibrate(cluster->meter,
                                            ENERGY_METER_CHANNEL_VOLTAGE,
                                            cluster->calibrate_voltage);
        break;
    case ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_CURRENT:
        ref_field  = &cluster->calibrate_current;
        calibrated = energy_meter_calibrate(cluster->meter,
                                            ENERGY_METER_CHANNEL_CURRENT,
                                            cluster->calibrate_current);
        break;
    case ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_POWER:
        ref_field  = &cluster->calibrate_power;
        calibrated = energy_meter_calibrate(cluster->meter,
                                            ENERGY_METER_CHANNEL_POWER,
                                            cluster->calibrate_power);
        break;
    default:
        return;
    }

    // Clear the input so the field resets and a repeat calibration re-triggers.
    if (ref_field)
        *ref_field = 0;
    if (calibrated == 0)
        elec_meas_save_calibration(cluster);
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

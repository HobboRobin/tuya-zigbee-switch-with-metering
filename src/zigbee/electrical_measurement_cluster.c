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

// Append `value` as decimal digits at str+pos, returning the new position.
static uint8_t append_u32_dec(char *str, uint8_t pos, uint32_t value) {
    char    digits[10];
    uint8_t n = 0;

    do {
        digits[n++] = (char)('0' + (value % 10u));
        value      /= 10u;
    } while (value != 0);
    while (n > 0) {
        str[pos++] = digits[--n];
    }
    return pos;
}

// Rebuild the calibration_values string ("V<mult>A<mult>W<mult>") from the
// meter's active multipliers, so a read always returns the canonical form.
static void elec_meas_refresh_calibration_values(
    electrical_measurement_cluster_t *cluster) {
    energy_meter_calibration_t cal;
    uint8_t pos = 0;

    energy_meter_get_calibration(cluster->meter, &cal);
    cluster->calibration_values.str[pos++] = 'V';
    pos = append_u32_dec(cluster->calibration_values.str, pos,
                         cal.voltage_multiplier);
    cluster->calibration_values.str[pos++] = 'A';
    pos = append_u32_dec(cluster->calibration_values.str, pos,
                         cal.current_multiplier);
    cluster->calibration_values.str[pos++] = 'W';
    pos = append_u32_dec(cluster->calibration_values.str, pos,
                         cal.power_multiplier);
    cluster->calibration_values.len = pos;
}

// Parse "V<mult>A<mult>W<mult>" (any order, each channel optional; 0 or a
// missing channel keeps the current multiplier) and apply + persist it.
static void elec_meas_apply_calibration_values(
    electrical_measurement_cluster_t *cluster) {
    const char *str      = cluster->calibration_values.str;
    uint8_t     len      = cluster->calibration_values.len;
    uint32_t    mults[3] = { 0, 0, 0 }; // V, A, W

    if (len >= sizeof(cluster->calibration_values.str))
        len = sizeof(cluster->calibration_values.str) - 1;

    for (uint8_t i = 0; i < len; i++) {
        int8_t channel = -1;
        if (str[i] == 'V')
            channel = 0;
        else if (str[i] == 'A')
            channel = 1;
        else if (str[i] == 'W')
            channel = 2;

        if (channel < 0)
            continue;

        uint32_t value = 0;
        for (i++; i < len && str[i] >= '0' && str[i] <= '9'; i++) {
            value = value * 10u + (uint32_t)(str[i] - '0');
        }
        i--; // outer loop increments past the last digit
        mults[channel] = value;
    }

    energy_meter_set_calibration(cluster->meter, mults[0], mults[1], mults[2]);
    elec_meas_save_calibration(cluster);
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
    SETUP_ATTR(16, ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATION_VALUES, ZCL_DATA_TYPE_CHAR_STR,
               ATTR_WRITABLE,
               cluster->calibration_values);

    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_ELECTRICAL_MEASUREMENT;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 17;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->cluster_count++;
    cluster->endpoint = endpoint->endpoint;
    g_elec_cluster    = cluster;
    electrical_measurement_cluster_load_calibration(cluster);
    // Seed the readable multiplier string (defaults, config_str markers and
    // NVM-restored calibration are all applied by now).
    elec_meas_refresh_calibration_values(cluster);
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

    // Calibrate the addressed channel, then clear the input field so it resets
    // and a repeat calibration re-triggers. (The power reference is uint32, so
    // the fields are cleared per-case rather than through a shared pointer.)
    int calibrated = -1;
    switch (attribute_id) {
    case ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_VOLTAGE:
        calibrated = energy_meter_calibrate(cluster->meter,
                                            ENERGY_METER_CHANNEL_VOLTAGE,
                                            cluster->calibrate_voltage);
        cluster->calibrate_voltage = 0;
        break;
    case ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_CURRENT:
        calibrated = energy_meter_calibrate(cluster->meter,
                                            ENERGY_METER_CHANNEL_CURRENT,
                                            cluster->calibrate_current);
        cluster->calibrate_current = 0;
        break;
    case ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_POWER:
        calibrated = energy_meter_calibrate(cluster->meter,
                                            ENERGY_METER_CHANNEL_POWER,
                                            cluster->calibrate_power);
        cluster->calibrate_power = 0;
        break;
    case ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATION_VALUES:
        // Direct multiplier import (e.g. copied from a calibrated reference
        // device). Applies, persists, and rewrites the canonical string.
        elec_meas_apply_calibration_values(cluster);
        elec_meas_refresh_calibration_values(cluster);
        return;

    default:
        return;
    }

    if (calibrated == 0) {
        elec_meas_save_calibration(cluster);
        elec_meas_refresh_calibration_values(cluster);
    }
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

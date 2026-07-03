#include "basic_cluster.h"
#include "base_components/network_indicator.h"
#include "build_date.h"
#include "cluster_common.h"
#include "consts.h"
#include "device_config/config_nv.h"
#include "device_config/config_parser.h"
#include "device_config/device_params_nv.h"
#include "device_config/nvm_items.h"
#include "device_config/reset.h"
#include "hal/nvm.h"
#include "hal/tasks.h"
#include <stddef.h>

#ifdef HAL_SILABS
#include "silabs_config.h"
#endif

const uint8_t zclVersion   = 0x03;
const uint8_t appVersion   = 0x03;
const uint8_t stackVersion = 0x02;
const uint8_t hwVersion    = 0x00;

// Power source - set at runtime based on battery config
uint8_t powerSource = POWER_SOURCE_MAINS_1_PHASE; // 0x01 default

const uint16_t cluster_revision = 0x01;
// swBuildId carries an energy-monitoring runtime diagnostic suffix " E?M?":
// E<x> = haElectricalMeasurement registered in stack (1/0, '-' if disabled),
// M<y> = seMetering registered in stack. Filled in by
// basic_cluster_set_energy_diag() after the stack finishes registration.
//
// The " P0000R0000H00K00" suffix is a temporary BL0942 UART diagnostic,
// refreshed ~1x/second by basic_cluster_update_uart_diag():
//   P = poll commands sent, R = raw bytes received on UART RX,
//   H = frame headers (0x55) matched, K = checksum-valid frames.
// Read swBuildId (genBasic attr 0x4000) to see whether TX/RX/parsing works.
DEF_STR_NON_CONST(STRINGIFY_VALUE(VERSION_STR) " E?M? P0000R0000H00K00",
                  swBuildId);
extern network_indicator_t network_indicator;

void basic_cluster_store_attrs_to_nv();
void basic_cluster_load_attrs_from_nv();

// Set when the dedicated status/network LED is PWM-dimmable, so attribute
// writes can reach the cluster's brightness/transition storage.
static zigbee_basic_cluster *g_basic_cluster = NULL;

// The dedicated status LED is always the network indicator's first led (the
// config parser puts the L-configured led there).
static led_t *status_led(void) {
    if (!network_indicator.has_dedicated_led)
        return NULL;

    return network_indicator.leds[0];
}

typedef struct {
    uint8_t  brightness;
    uint16_t transition;
} net_led_dimming_nv_t;

static void basic_cluster_save_net_led_dimming(zigbee_basic_cluster *cluster) {
    net_led_dimming_nv_t nv = {
        .brightness = cluster->status_led_brightness,
        .transition = cluster->status_led_transition,
    };

    hal_nvm_write(NV_ITEM_NET_LED_DIMMING, sizeof(nv), (uint8_t *)&nv);
}

static void basic_cluster_load_net_led_dimming(zigbee_basic_cluster *cluster) {
    net_led_dimming_nv_t nv;

    if (hal_nvm_read(NV_ITEM_NET_LED_DIMMING, sizeof(nv), (uint8_t *)&nv) ==
        HAL_NVM_SUCCESS) {
        cluster->status_led_brightness = nv.brightness;
        cluster->status_led_transition = nv.transition;
    }
}

void basic_cluster_callback_attr_write_trampoline(uint16_t attribute_id) {
    basic_cluster_store_attrs_to_nv();
    if (attribute_id == ZCL_ATTR_BASIC_DEVICE_CONFIG) {
        device_config_str.data[device_config_str.size] =
            0;              // NULL terminate the string
        device_config_write_to_nv();
        schedule_reboot(0); // Use default delay
    }
    if (attribute_id == ZCL_ATTR_BASIC_STATUS_LED_STATE) {
        network_indicator_from_manual_state(&network_indicator);
    }
    if (attribute_id == ZCL_ATTR_BASIC_MULTI_PRESS_RESET_COUNT) {
        device_params_set_multi_press_reset_count(g_multi_press_reset_count);
    }
    if (g_basic_cluster != NULL && status_led() != NULL) {
        if (attribute_id == ZCL_ATTR_BASIC_STATUS_LED_BRIGHTNESS) {
            // Live dimming: applies immediately if the led is currently on.
            led_set_brightness(status_led(),
                               g_basic_cluster->status_led_brightness);
            basic_cluster_save_net_led_dimming(g_basic_cluster);
        } else if (attribute_id == ZCL_ATTR_BASIC_STATUS_LED_TRANSITION) {
            led_set_transition(status_led(),
                               g_basic_cluster->status_led_transition);
            basic_cluster_save_net_led_dimming(g_basic_cluster);
        }
    }
}

void basic_cluster_set_energy_diag(uint8_t energy_enabled, uint8_t elec_meas_ok,
                                   uint8_t metering_ok) {
    // Replace the two '?' markers in swBuildId (" E?M?") in order.
    char    em    = !energy_enabled ? '-' : (elec_meas_ok ? '1' : '0');
    char    met   = !energy_enabled ? '-' : (metering_ok ? '1' : '0');
    uint8_t which = 0;

    for (unsigned i = 0; i < sizeof(swBuildId.str); i++) {
        if (swBuildId.str[i] == '?') {
            swBuildId.str[i] = (which == 0) ? em : met;
            which++;
        }
    }
}

// Overwrite `width` decimal digits following the first occurrence of `marker`
// in swBuildId with the zero-padded low digits of `value`. Length is unchanged,
// so the ZCL string stays valid without re-reporting its size.
static void basic_cluster_write_dec_field(char marker, uint32_t value,
                                          uint8_t width) {
    for (unsigned i = 0; i + 1 + width <= sizeof(swBuildId.str); i++) {
        if (swBuildId.str[i] != marker) {
            continue;
        }
        for (int d = width - 1; d >= 0; d--) {
            swBuildId.str[i + 1 + d] = (char)('0' + (value % 10));
            value /= 10;
        }
        return;
    }
}

void basic_cluster_update_uart_diag(uint16_t polls, uint16_t rx_bytes,
                                    uint8_t headers, uint8_t checksums) {
    basic_cluster_write_dec_field('P', polls % 10000u, 4);
    basic_cluster_write_dec_field('R', rx_bytes % 10000u, 4);
    basic_cluster_write_dec_field('H', headers % 100u, 2);
    basic_cluster_write_dec_field('K', checksums % 100u, 2);
}

void basic_cluster_add_to_endpoint(zigbee_basic_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint) {
    // Set power source based on runtime battery configuration
    if (battery.pin != HAL_INVALID_PIN) {
        powerSource = POWER_SOURCE_BATTERY;
    }

    // Initialize build date buffer
    zb_build_date_init(ZB_BUILD_DATE_YYYYMMDD);

    // Fill Attrs

    SETUP_ATTR(0, ZCL_ATTR_BASIC_ZCL_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               zclVersion);

    SETUP_ATTR(1, ZCL_ATTR_BASIC_APP_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               appVersion);
    SETUP_ATTR(2, ZCL_ATTR_BASIC_STACK_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               stackVersion);
    SETUP_ATTR(3, ZCL_ATTR_BASIC_HW_VER, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               hwVersion);
    SETUP_ATTR(4, ZCL_ATTR_BASIC_MFR_NAME, ZCL_DATA_TYPE_CHAR_STR, ATTR_READONLY,
               cluster->manuName);
    SETUP_ATTR(5, ZCL_ATTR_BASIC_MODEL_ID, ZCL_DATA_TYPE_CHAR_STR, ATTR_READONLY,
               cluster->modelId);
    SETUP_ATTR(6, ZCL_ATTR_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8, ATTR_READONLY,
               powerSource);
    SETUP_ATTR(7, ZCL_ATTR_BASIC_DEV_ENABLED, ZCL_DATA_TYPE_BOOLEAN,
               ATTR_WRITABLE, cluster->deviceEnable);
    SETUP_ATTR(8, ZCL_ATTR_BASIC_SW_BUILD_ID, ZCL_DATA_TYPE_CHAR_STR,
               ATTR_READONLY, swBuildId);
    SETUP_ATTR(9, ZCL_ATTR_BASIC_DATE_CODE, ZCL_DATA_TYPE_CHAR_STR, ATTR_READONLY,
               ZB_BUILD_DATE_YYYYMMDD);
    SETUP_ATTR(10, ZCL_ATTR_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
               ATTR_READONLY, cluster_revision);
    SETUP_ATTR(11, ZCL_ATTR_BASIC_DEVICE_CONFIG, ZCL_DATA_TYPE_LONG_CHAR_STR,
               ATTR_WRITABLE, device_config_str);
    SETUP_ATTR(12, ZCL_ATTR_BASIC_MULTI_PRESS_RESET_COUNT, ZCL_DATA_TYPE_UINT8,
               ATTR_WRITABLE, g_multi_press_reset_count);
    uint8_t attr_count = 13;
    if (network_indicator.has_dedicated_led) {
        SETUP_ATTR(13, ZCL_ATTR_BASIC_STATUS_LED_STATE, ZCL_DATA_TYPE_BOOLEAN,
                   ATTR_WRITABLE, network_indicator.manual_state_when_connected);
        attr_count = 14;

        if (status_led()->dimmable) {
            // Seed from the led's defaults, then let NVM override.
            cluster->status_led_brightness = status_led()->brightness;
            cluster->status_led_transition = status_led()->transition_ms;
            basic_cluster_load_net_led_dimming(cluster);
            led_set_brightness(status_led(), cluster->status_led_brightness);
            led_set_transition(status_led(), cluster->status_led_transition);
            g_basic_cluster = cluster;

            SETUP_ATTR(14, ZCL_ATTR_BASIC_STATUS_LED_BRIGHTNESS,
                       ZCL_DATA_TYPE_UINT8, ATTR_WRITABLE,
                       cluster->status_led_brightness);
            SETUP_ATTR(15, ZCL_ATTR_BASIC_STATUS_LED_TRANSITION,
                       ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
                       cluster->status_led_transition);
            attr_count = 16;
        }
    }

    endpoint->clusters[endpoint->cluster_count].cluster_id      = ZCL_CLUSTER_BASIC;
    endpoint->clusters[endpoint->cluster_count].attribute_count = attr_count;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->cluster_count++;

    device_params_load_from_nv();
    basic_cluster_load_attrs_from_nv();
    if (hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED &&
        network_indicator.has_dedicated_led) {
        network_indicator_from_manual_state(&network_indicator);
    }
}

typedef struct {
    uint8_t network_led_on;
} zigbee_basic_cluster_config;

static zigbee_basic_cluster_config nv_config_buffer;

void basic_cluster_store_attrs_to_nv() {
    nv_config_buffer.network_led_on =
        network_indicator.manual_state_when_connected;

    hal_nvm_write(NV_ITEM_BASIC_CLUSTER_DATA, sizeof(zigbee_basic_cluster_config),
                  (uint8_t *)&nv_config_buffer);
}

void basic_cluster_load_attrs_from_nv() {
    hal_nvm_status_t st = hal_nvm_read(NV_ITEM_BASIC_CLUSTER_DATA,
                                       sizeof(zigbee_basic_cluster_config),
                                       (uint8_t *)&nv_config_buffer);

    if (st != HAL_NVM_SUCCESS) {
        return;
    }
    network_indicator.manual_state_when_connected =
        nv_config_buffer.network_led_on;
}

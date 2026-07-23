#include "hal/gpio.h"
#include "hal/printf_selector.h"
#include "hal/zigbee.h"
#include "zigbee/basic_cluster.h"
#include "zigbee/battery_cluster.h"
#include "zigbee/consts.h"
#include "zigbee/cover_cluster.h"
#include "zigbee/cover_switch_cluster.h"
#include "zigbee/group_cluster.h"
#include "zigbee/relay_cluster.h"
#include "zigbee/poll_control_cluster.h"
#include "zigbee/switch_cluster.h"
#include "base_components/energy_measurement/hlw8012.h"
#include "base_components/energy_measurement/bl0942.h"
#include "zigbee/electrical_measurement_cluster.h"
#include "zigbee/metering_cluster.h"

#include <stdint.h>
#include <string.h>

#include "base_components/led.h"
#include "base_components/network_indicator.h"
#include "base_components/battery.h"
#include "config_nv.h"
#include "device_config/device_params_nv.h"
#include "device_config/reset.h"
#include "hal/system.h"
#include "hal/zigbee.h"
#include "hal/zigbee_ota.h"

// Forward declarations
void peripherals_init(void);

// extern ota_preamble_t baseEndpoint_otaInfo;

network_indicator_t network_indicator = {
    .leds                        = { NULL, NULL, NULL, NULL },
    .has_dedicated_led           = 0,
    .manual_state_when_connected = 1,
};

led_t   leds[5];
uint8_t leds_cnt = 0;

button_t buttons[11];
uint8_t  buttons_cnt = 0;

relay_t relays[10]; // 4 relay endpoints + 3 cover endpoints
uint8_t relays_cnt = 0;

zigbee_basic_cluster basic_cluster = {
    .deviceEnable = 1,
};

zigbee_group_cluster group_cluster = {};

zigbee_switch_cluster switch_clusters[4];
uint8_t switch_clusters_cnt = 0;

// Up to 6 relay endpoints (e.g. the UseeLink 4-AC + USB strip has 5). One
// zigbee endpoint each, so this must stay within endpoints[]/clusters[] below.
zigbee_relay_cluster relay_clusters[6];
uint8_t relay_clusters_cnt = 0;

zigbee_cover_switch_cluster cover_switch_clusters[3];
uint8_t cover_switch_clusters_cnt = 0;

zigbee_cover_cluster cover_clusters[3];
uint8_t cover_clusters_cnt = 0;

// Sized for the largest supported layout, including the optional per-switch
// long-press binding endpoints (2EP token): a 4-gang switch with 2EP uses
// 4 switch + 4 relay + 4 long-press = 12 endpoints.
hal_zigbee_cluster  clusters[40];
hal_zigbee_endpoint endpoints[12];

uint8_t allow_simultaneous_latching_pulses = 0;

// `2EP` token: give every switch a companion long-press binding endpoint.
uint8_t long_press_bind_endpoints = 0;

battery_t battery = {
    .pin         = HAL_INVALID_PIN,
    .voltage_min =            2000,
    .voltage_max =            3000,
};

uint32_t parse_int(const char *s);
char *seek_until(char *cursor, char needle);
char *extract_next_entry(char **cursor);

// Parse the optional flags after an L/I LED pin (starting at entry+3):
//   'i' inverts the output (active-low), 'p' makes it PWM-dimmable.
// The PWM channel itself is chosen by the HAL in led_init(): on TLSR8258 each
// pin supports at most one specific PWM function (pin mux table).
static void led_apply_flags(led_t *led, const char *flags) {
    led->on_high = (*seek_until((char *)flags, 'i') != 'i');
    if (*seek_until((char *)flags, 'p') == 'p') {
        led->dimmable   = 1;
        led->brightness = 255;
    }
}

static hlw8012_t       hlw8012_device;
static bl0942_t        bl0942_device;
static energy_meter_t *energy_meter = NULL;
static electrical_measurement_cluster_t elec_meas_cluster;
static metering_cluster_t metering_cluster_inst;
static uint8_t            energy_monitoring_enabled  = 0;
static uint8_t            energy_monitoring_endpoint = 1;

void on_reset_clicked(void *_) {
    hal_factory_reset();
}

void on_multi_press_reset(void *_, uint8_t press_count) {
    if (g_multi_press_reset_count != 0 &&
        press_count >= g_multi_press_reset_count) {
        hal_factory_reset();
    }
}

void parse_config() {
    device_config_read_from_nv();
    char *cursor = (char *)device_config_str.data;

    const char *zb_manufacturer = extract_next_entry(&cursor);

    basic_cluster.manuName[0] = strlen(zb_manufacturer);
    if (basic_cluster.manuName[0] > 31) {
        printf("Manufacturer too big\r\n");
        reset_all();
    }
    memcpy(basic_cluster.manuName + 1, zb_manufacturer,
           basic_cluster.manuName[0]);

    const char *zb_model = extract_next_entry(&cursor);
    basic_cluster.modelId[0] = strlen(zb_model);
    if (basic_cluster.modelId[0] > 31) {
        printf("Model too big\r\n");
        reset_all();
    }
    memcpy(basic_cluster.modelId + 1, zb_model, basic_cluster.modelId[0]);

    bool     has_dedicated_status_led = false;
    uint16_t debounce_ms = DEBOUNCE_DELAY_MS;
    char *   entry;
    for (entry = extract_next_entry(&cursor); *entry != '\0';
         entry = extract_next_entry(&cursor)) {
        if (entry[0] == 'S' && entry[1] == 'L' && entry[2] == 'P') {
            // Simultaneous Latching Pulses == SLP
            allow_simultaneous_latching_pulses = 1;
        } else if (entry[0] == '2' && entry[1] == 'E' && entry[2] == 'P') {
            // 2EP: add a per-switch long-press binding endpoint.
            long_press_bind_endpoints = 1;
        } else if (entry[0] == 'D' && entry[1] >= '0' && entry[1] <= '9') {
            // D<N> sets the global debounce duration in milliseconds.
            debounce_ms = (uint16_t)parse_int(entry + 1);
            for (int i = 0; i < buttons_cnt; i++) {
                buttons[i].debounce_delay_ms = debounce_ms;
            }
        } else if (entry[0] == 'B' && entry[1] == 'T') {
            // Battery: BT<pin>, e.g. BTC5
            hal_gpio_pin_t pin = hal_gpio_parse_pin(entry + 2);
            battery.pin = pin;
            battery_init(&battery);
        } else if (entry[0] == 'B') {
            hal_gpio_pin_t  pin  = hal_gpio_parse_pin(entry + 1);
            hal_gpio_pull_t pull = hal_gpio_parse_pull(entry + 3);
            hal_gpio_init(pin, 1, pull);

            buttons[buttons_cnt].pin = pin;
            buttons[buttons_cnt].long_press_duration_ms  = 2000;
            buttons[buttons_cnt].multi_press_duration_ms = 800;
            buttons[buttons_cnt].debounce_delay_ms       = debounce_ms;
            buttons[buttons_cnt].on_long_press           = on_reset_clicked;
            buttons_cnt++;
        } else if (entry[0] == 'L') {
            hal_gpio_pin_t pin = hal_gpio_parse_pin(entry + 1);
            hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);
            leds[leds_cnt].pin = pin;
            led_apply_flags(&leds[leds_cnt], entry + 3);

            led_init(&leds[leds_cnt]);

            network_indicator.leds[0]           = &leds[leds_cnt];
            network_indicator.leds[1]           = NULL;
            network_indicator.has_dedicated_led = true;

            has_dedicated_status_led = true;
            leds_cnt++;
        } else if (entry[0] == 'I') {
            hal_gpio_pin_t pin = hal_gpio_parse_pin(entry + 1);
            hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);
            leds[leds_cnt].pin = pin;
            led_apply_flags(&leds[leds_cnt], entry + 3);
            led_init(&leds[leds_cnt]);

            for (int index = 0; index < relay_clusters_cnt; index++) {
                if (relay_clusters[index].indicator_led == NULL) {
                    relay_clusters[index].indicator_led = &leds[leds_cnt];
                    break;
                }
            }

            for (int index = 0; index < 4; index++) {
                if (switch_clusters[index].indicator_led == NULL) {
                    switch_clusters[index].indicator_led = &leds[leds_cnt];
                    break;
                }
            }

            if (!has_dedicated_status_led) {
                for (int index = 0; index < 4; index++) {
                    if (network_indicator.leds[index] == NULL) {
                        network_indicator.leds[index] = &leds[leds_cnt];
                        break;
                    }
                }
            }
            leds_cnt++;
        } else if (entry[0] == 'S') {
            hal_gpio_pin_t  pin  = hal_gpio_parse_pin(entry + 1);
            hal_gpio_pull_t pull = hal_gpio_parse_pull(entry + 3);
            hal_gpio_init(pin, 1, pull);

            buttons[buttons_cnt].pin = pin;
            buttons[buttons_cnt].long_press_duration_ms  = 800;
            buttons[buttons_cnt].multi_press_duration_ms = 800;
            buttons[buttons_cnt].debounce_delay_ms       = debounce_ms;
            buttons[buttons_cnt].on_multi_press          = on_multi_press_reset;

            if (entry[3] == 'd')
                buttons[buttons_cnt].pressed_when_high = 1;
            switch_clusters[switch_clusters_cnt].switch_idx = switch_clusters_cnt;
            switch_clusters[switch_clusters_cnt].mode       =
                ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE;
            switch_clusters[switch_clusters_cnt].action =
                ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE;
            switch_clusters[switch_clusters_cnt].relay_mode =
                ZCL_ONOFF_CONFIGURATION_RELAY_MODE_SHORT;
            switch_clusters[switch_clusters_cnt].binded_mode =
                ZCL_ONOFF_CONFIGURATION_BINDED_MODE_SHORT;
            switch_clusters[switch_clusters_cnt].relay_index     = switch_clusters_cnt + 1;
            switch_clusters[switch_clusters_cnt].button          = &buttons[buttons_cnt];
            switch_clusters[switch_clusters_cnt].level_move_rate = 50;
            buttons_cnt++;
            switch_clusters_cnt++;
        } else if (entry[0] == 'R') {
            hal_gpio_pin_t pin = hal_gpio_parse_pin(entry + 1);
            hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);

            relays[relays_cnt].pin     = pin;
            relays[relays_cnt].on_high = 1;

            if (entry[3] == 'i') {
                // R<pin>i: active-low (inverted) monostable relay - the pin is
                // driven low to switch the relay on (same 'i' convention as LEDs).
                relays[relays_cnt].on_high = 0;
            } else if (entry[3] != '\0') {
                pin = hal_gpio_parse_pin(entry + 3);
                hal_gpio_init(pin, 0, HAL_GPIO_PULL_NONE);
                relays[relays_cnt].off_pin     = pin;
                relays[relays_cnt].is_latching = 1;
            }

            relay_clusters[relay_clusters_cnt].relay_idx = relay_clusters_cnt;
            relay_clusters[relay_clusters_cnt].relay     = &relays[relays_cnt];

            relays_cnt++;
            relay_clusters_cnt++;
        } else if (entry[0] == 'X') {
            hal_gpio_pin_t  open_pin  = hal_gpio_parse_pin(entry + 1);
            hal_gpio_pin_t  close_pin = hal_gpio_parse_pin(entry + 3);
            hal_gpio_pull_t pull      = hal_gpio_parse_pull(entry + 5);

            hal_gpio_init(open_pin, 1, pull);
            hal_gpio_init(close_pin, 1, pull);

            buttons[buttons_cnt].pin = open_pin;
            buttons[buttons_cnt].long_press_duration_ms  = 800;
            buttons[buttons_cnt].multi_press_duration_ms = 800;
            buttons[buttons_cnt].debounce_delay_ms       = debounce_ms;
            buttons[buttons_cnt].on_multi_press          = on_multi_press_reset;
            button_t *open_button = &buttons[buttons_cnt++];

            buttons[buttons_cnt].pin = close_pin;
            buttons[buttons_cnt].long_press_duration_ms  = 800;
            buttons[buttons_cnt].multi_press_duration_ms = 800;
            buttons[buttons_cnt].debounce_delay_ms       = debounce_ms;
            buttons[buttons_cnt].on_multi_press          = on_multi_press_reset;
            button_t *close_button = &buttons[buttons_cnt++];

            cover_switch_clusters[cover_switch_clusters_cnt].open_button =
                open_button;
            cover_switch_clusters[cover_switch_clusters_cnt].close_button =
                close_button;
            cover_switch_clusters[cover_switch_clusters_cnt].cover_switch_idx =
                cover_switch_clusters_cnt;
            cover_switch_clusters_cnt++;
        } else if (entry[0] == 'C') {
            hal_gpio_pin_t open_pin  = hal_gpio_parse_pin(entry + 1);
            hal_gpio_pin_t close_pin = hal_gpio_parse_pin(entry + 3);

            hal_gpio_init(open_pin, 0, HAL_GPIO_PULL_NONE);
            hal_gpio_init(close_pin, 0, HAL_GPIO_PULL_NONE);

            relays[relays_cnt].pin         = open_pin;
            relays[relays_cnt].on_high     = 1;
            relays[relays_cnt].is_latching = 0;
            relay_t *open_relay = &relays[relays_cnt++];

            relays[relays_cnt].pin         = close_pin;
            relays[relays_cnt].on_high     = 1;
            relays[relays_cnt].is_latching = 0;
            relay_t *close_relay = &relays[relays_cnt++];

            cover_clusters[cover_clusters_cnt].open_relay  = open_relay;
            cover_clusters[cover_clusters_cnt].close_relay = close_relay;
            cover_clusters[cover_clusters_cnt].cover_idx   = cover_clusters_cnt;
            cover_clusters_cnt++;
        } else if (entry[0] == 'i') {
            uint32_t image_type = parse_int(entry + 1);
            hal_zigbee_set_image_type(image_type);
        } else if (entry[0] == 'M') {
            for (int index = 0; index < switch_clusters_cnt; index++) {
                switch_clusters[index].mode =
                    ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY;
            }
        } else if (entry[0] == 'E' && entry[1] == 'P') {
            // HLW8012/BL0937 energy monitoring:
            //   EP<CF_PIN><CF1_PIN><SEL_PIN>[V<volt_mult>][A<curr_mult>][W<pow_mult>]
            // Pins are 2 chars each (port letter + digit). The optional V/A/W
            // markers (decimal, any order, after the pins) override the
            // compiled-in calibration multipliers, so a board revision with a
            // different sense resistor/divider can be calibrated from the
            // config_str without a firmware rebuild.
            printf("Config: Found energy monitoring entry: '%s'\r\n", entry);
            hal_gpio_pin_t cf_pin  = hal_gpio_parse_pin(entry + 2);
            hal_gpio_pin_t cf1_pin = hal_gpio_parse_pin(entry + 4);
            hal_gpio_pin_t sel_pin = hal_gpio_parse_pin(entry + 6);
            if (cf_pin != HAL_INVALID_PIN && cf1_pin != HAL_INVALID_PIN &&
                sel_pin != HAL_INVALID_PIN) {
                if (hlw8012_init(&hlw8012_device, cf_pin, cf1_pin, sel_pin) == 0) {
                    // Pins occupy the fixed first 6 chars after "EP"; any
                    // calibration markers follow from entry + 8 onwards.
                    const char *cal = entry + 8;
                    const char *v   = seek_until((char *)cal, 'V');
                    const char *a   = seek_until((char *)cal, 'A');
                    const char *w   = seek_until((char *)cal, 'W');
                    hlw8012_set_calibration(
                        &hlw8012_device,
                        (*v == 'V') ? parse_int(v + 1) : 0,
                        (*a == 'A') ? parse_int(a + 1) : 0,
                        (*w == 'W') ? parse_int(w + 1) : 0);
                    energy_meter = hlw8012_as_energy_meter(&hlw8012_device);
                    electrical_measurement_cluster_init(&elec_meas_cluster, energy_meter);
                    metering_cluster_init(&metering_cluster_inst, energy_meter);
                    energy_monitoring_enabled  = 1;
                    energy_monitoring_endpoint = 1;
                    printf("Config: HLW8012 on CF=%04x CF1=%04x SEL=%04x\r\n",
                           cf_pin, cf1_pin, sel_pin);
                }
            }
        } else if (entry[0] == 'E' && entry[1] == 'B') {
            // BL0942 UART energy monitoring:
            //   EB<TX_PIN><RX_PIN>[S<baud>][V<volt_mult>][A<curr_mult>][W<pow_mult>]
            // TX/RX are from the MCU's point of view, 2 chars each. The
            // optional S marker overrides the 4800 default baudrate; V/A/W
            // override the calibration multipliers, all editable from the
            // config_str without a rebuild.
            printf("Config: Found energy monitoring entry: '%s'\r\n", entry);
            hal_gpio_pin_t tx_pin = hal_gpio_parse_pin(entry + 2);
            hal_gpio_pin_t rx_pin = hal_gpio_parse_pin(entry + 4);
            if (tx_pin != HAL_INVALID_PIN && rx_pin != HAL_INVALID_PIN) {
                const char *opt  = entry + 6;
                const char *s    = seek_until((char *)opt, 'S');
                uint32_t    baud =
                    (*s == 'S') ? parse_int(s + 1) : BL0942_DEFAULT_BAUDRATE;
                if (bl0942_init(&bl0942_device, tx_pin, rx_pin, baud) == 0) {
                    const char *v = seek_until((char *)opt, 'V');
                    const char *a = seek_until((char *)opt, 'A');
                    const char *w = seek_until((char *)opt, 'W');
                    bl0942_set_calibration(
                        &bl0942_device,
                        (*v == 'V') ? parse_int(v + 1) : 0,
                        (*a == 'A') ? parse_int(a + 1) : 0,
                        (*w == 'W') ? parse_int(w + 1) : 0);
                    energy_meter = bl0942_as_energy_meter(&bl0942_device);
                    electrical_measurement_cluster_init(&elec_meas_cluster, energy_meter);
                    metering_cluster_init(&metering_cluster_inst, energy_meter);
                    energy_monitoring_enabled  = 1;
                    energy_monitoring_endpoint = 1;
                    printf("Config: BL0942 on TX=%04x RX=%04x\r\n", tx_pin,
                           rx_pin);
                }
            }
        } else if (entry[0] == 'O' && entry[1] == 'L') {
            // Overload limits: OL[C<soft_mA>][P<peak_mA>]
            // Sets the device's rated continuous (soft) and peak (hard) current
            // caps; the matching wattages are derived at the nominal mains
            // voltage. Must appear after the EP/EB token so the meter cluster is
            // already initialised. Only meaningful with energy monitoring.
            if (energy_monitoring_enabled) {
                const char *c = seek_until((char *)(entry + 2), 'C');
                const char *p = seek_until((char *)(entry + 2), 'P');
                overload_protection_set_current_limits(
                    &elec_meas_cluster.overload,
                    (*c == 'C') ? (uint16_t)parse_int(c + 1) : 0,
                    (*p == 'P') ? (uint16_t)parse_int(p + 1) : 0);
                printf("Config: overload limits soft=%umA peak=%umA\r\n",
                       elec_meas_cluster.overload.cfg.current_limit_ma,
                       elec_meas_cluster.overload.cfg.hard_current_ma);
            }
        }
    }

    peripherals_init();

    printf("Initializing Zigbee with %d switches, %d relays, %d cover switches, "
           "%d covers\r\n",
           switch_clusters_cnt, relay_clusters_cnt, cover_switch_clusters_cnt,
           cover_clusters_cnt);

    // Each switch gets a trailing long-press binding endpoint when 2EP is set.
    uint8_t long_press_ep_cnt =
        long_press_bind_endpoints ? switch_clusters_cnt : 0;

    uint8_t total_endpoints = switch_clusters_cnt + relay_clusters_cnt +
                              cover_switch_clusters_cnt + cover_clusters_cnt +
                              long_press_ep_cnt;

    hal_zigbee_cluster *cluster_ptr = clusters;

    for (int index = 0; index < switch_clusters_cnt; index++) {
        if (switch_clusters[index].relay_index > relay_clusters_cnt) {
            // Detach switches that point past the available relay count.
            switch_clusters[index].relay_mode =
                ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED;
            switch_clusters[index].relay_index = 0;
        }
    }

    // special case when no switches or relays are defined, so we can init a
    // "clean" device and configure it while running endpoint 1 still needs to be
    // initialised even though wenn no switches or relays are defined, so it can
    // join the network!
    if (total_endpoints == 0)
        total_endpoints = 1;

    for (int index = 0; index < total_endpoints; index++) {
        endpoints[index].endpoint   = index + 1;
        endpoints[index].profile_id = 0x0104;
        endpoints[index].device_id  = 0xffff;
    }

    endpoints[0].clusters = cluster_ptr;
    basic_cluster_add_to_endpoint(&basic_cluster, &endpoints[0]);

    hal_ota_cluster_setup(&endpoints[0].clusters[endpoints[0].cluster_count]);
    endpoints[0].cluster_count++;

    // Add battery cluster for battery-powered devices
    if (battery.pin != HAL_INVALID_PIN) {
        static zigbee_battery_cluster battery_cluster;
        battery_cluster_add_to_endpoint(&battery_cluster, &endpoints[0]);
    }

#ifdef END_DEVICE
    // Add poll control cluster for end devices
    static zigbee_poll_control_cluster poll_ctrl_cluster;
    poll_control_cluster_add_to_endpoint(&poll_ctrl_cluster, &endpoints[0],
                                         battery.pin != HAL_INVALID_PIN);
#endif

    for (int index = 0; index < switch_clusters_cnt; index++) {
        if (index != 0) {
            cluster_ptr += endpoints[index - 1].cluster_count;
            endpoints[index].clusters = cluster_ptr;
        }
        switch_cluster_add_to_endpoint(&switch_clusters[index], &endpoints[index]);
    }

    // Add energy measurement clusters to EP1 before the relay loop so that
    // the relay loop's cluster_ptr arithmetic sees the final EP1 cluster count.
    if (energy_monitoring_enabled && energy_monitoring_endpoint == 1) {
        electrical_measurement_cluster_add_to_endpoint(
            &elec_meas_cluster, &endpoints[0]);
        metering_cluster_add_to_endpoint(
            &metering_cluster_inst, &endpoints[0]);
    }

    for (int index = 0; index < relay_clusters_cnt; index++) {
        if (switch_clusters_cnt + index != 0) {
            cluster_ptr += endpoints[switch_clusters_cnt + index - 1].cluster_count;
            endpoints[switch_clusters_cnt + index].clusters = cluster_ptr;
        }
        relay_cluster_add_to_endpoint(&relay_clusters[index],
                                      &endpoints[switch_clusters_cnt + index]);
        // Group cluster is stateless, safe to add to multiple endpoints
        group_cluster_add_to_endpoint(&group_cluster,
                                      &endpoints[switch_clusters_cnt + index]);
    }

    // Overload protection guards the first relay on the metering device.
    if (energy_monitoring_enabled && relay_clusters_cnt > 0) {
        electrical_measurement_cluster_set_protected_relay(&elec_meas_cluster,
                                                           &relay_clusters[0]);
    }

    int cover_switch_base = switch_clusters_cnt + relay_clusters_cnt;
    for (int index = 0; index < cover_switch_clusters_cnt; index++) {
        if (cover_switch_base + index != 0) {
            cluster_ptr += endpoints[cover_switch_base + index - 1].cluster_count;
            endpoints[cover_switch_base + index].clusters = cluster_ptr;
        }
        cover_switch_cluster_add_to_endpoint(&cover_switch_clusters[index],
                                             &endpoints[cover_switch_base + index]);
    }

    int cover_base =
        switch_clusters_cnt + relay_clusters_cnt + cover_switch_clusters_cnt;
    for (int index = 0; index < cover_clusters_cnt; index++) {
        if (cover_base + index != 0) {
            cluster_ptr += endpoints[cover_base + index - 1].cluster_count;
            endpoints[cover_base + index].clusters = cluster_ptr;
        }
        cover_cluster_add_to_endpoint(&cover_clusters[index],
                                      &endpoints[cover_base + index]);
    }

    // Add energy measurement clusters to endpoints > 1 (EP1 case handled before relay loop)
    if (energy_monitoring_enabled && energy_monitoring_endpoint > 1) {
        electrical_measurement_cluster_add_to_endpoint(
            &elec_meas_cluster, &endpoints[energy_monitoring_endpoint - 1]);
        metering_cluster_add_to_endpoint(
            &metering_cluster_inst, &endpoints[energy_monitoring_endpoint - 1]);
    }

    // Long-press binding endpoints (2EP): one trailing endpoint per switch,
    // each carrying just an OnOff *client* cluster so it can be bound in Z2M.
    // A long press toggles this endpoint's own bindings (see
    // switch_cluster_on_button_long_press), so short and long press can drive
    // two independent targets.
    int long_press_base = switch_clusters_cnt + relay_clusters_cnt +
                          cover_switch_clusters_cnt + cover_clusters_cnt;
    for (int index = 0; index < long_press_ep_cnt; index++) {
        int ep_i = long_press_base + index;
        cluster_ptr += endpoints[ep_i - 1].cluster_count;
        endpoints[ep_i].clusters = cluster_ptr;
        endpoints[ep_i].clusters[0].cluster_id      = ZCL_CLUSTER_ON_OFF;
        endpoints[ep_i].clusters[0].attribute_count = 0;
        endpoints[ep_i].clusters[0].attributes      = NULL;
        endpoints[ep_i].clusters[0].is_server       = 0;
        endpoints[ep_i].cluster_count = 1;
        switch_clusters[index].long_press_endpoint = endpoints[ep_i].endpoint;
    }

    hal_zigbee_init(endpoints, total_endpoints);

    // Record whether the energy clusters actually registered in the stack, so
    // it can be read back from genBasic swBuildId for diagnostics.
    basic_cluster_set_energy_diag(
        energy_monitoring_enabled,
        hal_zigbee_stack_has_attribute(energy_monitoring_endpoint,
                                       ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
                                       ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE),
        hal_zigbee_stack_has_attribute(
            energy_monitoring_endpoint, ZCL_CLUSTER_METERING,
            ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED));

    while (cursor != (char *)device_config_str.data) {
        cursor--;
        if (*cursor == '\0') {
            *cursor = ';';
        }
    }

    printf("Config parsed successfully\r\n");
}

void network_indicator_on_network_status_change(
    hal_zigbee_network_status_t new_status) {
    printf("Network status changed to %d\r\n", new_status);
    if (new_status == HAL_ZIGBEE_NETWORK_JOINED) {
        if (battery.pin != HAL_INVALID_PIN) {
            network_indicator.manual_state_when_connected = 0;
        }
        network_indicator_connected(&network_indicator);
        update_switch_clusters();
        update_relay_clusters();
    } else {
        network_indicator_not_connected(&network_indicator);
    }
}

void peripherals_init() {
    for (int index = 0; index < buttons_cnt; index++) {
        btn_init(&buttons[index]);
    }
    for (int index = 0; index < leds_cnt; index++) {
        led_init(&leds[index]);
    }
    for (int index = 0; index < relays_cnt; index++) {
        relay_init(&relays[index]);
    }
    if (hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED) {
        network_indicator_connected(&network_indicator);
        update_switch_clusters();
        update_relay_clusters();
    } else {
        network_indicator_not_connected(&network_indicator);
    }
    hal_register_on_network_status_change_callback(
        network_indicator_on_network_status_change);
}

// Helper functions

char *seek_until(char *cursor, char needle) {
    while (*cursor != needle && *cursor != '\0') {
        cursor++;
    }
    return(cursor);
}

char *extract_next_entry(char **cursor) {
    char *end = seek_until(*cursor, ';');

    *end = '\0';
    char *res = *cursor;
    *cursor = end + 1;
    return(res);
}

uint32_t parse_int(const char *s) {
    if (!s)
        return 0;

    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return n;
}

void init_energy_reporting(void) {
    if (!energy_monitoring_enabled)
        return;

    electrical_measurement_cluster_update(&elec_meas_cluster);
    electrical_measurement_cluster_report(&elec_meas_cluster);
    metering_cluster_update(&metering_cluster_inst);
    metering_cluster_report(&metering_cluster_inst);
}

uint8_t get_energy_monitoring_enabled(void) {
    return energy_monitoring_enabled;
}

void energy_monitoring_tick(void) {
    if (!energy_monitoring_enabled)
        return;

    hlw8012_tick(&hlw8012_device);
    // Refresh the measurement mirror and run overload protection every tick,
    // independent of network join, so the relay is protected even offline.
    electrical_measurement_cluster_update(&elec_meas_cluster);
}

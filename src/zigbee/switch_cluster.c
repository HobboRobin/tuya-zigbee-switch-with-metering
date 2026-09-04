#include "switch_cluster.h"
#include "base_components/relay.h"
#include "cluster_common.h"
#include "consts.h"
#include "device_config/nvm_items.h"
#include "hal/nvm.h"

#include "hal/printf_selector.h"
#include "hal/system.h"
#include "hal/tasks.h"
#include "light_cluster.h"
#include "relay_cluster.h"
#include "zigbee_commands.h"

const uint8_t  multistate_out_of_service = 0;
const uint8_t  multistate_flags          = 0;
const uint16_t multistate_num_of_states  = 3;

#define MULTISTATE_NOT_PRESSED     0
#define MULTISTATE_PRESS           1
#define MULTISTATE_LONG_PRESS      2
#define MULTISTATE_POSITION_ON     3
#define MULTISTATE_POSITION_OFF    4

extern zigbee_relay_cluster relay_clusters[];
extern uint8_t relay_clusters_cnt;
extern zigbee_light_cluster light_clusters[];
extern uint8_t light_clusters_cnt;
extern zigbee_switch_cluster switch_clusters[];
extern uint8_t switch_clusters_cnt;

void switch_cluster_on_button_press(zigbee_switch_cluster *cluster);
void switch_cluster_on_button_release(zigbee_switch_cluster *cluster);
void switch_cluster_on_button_long_press(zigbee_switch_cluster *cluster);
static bool switch_cluster_has_valid_relay(
    const zigbee_switch_cluster *cluster);

zigbee_switch_cluster *switch_cluster_by_endpoint[12];

static void sync_switch_indicator_led(zigbee_switch_cluster *cluster) {
    if (cluster->indicator_led == NULL) {
        return;
    }

    if (cluster->relay_mode != ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED &&
        switch_cluster_has_valid_relay(cluster)) {
        return;
    }

    led_off(cluster->indicator_led);
}

void update_switch_clusters() {
    for (int i = 0; i < switch_clusters_cnt; i++) {
        sync_switch_indicator_led(&switch_clusters[i]);
    }
}

// A switch drives one of the device's local outputs, numbered from 1: the
// relays first, then the lights. A light is just another thing to switch on and
// off, so a device with no relays at all - a lamp - can still work its own
// outputs from its buttons instead of only through a binding.
uint8_t switch_cluster_output_cnt(void) {
    return relay_clusters_cnt + light_clusters_cnt;
}

static bool switch_cluster_output_is_on(uint8_t index) {
    if (index <= relay_clusters_cnt) {
        return relay_clusters[index - 1].relay->on != 0;
    }
    return light_clusters[index - relay_clusters_cnt - 1].on != 0;
}

static void switch_cluster_output_set(uint8_t index, bool on) {
    if (index <= relay_clusters_cnt) {
        on ? relay_cluster_on(&relay_clusters[index - 1])
           : relay_cluster_off(&relay_clusters[index - 1]);
        return;
    }
    zigbee_light_cluster *light = &light_clusters[index - relay_clusters_cnt - 1];
    on ? light_cluster_on(light) : light_cluster_off(light);
}

static void switch_cluster_output_toggle(uint8_t index) {
    if (index <= relay_clusters_cnt) {
        // Relays keep their own toggle: a latching relay has to pulse rather
        // than be driven to a level.
        relay_cluster_toggle(&relay_clusters[index - 1]);
        return;
    }
    light_cluster_toggle(&light_clusters[index - relay_clusters_cnt - 1]);
}

static bool switch_cluster_targets_all(const zigbee_switch_cluster *cluster) {
    return cluster->relay_index == SWITCH_RELAY_INDEX_ALL &&
           switch_cluster_output_cnt() > 0;
}

static bool switch_cluster_has_valid_relay(const zigbee_switch_cluster *cluster) {
    return switch_cluster_targets_all(cluster) ||
           (cluster->relay_index > 0 &&
            cluster->relay_index <= switch_cluster_output_cnt());
}

// Is the switch's target on? With `all` that means "is anything on", which is
// what makes a master button unambiguous when the outputs disagree: any output
// on counts as on, so the next press turns everything off.
static bool switch_cluster_target_is_on(const zigbee_switch_cluster *cluster) {
    if (switch_cluster_targets_all(cluster)) {
        for (uint8_t i = 1; i <= switch_cluster_output_cnt(); i++) {
            if (switch_cluster_output_is_on(i)) {
                return true;
            }
        }
        return false;
    }
    return switch_cluster_output_is_on(cluster->relay_index);
}

static void switch_cluster_target_set(zigbee_switch_cluster *cluster, bool on) {
    if (!switch_cluster_targets_all(cluster)) {
        switch_cluster_output_set(cluster->relay_index, on);
        return;
    }
    for (uint8_t i = 1; i <= switch_cluster_output_cnt(); i++) {
        switch_cluster_output_set(i, on);
    }
}

// Toggling `all` is resolved as a group rather than per output: anything on
// means everything goes off, otherwise everything goes on. Toggling each output
// on its own would only deepen a mixed state instead of levelling it.
static void switch_cluster_target_toggle(zigbee_switch_cluster *cluster) {
    if (!switch_cluster_targets_all(cluster)) {
        switch_cluster_output_toggle(cluster->relay_index);
        return;
    }
    switch_cluster_target_set(cluster, !switch_cluster_target_is_on(cluster));
}

static void switch_cluster_flash_indicator(zigbee_switch_cluster *cluster) {
    if (cluster->indicator_led == NULL) {
        return;
    }
    // Skip flash when relay is attached — the relay toggle itself changes the
    // indicator, and the blink would race with sync_indicator_led.
    if (cluster->relay_mode != ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED &&
        switch_cluster_has_valid_relay(cluster)) {
        return;
    }
    // Only flash when LED is idle (not in "not connected" forever-blink)
    if (cluster->indicator_led->blink_times_left == 0) {
        led_blink(cluster->indicator_led, 50, 50, 1);
    }
}

void switch_cluster_store_attrs_to_nv(zigbee_switch_cluster *cluster);
void switch_cluster_load_attrs_from_nv(zigbee_switch_cluster *cluster);
void switch_cluster_store_multi_press_reset_to_nv(zigbee_switch_cluster *cluster);
void switch_cluster_load_multi_press_reset_from_nv(zigbee_switch_cluster *cluster);
void switch_cluster_on_write_attr(zigbee_switch_cluster *cluster,
                                  uint16_t attribute_id);

void switch_cluster_report_action(zigbee_switch_cluster *cluster);

void switch_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                   uint16_t attribute_id) {
    switch_cluster_on_write_attr(switch_cluster_by_endpoint[endpoint],
                                 attribute_id);
}

void switch_cluster_add_to_endpoint(zigbee_switch_cluster *cluster,
                                    hal_zigbee_endpoint *endpoint) {
    switch_cluster_by_endpoint[endpoint->endpoint] = cluster;
    cluster->endpoint = endpoint->endpoint;
    switch_cluster_load_attrs_from_nv(cluster);
    switch_cluster_load_multi_press_reset_from_nv(cluster);

    cluster->button->on_press =
        (ev_button_callback_t)switch_cluster_on_button_press;
    cluster->button->on_release =
        (ev_button_callback_t)switch_cluster_on_button_release;
    cluster->button->on_long_press =
        (ev_button_callback_t)switch_cluster_on_button_long_press;
    cluster->button->callback_param = cluster;

    SETUP_ATTR(0, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_TYPE, ZCL_DATA_TYPE_ENUM8,
               ATTR_READONLY, cluster->mode);
    SETUP_ATTR(1, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_ACTIONS,
               ZCL_DATA_TYPE_ENUM8, ATTR_WRITABLE, cluster->action);
    SETUP_ATTR(2, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE, ZCL_DATA_TYPE_ENUM8,
               ATTR_WRITABLE, cluster->mode);
    SETUP_ATTR(3, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_MODE,
               ZCL_DATA_TYPE_ENUM8, ATTR_WRITABLE, cluster->relay_mode);
    SETUP_ATTR(4, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX,
               ZCL_DATA_TYPE_UINT8, ATTR_WRITABLE, cluster->relay_index);
    SETUP_ATTR(5, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LONG_PRESS_DUR,
               ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
               cluster->button->long_press_duration_ms);
    SETUP_ATTR(6, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_LEVEL_MOVE_RATE,
               ZCL_DATA_TYPE_UINT8, ATTR_WRITABLE, cluster->level_move_rate);
    SETUP_ATTR(7, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_BINDING_MODE,
               ZCL_DATA_TYPE_ENUM8, ATTR_WRITABLE, cluster->binded_mode);
    SETUP_ATTR(8, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MULTI_PRESS_RESET,
               ZCL_DATA_TYPE_BOOLEAN, ATTR_WRITABLE, cluster->multi_press_reset);

    // Configuration
    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 9;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->cluster_count++;

    // Output ON OFF to bind to other devices
    endpoint->clusters[endpoint->cluster_count].cluster_id      = ZCL_CLUSTER_ON_OFF;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 0;
    endpoint->clusters[endpoint->cluster_count].attributes      = NULL;
    endpoint->clusters[endpoint->cluster_count].is_server       = 0;
    endpoint->cluster_count++;

    SETUP_ATTR_FOR_TABLE(cluster->multistate_attr_infos, 0,
                         ZCL_ATTR_MULTISTATE_INPUT_NUMBER_OF_STATES,
                         ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
                         multistate_num_of_states);
    SETUP_ATTR_FOR_TABLE(cluster->multistate_attr_infos, 1,
                         ZCL_ATTR_MULTISTATE_INPUT_OUT_OF_SERVICE,
                         ZCL_DATA_TYPE_BOOLEAN, ATTR_READONLY,
                         multistate_out_of_service);
    SETUP_ATTR_FOR_TABLE(cluster->multistate_attr_infos, 2,
                         ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE,
                         ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
                         cluster->multistate_state);
    SETUP_ATTR_FOR_TABLE(cluster->multistate_attr_infos, 3,
                         ZCL_ATTR_MULTISTATE_INPUT_STATUS_FLAGS,
                         ZCL_DATA_TYPE_BITMAP8, ATTR_READONLY, multistate_flags);

    // Output
    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_MULTISTATE_INPUT_BASIC;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 4;
    endpoint->clusters[endpoint->cluster_count].attributes      =
        cluster->multistate_attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server = 1;
    endpoint->cluster_count++;

    // Output Level for other devices
    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_LEVEL_CONTROL;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 0;
    endpoint->clusters[endpoint->cluster_count].attributes      = NULL;
    endpoint->clusters[endpoint->cluster_count].is_server       = 0;
    endpoint->cluster_count++;
}

// Perform the relay action for ON position (position 1 in ZCL docs)
void switch_cluster_relay_action_on(zigbee_switch_cluster *cluster) {
    if (!switch_cluster_has_valid_relay(cluster))
        return;

    switch (cluster->action) {
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_ONOFF:
        switch_cluster_target_set(cluster, true);
        break;
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_OFFON:
        switch_cluster_target_set(cluster, false);
        break;
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE:
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_SYNC:
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_OPPOSITE:
        switch_cluster_target_toggle(cluster);
        break;
    }
}

// Perform the relay action for OFF position (position 2 in ZCL docs)
void switch_cluster_relay_action_off(zigbee_switch_cluster *cluster) {
    if (!switch_cluster_has_valid_relay(cluster))
        return;

    switch (cluster->action) {
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_ONOFF:
        switch_cluster_target_set(cluster, false);
        break;
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_OFFON:
        switch_cluster_target_set(cluster, true);
        break;
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE:
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_SYNC:
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_OPPOSITE:
        switch_cluster_target_toggle(cluster);
        break;
    }
}

// Send OnOff command to binded device based on ON position (position 1 in
// ZCL docs)
void switch_cluster_binding_action_on(zigbee_switch_cluster *cluster) {
    if (hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINED) {
        return;
    }

    uint8_t cmd_id;

    switch (cluster->action) {
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_ONOFF:
        cmd_id = ZCL_CMD_ONOFF_ON;
        break;

    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_OFFON:
        cmd_id = ZCL_CMD_ONOFF_OFF;
        break;

    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE:
        cmd_id = ZCL_CMD_ONOFF_TOGGLE;
        break;

    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_SYNC:
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_OPPOSITE:
        if (!switch_cluster_has_valid_relay(cluster)) {
            cmd_id = ZCL_CMD_ONOFF_TOGGLE;
        } else {
            bool target_on = switch_cluster_target_is_on(cluster);
            if (cluster->action ==
                ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_SYNC)
                cmd_id = target_on ? ZCL_CMD_ONOFF_ON : ZCL_CMD_ONOFF_OFF;
            else
                cmd_id = target_on ? ZCL_CMD_ONOFF_OFF : ZCL_CMD_ONOFF_ON;
        }
        break;

    default:
        return;
    }

    hal_zigbee_cmd c = build_onoff_cmd(cluster->endpoint, cmd_id);
    hal_zigbee_send_cmd_to_bindings(&c);
}

// Send OnOff command to binded device based on OFF position (position 2 in
// ZCL docs)
void switch_cluster_binding_action_off(zigbee_switch_cluster *cluster) {
    if (hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINED) {
        return;
    }

    uint8_t cmd_id;

    switch (cluster->action) {
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_ONOFF:
        cmd_id = ZCL_CMD_ONOFF_OFF;
        break;

    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_OFFON:
        cmd_id = ZCL_CMD_ONOFF_ON;
        break;

    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SIMPLE:
        cmd_id = ZCL_CMD_ONOFF_TOGGLE;
        break;

    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_SYNC:
    case ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_OPPOSITE:
        if (!switch_cluster_has_valid_relay(cluster)) {
            cmd_id = ZCL_CMD_ONOFF_TOGGLE;
        } else {
            bool target_on = switch_cluster_target_is_on(cluster);
            if (cluster->action ==
                ZCL_ONOFF_CONFIGURATION_SWITCH_ACTION_TOGGLE_SMART_SYNC)
                cmd_id = target_on ? ZCL_CMD_ONOFF_ON : ZCL_CMD_ONOFF_OFF;
            else
                cmd_id = target_on ? ZCL_CMD_ONOFF_OFF : ZCL_CMD_ONOFF_ON;
        }
        break;

    default:
        return;
    }

    hal_zigbee_cmd c = build_onoff_cmd(cluster->endpoint, cmd_id);
    hal_zigbee_send_cmd_to_bindings(&c);
}

void switch_cluster_level_stop(zigbee_switch_cluster *cluster) {
    if (hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINED) {
        return;
    }

    hal_zigbee_cmd c = build_level_stop_onoff_cmd(cluster->endpoint);
    hal_zigbee_send_cmd_to_bindings(&c);
}

void switch_cluster_level_control(zigbee_switch_cluster *cluster) {
    if (hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINED) {
        return;
    }

    hal_zigbee_cmd c = build_level_move_onoff_cmd(cluster->endpoint,
                                                  cluster->level_move_direction,
                                                  cluster->level_move_rate);
    hal_zigbee_send_cmd_to_bindings(&c);

    if (cluster->level_move_direction == ZCL_LEVEL_MOVE_DOWN) {
        cluster->level_move_direction = ZCL_LEVEL_MOVE_UP;
    } else {
        cluster->level_move_direction = ZCL_LEVEL_MOVE_DOWN;
    }
}

void switch_cluster_on_button_press(zigbee_switch_cluster *cluster) {
    switch_cluster_flash_indicator(cluster);

    if (cluster->mode == ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE) {
        // Toggle does not support modes (RISE, SHORT, LONG)
        if (cluster->relay_mode != ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED) {
            switch_cluster_relay_action_on(cluster);
        }
        switch_cluster_binding_action_on(cluster);
        cluster->multistate_state = MULTISTATE_POSITION_ON;
        hal_zigbee_notify_attribute_changed(
            cluster->endpoint, ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
            ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE);
        return;
    }

    if (cluster->relay_mode == ZCL_ONOFF_CONFIGURATION_RELAY_MODE_RISE) {
        switch_cluster_relay_action_on(cluster);
    }

    if (cluster->binded_mode == ZCL_ONOFF_CONFIGURATION_BINDED_MODE_RISE) {
        switch_cluster_binding_action_on(cluster);
    }

    cluster->multistate_state = MULTISTATE_PRESS;
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
                                        ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE);
}

void switch_cluster_on_button_release(zigbee_switch_cluster *cluster) {
    if (cluster->mode == ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE) {
        // Only flash on release for toggles,
        // for momentary flash on press only
        switch_cluster_flash_indicator(cluster);
    }

    if (cluster->mode == ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE) {
        // Toggle does not support modes (RISE, SHORT, LONG)
        if (cluster->relay_mode != ZCL_ONOFF_CONFIGURATION_RELAY_MODE_DETACHED) {
            switch_cluster_relay_action_off(cluster);
        }
        switch_cluster_binding_action_off(cluster);
        cluster->multistate_state = MULTISTATE_POSITION_OFF;
        hal_zigbee_notify_attribute_changed(
            cluster->endpoint, ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
            ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE);
        return;
    }

    if (cluster->multistate_state != MULTISTATE_LONG_PRESS) {
        if (cluster->relay_mode == ZCL_ONOFF_CONFIGURATION_RELAY_MODE_SHORT) {
            switch_cluster_relay_action_on(cluster);
        }
        if (cluster->binded_mode == ZCL_ONOFF_CONFIGURATION_BINDED_MODE_SHORT) {
            switch_cluster_binding_action_on(cluster);
        }
    } else {
        // This is end of long press, send zcl_level stop
        switch_cluster_level_stop(cluster);
    }

    cluster->multistate_state = MULTISTATE_NOT_PRESSED;
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
                                        ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE);
}

void switch_cluster_on_button_long_press(zigbee_switch_cluster *cluster) {
    if (cluster->mode == ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE) {
        // Toggle does not support modes (RISE, SHORT, LONG)
        return;
    }

    if (cluster->relay_mode == ZCL_ONOFF_CONFIGURATION_RELAY_MODE_LONG) {
        if (switch_cluster_has_valid_relay(cluster)) {
            switch_cluster_target_toggle(cluster);
        }
    }

    if (cluster->binded_mode == ZCL_ONOFF_CONFIGURATION_BINDED_MODE_LONG) {
        switch_cluster_binding_action_on(cluster);
    }

    // Companion long-press endpoint (2EP): toggle its own separate bindings, so
    // a short press and a long press can control two different bound targets.
    if (cluster->long_press_endpoint != 0 &&
        hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED) {
        hal_zigbee_cmd c =
            build_onoff_cmd(cluster->long_press_endpoint, ZCL_CMD_ONOFF_TOGGLE);
        hal_zigbee_send_cmd_to_bindings(&c);
    }

    switch_cluster_level_control(cluster);

    cluster->multistate_state = MULTISTATE_LONG_PRESS;
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
                                        ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE);
}

void synchronize_multistate_state(zigbee_switch_cluster *cluster) {
    if (cluster->mode == ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_TOGGLE) {
        if (cluster->button->pressed) {
            cluster->multistate_state = MULTISTATE_POSITION_ON;
        } else {
            cluster->multistate_state = MULTISTATE_POSITION_OFF;
        }
    } else {
        if (cluster->button->long_pressed) {
            cluster->multistate_state = MULTISTATE_LONG_PRESS;
        } else if (cluster->button->pressed) {
            cluster->multistate_state = MULTISTATE_PRESS;
        } else {
            cluster->multistate_state = MULTISTATE_NOT_PRESSED;
        }
    }
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
                                        ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE);
}

void switch_cluster_on_write_attr(zigbee_switch_cluster *cluster,
                                  uint16_t attribute_id) {
    printf("Index at write attr: %d\r\n", cluster->switch_idx);
    if (attribute_id == ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX) {
        if (switch_cluster_output_cnt() == 0) {
            cluster->relay_index = 0;
        } else if (cluster->relay_index != SWITCH_RELAY_INDEX_ALL &&
                   (cluster->relay_index < 1 ||
                    cluster->relay_index > switch_cluster_output_cnt())) {
            cluster->relay_index = 1;
        }
    }
    if (attribute_id == ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MODE) {
        synchronize_multistate_state(cluster);
        if (cluster->mode == ZCL_ONOFF_CONFIGURATION_SWITCH_TYPE_MOMENTARY_NC) {
            cluster->button->pressed_when_high = 1;
        } else {
            cluster->button->pressed_when_high = 0;
        }
    }
    if (attribute_id == ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MULTI_PRESS_RESET) {
        cluster->multi_press_reset = cluster->multi_press_reset ? 1 : 0;
        switch_cluster_store_multi_press_reset_to_nv(cluster);
    }
    switch_cluster_store_attrs_to_nv(cluster);
}

bool switch_cluster_multi_press_resets(const zigbee_switch_cluster *cluster) {
    return cluster != NULL && cluster->multi_press_reset;
}

void switch_cluster_store_multi_press_reset_to_nv(zigbee_switch_cluster *cluster) {
    uint8_t value = cluster->multi_press_reset ? 1 : 0;

    hal_nvm_write(NV_ITEM_SWITCH_MULTI_PRESS_RESET(cluster->switch_idx),
                  sizeof(value), &value);
}

void switch_cluster_load_multi_press_reset_from_nv(zigbee_switch_cluster *cluster) {
    uint8_t value;

    // No record means a device that has never been told otherwise, so the
    // reset stays available - never lock a user out of their own switch.
    if (hal_nvm_read(NV_ITEM_SWITCH_MULTI_PRESS_RESET(cluster->switch_idx),
                     sizeof(value), &value) != HAL_NVM_SUCCESS) {
        return;
    }
    cluster->multi_press_reset = value ? 1 : 0;
}

zigbee_switch_cluster_config nv_config_buffer;

void switch_cluster_store_attrs_to_nv(zigbee_switch_cluster *cluster) {
    nv_config_buffer.action      = cluster->action;
    nv_config_buffer.mode        = cluster->mode;
    nv_config_buffer.relay_index = cluster->relay_index;
    nv_config_buffer.relay_mode  = cluster->relay_mode;
    nv_config_buffer.button_long_press_duration =
        cluster->button->long_press_duration_ms;
    nv_config_buffer.level_move_rate = cluster->level_move_rate;
    nv_config_buffer.binded_mode     = cluster->binded_mode;
    hal_nvm_write(NV_ITEM_SWITCH_CLUSTER_DATA(cluster->switch_idx),
                  sizeof(zigbee_switch_cluster_config),
                  (uint8_t *)&nv_config_buffer);
}

void switch_cluster_load_attrs_from_nv(zigbee_switch_cluster *cluster) {
    hal_nvm_status_t st = hal_nvm_read(
        NV_ITEM_SWITCH_CLUSTER_DATA(cluster->switch_idx),
        sizeof(zigbee_switch_cluster_config), (uint8_t *)&nv_config_buffer);

    if (st != HAL_NVM_SUCCESS) {
        printf("No switch config in NV, using defaults\r\n");
        return;
    }
    cluster->action      = nv_config_buffer.action;
    cluster->mode        = nv_config_buffer.mode;
    cluster->relay_index = nv_config_buffer.relay_index;
    cluster->relay_mode  = nv_config_buffer.relay_mode;
    cluster->button->long_press_duration_ms =
        nv_config_buffer.button_long_press_duration;
    cluster->level_move_rate = nv_config_buffer.level_move_rate;
    cluster->binded_mode     = nv_config_buffer.binded_mode;

    // Validate relay_index to prevent out-of-bounds access
    if (switch_cluster_output_cnt() == 0) {
        cluster->relay_index = 0;
    } else if (cluster->relay_index != SWITCH_RELAY_INDEX_ALL &&
               (cluster->relay_index < 1 ||
                cluster->relay_index > switch_cluster_output_cnt())) {
        printf("Invalid relay_index %d in NV, resetting to default\r\n",
               cluster->relay_index);
        cluster->relay_index = cluster->switch_idx + 1;
    }
}

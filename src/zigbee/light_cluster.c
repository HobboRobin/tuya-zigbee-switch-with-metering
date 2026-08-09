#include "light_cluster.h"
#include "cluster_common.h"
#include "consts.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "device_config/nvm_items.h"
#include <string.h>

#define LIGHT_LEVEL_MIN           1
#define LIGHT_LEVEL_MAX           254

#define LIGHT_STARTUP_OFF         0
#define LIGHT_STARTUP_ON          1
#define LIGHT_STARTUP_PREVIOUS    2

// Fixed attribute values; the stack needs an address for every attribute.
static uint16_t color_temp_phys_min = LIGHT_COLOR_TEMP_MIN_MIREDS;
static uint16_t color_temp_phys_max = LIGHT_COLOR_TEMP_MAX_MIREDS;
static uint8_t  color_mode          = ZCL_COLOR_MODE_TEMPERATURE;
static uint16_t color_capabilities  = ZCL_COLOR_CAPABILITY_TEMPERATURE;

zigbee_light_cluster *light_cluster_by_endpoint[12];

static void light_cluster_apply(zigbee_light_cluster *cluster);
static void light_cluster_store_to_nv(zigbee_light_cluster *cluster);
static void light_cluster_load_from_nv(zigbee_light_cluster *cluster);

typedef struct {
    uint8_t  startup_mode;
    uint8_t  level;
    uint16_t color_temp;
    uint16_t transition_ms;
    uint16_t startup_color_temp_setting;
    uint8_t  color_options;
} light_nv_data_t;

// Drive the channels from the current on/level/colour_temp.
//
// For a single channel the level is the brightness. For tunable white the
// level is split between cold and warm by where the colour temperature sits in
// the mired range, so total output stays roughly constant as the colour is
// moved. Mireds run the other way round from kelvin: the lower end is the
// coldest light, so it feeds the cold channel.
static void light_cluster_apply(zigbee_light_cluster *cluster) {
    if (!cluster->on) {
        for (uint8_t i = 0; i < cluster->channel_count; i++) {
            led_set_transition(cluster->channels[i], cluster->transition_ms);
            led_off(cluster->channels[i]);
        }
        return;
    }

    if (cluster->channel_count == 1) {
        led_set_transition(cluster->channels[0], cluster->transition_ms);
        led_set_brightness(cluster->channels[0], cluster->level);
        led_on(cluster->channels[0]);
        return;
    }

    uint16_t span = LIGHT_COLOR_TEMP_MAX_MIREDS - LIGHT_COLOR_TEMP_MIN_MIREDS;
    uint16_t pos  = cluster->color_temp;

    if (pos < LIGHT_COLOR_TEMP_MIN_MIREDS) {
        pos = LIGHT_COLOR_TEMP_MIN_MIREDS;
    }
    if (pos > LIGHT_COLOR_TEMP_MAX_MIREDS) {
        pos = LIGHT_COLOR_TEMP_MAX_MIREDS;
    }
    uint16_t warm_permille =
        (uint16_t)(((uint32_t)(pos - LIGHT_COLOR_TEMP_MIN_MIREDS) * 1000u) / span);

    uint8_t warm = (uint8_t)(((uint32_t)cluster->level * warm_permille) / 1000u);
    uint8_t cold = (uint8_t)(cluster->level - warm);

    led_set_transition(cluster->channels[0], cluster->transition_ms);
    led_set_transition(cluster->channels[1], cluster->transition_ms);
    led_set_brightness(cluster->channels[0], cold);
    led_set_brightness(cluster->channels[1], warm);
    // A channel at zero must go off rather than sit at brightness 0, so the
    // fade actually ends dark.
    cold ? led_on(cluster->channels[0]) : led_off(cluster->channels[0]);
    warm ? led_on(cluster->channels[1]) : led_off(cluster->channels[1]);
}

static void light_cluster_notify(zigbee_light_cluster *cluster) {
    hal_zigbee_notify_attribute_changed(cluster->endpoint, ZCL_CLUSTER_ON_OFF,
                                        ZCL_ATTR_ONOFF);
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_LEVEL_CONTROL,
                                        ZCL_ATTR_LEVEL_CURRENT_LEVEL);
    if (cluster->channel_count > 1) {
        hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                            ZCL_CLUSTER_COLOR_CONTROL,
                                            ZCL_ATTR_COLOR_TEMP_MIREDS);
    }
}

static void light_cluster_changed(zigbee_light_cluster *cluster) {
    light_cluster_apply(cluster);
    light_cluster_notify(cluster);
    light_cluster_store_to_nv(cluster);
}

void light_cluster_on(zigbee_light_cluster *cluster) {
    cluster->on = 1;
    light_cluster_changed(cluster);
}

void light_cluster_off(zigbee_light_cluster *cluster) {
    cluster->on = 0;
    light_cluster_changed(cluster);
}

void light_cluster_toggle(zigbee_light_cluster *cluster) {
    cluster->on = !cluster->on;
    light_cluster_changed(cluster);
}

void light_cluster_set_level(zigbee_light_cluster *cluster, uint8_t level) {
    if (level < LIGHT_LEVEL_MIN) {
        level = LIGHT_LEVEL_MIN;
    }
    if (level > LIGHT_LEVEL_MAX) {
        level = LIGHT_LEVEL_MAX;
    }
    cluster->level = level;
    light_cluster_changed(cluster);
}

void light_clusters_report_state(void) {
    for (int i = 0; i < 12; i++) {
        zigbee_light_cluster *cluster = light_cluster_by_endpoint[i];
        if (cluster == NULL) {
            continue;
        }
        hal_zigbee_send_report_attr(cluster->endpoint, ZCL_CLUSTER_ON_OFF,
                                    ZCL_ATTR_ONOFF, ZCL_DATA_TYPE_BOOLEAN,
                                    &cluster->on, 1);
        hal_zigbee_send_report_attr(cluster->endpoint, ZCL_CLUSTER_LEVEL_CONTROL,
                                    ZCL_ATTR_LEVEL_CURRENT_LEVEL,
                                    ZCL_DATA_TYPE_UINT8, &cluster->level, 1);
    }
}

void light_clusters_blink(uint16_t on_time_ms, uint16_t off_time_ms,
                          uint16_t times) {
    for (int i = 0; i < 12; i++) {
        zigbee_light_cluster *cluster = light_cluster_by_endpoint[i];
        if (cluster == NULL) {
            continue;
        }
        for (uint8_t ch = 0; ch < cluster->channel_count; ch++) {
            led_blink(cluster->channels[ch], on_time_ms, off_time_ms, times);
        }
    }
}

void light_clusters_restore(void) {
    for (int i = 0; i < 12; i++) {
        if (light_cluster_by_endpoint[i] != NULL) {
            light_cluster_apply(light_cluster_by_endpoint[i]);
        }
    }
}

static void light_cluster_store_to_nv(zigbee_light_cluster *cluster) {
    light_nv_data_t data = {
        .startup_mode  = cluster->startup_mode,
        .level         = cluster->level,
        .color_temp    = cluster->color_temp,
        .transition_ms = cluster->transition_ms,
        .startup_color_temp_setting = cluster->startup_color_temp_setting,
        .color_options              = cluster->color_options,
    };

    // "Previous" restores what the light was actually showing, so the stored
    // level and colour follow every change rather than only the startup mode.
    hal_nvm_write(NV_ITEM_LIGHT_CLUSTER_DATA(cluster->light_idx), sizeof(data),
                  (uint8_t *)&data);
}

static void light_cluster_load_from_nv(zigbee_light_cluster *cluster) {
    light_nv_data_t data;

    if (hal_nvm_read(NV_ITEM_LIGHT_CLUSTER_DATA(cluster->light_idx),
                     sizeof(data), (uint8_t *)&data) != HAL_NVM_SUCCESS) {
        return;
    }
    cluster->startup_mode               = data.startup_mode;
    cluster->startup_level              = data.level;
    cluster->startup_color_temp         = data.color_temp;
    cluster->transition_ms              = data.transition_ms;
    cluster->startup_color_temp_setting = data.startup_color_temp_setting;
    cluster->color_options              = data.color_options;
}

static void light_cluster_handle_startup(zigbee_light_cluster *cluster) {
    // startUpColorTemperature is independent of the on/off startup mode: it
    // decides which colour the light comes back at, not whether it comes back.
    if (cluster->startup_color_temp_setting != ZCL_COLOR_STARTUP_TEMP_PREVIOUS &&
        cluster->startup_color_temp_setting != 0) {
        cluster->color_temp = cluster->startup_color_temp_setting;
    } else if (cluster->startup_color_temp > 0) {
        cluster->color_temp = cluster->startup_color_temp;
    }

    switch (cluster->startup_mode) {
    case LIGHT_STARTUP_ON:
        cluster->on = 1;
        break;

    case LIGHT_STARTUP_PREVIOUS:
        // Brightness and colour come back too, not just on/off - a light that
        // returns at full white after a power cut is worse than one that stays
        // off.
        cluster->on = cluster->startup_level > 0;
        if (cluster->startup_level > 0) {
            cluster->level = cluster->startup_level;
        }
        // The colour is already restored above, for every startup mode.
        break;

    case LIGHT_STARTUP_OFF:
    default:
        cluster->on = 0;
        break;
    }
}

hal_zigbee_cmd_result_t light_cluster_onoff_callback(zigbee_light_cluster *cluster,
                                                     uint8_t command_id) {
    switch (command_id) {
    case ZCL_CMD_ONOFF_ON:
        light_cluster_on(cluster);
        break;

    case ZCL_CMD_ONOFF_OFF:
        light_cluster_off(cluster);
        break;

    case ZCL_CMD_ONOFF_TOGGLE:
        light_cluster_toggle(cluster);
        break;

    default:
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    return HAL_ZIGBEE_CMD_PROCESSED;
}

hal_zigbee_cmd_result_t light_cluster_level_callback(zigbee_light_cluster *cluster,
                                                     uint8_t command_id,
                                                     void *cmd_payload,
                                                     uint16_t cmd_payload_len) {
    switch (command_id) {
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF: {
        if (cmd_payload == NULL || cmd_payload_len < 1) {
            return HAL_ZIGBEE_MALFORMED_COMMAND;
        }
        uint8_t level = *(uint8_t *)cmd_payload;

        // Only the WithOnOff variant may switch the light; the plain one just
        // stores the level, per the Level Control spec.
        if (command_id == ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF) {
            cluster->on = level > 0;
        }
        light_cluster_set_level(cluster, level);
        break;
    }

    default:
        printf("Unhandled LevelCtrl command: %d\r\n", command_id);
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    return HAL_ZIGBEE_CMD_PROCESSED;
}

hal_zigbee_cmd_result_t light_cluster_color_callback(zigbee_light_cluster *cluster,
                                                     uint8_t command_id,
                                                     void *cmd_payload,
                                                     uint16_t cmd_payload_len) {
    if (cluster->channel_count < 2) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }

    switch (command_id) {
    case ZCL_CMD_COLOR_MOVE_TO_COLOR_TEMP: {
        if (cmd_payload == NULL || cmd_payload_len < 2) {
            return HAL_ZIGBEE_MALFORMED_COMMAND;
        }
        uint16_t mireds = (uint16_t)(((uint8_t *)cmd_payload)[0]) |
                          (uint16_t)(((uint8_t *)cmd_payload)[1] << 8);
        cluster->color_temp = mireds;
        light_cluster_changed(cluster);
        break;
    }

    default:
        printf("Unhandled ColorCtrl command: %d\r\n", command_id);
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    return HAL_ZIGBEE_CMD_PROCESSED;
}

hal_zigbee_cmd_result_t light_cluster_callback_trampoline(uint8_t endpoint,
                                                          uint16_t cluster_id,
                                                          uint8_t command_id,
                                                          void *cmd_payload,
                                                          uint16_t cmd_payload_len) {
    zigbee_light_cluster *cluster = light_cluster_by_endpoint[endpoint];

    if (cluster == NULL) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }

    switch (cluster_id) {
    case ZCL_CLUSTER_ON_OFF:
        return light_cluster_onoff_callback(cluster, command_id);

    case ZCL_CLUSTER_LEVEL_CONTROL:
        return light_cluster_level_callback(cluster, command_id, cmd_payload,
                                            cmd_payload_len);

    case ZCL_CLUSTER_COLOR_CONTROL:
        return light_cluster_color_callback(cluster, command_id, cmd_payload,
                                            cmd_payload_len);

    default:
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
}

void light_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                  uint16_t attribute_id) {
    zigbee_light_cluster *cluster = light_cluster_by_endpoint[endpoint];

    if (cluster == NULL) {
        return;
    }
    light_cluster_changed(cluster);
}

void light_cluster_add_to_endpoint(zigbee_light_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint) {
    light_cluster_by_endpoint[endpoint->endpoint] = cluster;
    cluster->endpoint = endpoint->endpoint;

    if (cluster->level == 0) {
        cluster->level = LIGHT_LEVEL_MAX;
    }
    if (cluster->color_temp == 0) {
        cluster->color_temp =
            (LIGHT_COLOR_TEMP_MIN_MIREDS + LIGHT_COLOR_TEMP_MAX_MIREDS) / 2;
    }
    // Default to picking the colour back up where it left off, and to obeying a
    // colour command while off - a coordinator that sets the colour first and
    // switches on second is otherwise ignored on the first half of the pair.
    cluster->startup_color_temp_setting = ZCL_COLOR_STARTUP_TEMP_PREVIOUS;
    cluster->color_options = ZCL_COLOR_OPTIONS_EXECUTE_IF_OFF;

    light_cluster_load_from_nv(cluster);
    light_cluster_handle_startup(cluster);
    light_cluster_apply(cluster);

    SETUP_ATTR_FOR_TABLE(cluster->onoff_attrs, 0, ZCL_ATTR_ONOFF,
                         ZCL_DATA_TYPE_BOOLEAN, ATTR_READONLY, cluster->on);
    SETUP_ATTR_FOR_TABLE(cluster->onoff_attrs, 1, ZCL_ATTR_START_UP_ONOFF,
                         ZCL_DATA_TYPE_ENUM8, ATTR_WRITABLE,
                         cluster->startup_mode);

    endpoint->clusters[endpoint->cluster_count].cluster_id      = ZCL_CLUSTER_ON_OFF;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 2;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->onoff_attrs;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->clusters[endpoint->cluster_count].cmd_callback    =
        light_cluster_callback_trampoline;
    endpoint->cluster_count++;

    SETUP_ATTR_FOR_TABLE(cluster->level_attrs, 0, ZCL_ATTR_LEVEL_CURRENT_LEVEL,
                         ZCL_DATA_TYPE_UINT8, ATTR_READONLY, cluster->level);
    SETUP_ATTR_FOR_TABLE(cluster->level_attrs, 1, ZCL_ATTR_LEVEL_TRANSITION,
                         ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
                         cluster->transition_ms);

    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_LEVEL_CONTROL;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 2;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->level_attrs;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->clusters[endpoint->cluster_count].cmd_callback    =
        light_cluster_callback_trampoline;
    endpoint->cluster_count++;

    if (cluster->channel_count < 2) {
        return;
    }

    SETUP_ATTR_FOR_TABLE(cluster->color_attrs, 0, ZCL_ATTR_COLOR_TEMP_MIREDS,
                         ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
                         cluster->color_temp);
    SETUP_ATTR_FOR_TABLE(cluster->color_attrs, 1, ZCL_ATTR_COLOR_MODE,
                         ZCL_DATA_TYPE_ENUM8, ATTR_READONLY, color_mode);
    SETUP_ATTR_FOR_TABLE(cluster->color_attrs, 2, ZCL_ATTR_COLOR_TEMP_PHYS_MIN,
                         ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
                         color_temp_phys_min);
    SETUP_ATTR_FOR_TABLE(cluster->color_attrs, 3, ZCL_ATTR_COLOR_TEMP_PHYS_MAX,
                         ZCL_DATA_TYPE_UINT16, ATTR_READONLY,
                         color_temp_phys_max);
    // Mandatory for a colour-temperature light: without it a coordinator has no
    // way to tell a tunable white from a full-colour one.
    SETUP_ATTR_FOR_TABLE(cluster->color_attrs, 4, ZCL_ATTR_COLOR_CAPABILITIES,
                         ZCL_DATA_TYPE_BITMAP16, ATTR_READONLY,
                         color_capabilities);
    // Z2M's "colour temp startup" option writes this one; leaving it out is
    // what made that read come back UNSUPPORTED_ATTRIBUTE.
    SETUP_ATTR_FOR_TABLE(cluster->color_attrs, 5, ZCL_ATTR_COLOR_STARTUP_TEMP,
                         ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
                         cluster->startup_color_temp_setting);
    SETUP_ATTR_FOR_TABLE(cluster->color_attrs, 6, ZCL_ATTR_COLOR_OPTIONS,
                         ZCL_DATA_TYPE_BITMAP8, ATTR_WRITABLE,
                         cluster->color_options);

    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_COLOR_CONTROL;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 7;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->color_attrs;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->clusters[endpoint->cluster_count].cmd_callback    =
        light_cluster_callback_trampoline;
    endpoint->cluster_count++;

    (void)color_capabilities;
}

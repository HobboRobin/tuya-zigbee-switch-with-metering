#include "identify_cluster.h"
#include "cluster_common.h"
#include "consts.h"
#include "hal/printf_selector.h"
#include "hal/tasks.h"
#include "light_cluster.h"
#include "relay_cluster.h"
#include <stddef.h>

// Set by config_parser once the peripherals are known, so this cluster can
// blink whatever the device happens to have without knowing its layout.
extern void identify_blink_all(uint16_t on_ms, uint16_t off_ms, uint16_t times);
extern void identify_restore_all(void);

static zigbee_identify_cluster *active_cluster = NULL;
static hal_task_t countdown_task;

static void identify_stop(zigbee_identify_cluster *cluster) {
    cluster->identify_time = 0;
    hal_tasks_unschedule(&countdown_task);
    identify_restore_all();
    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_IDENTIFY,
                                        ZCL_ATTR_IDENTIFY_TIME);
}

// One tick per second, so identifyTime stays readable while it runs - Z2M and
// the spec both expect it to count down rather than jump to zero at the end.
static void identify_tick(void *arg) {
    zigbee_identify_cluster *cluster = (zigbee_identify_cluster *)arg;

    if (cluster->identify_time == 0) {
        identify_stop(cluster);
        return;
    }
    cluster->identify_time--;

    if (cluster->identify_time == 0) {
        identify_stop(cluster);
        return;
    }
    hal_tasks_schedule(&countdown_task, 1000);
}

void identify_cluster_set_time(zigbee_identify_cluster *cluster,
                               uint16_t seconds) {
    active_cluster = cluster;

    if (seconds == 0) {
        identify_stop(cluster);
        return;
    }

    cluster->identify_time = seconds;
    identify_blink_all(IDENTIFY_BLINK_ON_MS, IDENTIFY_BLINK_OFF_MS,
                       LED_BLINK_FOREVER);

    countdown_task.handler = identify_tick;
    countdown_task.arg     = cluster;
    hal_tasks_init(&countdown_task);
    hal_tasks_schedule(&countdown_task, 1000);

    hal_zigbee_notify_attribute_changed(cluster->endpoint,
                                        ZCL_CLUSTER_IDENTIFY,
                                        ZCL_ATTR_IDENTIFY_TIME);
}

static hal_zigbee_cmd_result_t identify_cluster_callback(uint8_t endpoint,
                                                         uint16_t cluster_id,
                                                         uint8_t command_id,
                                                         void *cmd_payload,
                                                         uint16_t cmd_payload_len) {
    if (active_cluster == NULL) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }

    switch (command_id) {
    case ZCL_CMD_IDENTIFY: {
        if (cmd_payload == NULL || cmd_payload_len < 2) {
            return HAL_ZIGBEE_MALFORMED_COMMAND;
        }
        uint16_t seconds = (uint16_t)(((uint8_t *)cmd_payload)[0]) |
                           (uint16_t)(((uint8_t *)cmd_payload)[1] << 8);
        identify_cluster_set_time(active_cluster, seconds);
        break;
    }

    default:
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    return HAL_ZIGBEE_CMD_PROCESSED;
}

// Writing identifyTime is the other half of the spec's interface and the path
// some tools use instead of the command.
void identify_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                     uint16_t attribute_id) {
    if (active_cluster == NULL || attribute_id != ZCL_ATTR_IDENTIFY_TIME) {
        return;
    }
    identify_cluster_set_time(active_cluster, active_cluster->identify_time);
}

void identify_cluster_add_to_endpoint(zigbee_identify_cluster *cluster,
                                      hal_zigbee_endpoint *endpoint) {
    cluster->endpoint      = endpoint->endpoint;
    cluster->identify_time = 0;
    active_cluster         = cluster;

    SETUP_ATTR_FOR_TABLE(cluster->attr_infos, 0, ZCL_ATTR_IDENTIFY_TIME,
                         ZCL_DATA_TYPE_UINT16, ATTR_WRITABLE,
                         cluster->identify_time);

    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_IDENTIFY;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 1;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->clusters[endpoint->cluster_count].cmd_callback    =
        identify_cluster_callback;
    endpoint->cluster_count++;
}

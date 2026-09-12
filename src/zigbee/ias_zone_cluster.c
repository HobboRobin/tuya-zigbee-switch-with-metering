#include "ias_zone_cluster.h"
#include "cluster_common.h"
#include "consts.h"
#include "hal/printf_selector.h"
#include <string.h>

#define MAX_ENDPOINTS    12

// The coordinator writes its address, then expects to be asked to enrol. Doing
// that from inside the write handler would answer a write with a command while
// the stack is still holding the write, so it is pushed to the next tick.
#define ENROLL_DELAY_MS    200

static zigbee_ias_zone_cluster *zone_by_endpoint[MAX_ENDPOINTS];

static bool cie_address_is_set(const zigbee_ias_zone_cluster *cluster) {
    for (uint8_t i = 0; i < sizeof(cluster->cie_address); i++) {
        if (cluster->cie_address[i] != 0x00 && cluster->cie_address[i] != 0xFF) {
            return true;
        }
    }
    return false;
}

static void send_enroll_request(void *arg) {
    zigbee_ias_zone_cluster *cluster = (zigbee_ias_zone_cluster *)arg;

    // zoneType then the manufacturer code, little-endian. The manufacturer
    // code is zero: this is a plain ZCL zone with nothing proprietary in it.
    uint8_t        payload[4] = {
        (uint8_t)(cluster->zone_type & 0xFF),
        (uint8_t)(cluster->zone_type >> 8),
        0x00,
        0x00,
    };
    hal_zigbee_cmd cmd = {
        .endpoint            = cluster->endpoint,
        .profile_id          =                          0x0104,
        .cluster_id          = ZCL_CLUSTER_IAS_ZONE,
        .command_id          = ZCL_CMD_IAS_ZONE_ENROLL_REQUEST,
        .cluster_specific    =                               1,
        .direction           = HAL_ZIGBEE_DIR_SERVER_TO_CLIENT,
        .disable_default_rsp =                               0,
        .manufacturer_code   =                               0,
        .payload             = payload,
        .payload_len         = sizeof(payload),
    };

    hal_zigbee_send_cmd_to_bindings(&cmd);
}

static void send_status_change_notification(zigbee_ias_zone_cluster *cluster) {
    // zoneStatus, extendedStatus, zoneId, delay - the delay being how long ago
    // this happened, in quarter seconds. It happened just now.
    uint8_t        payload[6] = {
        (uint8_t)(cluster->zone_status & 0xFF),
        (uint8_t)(cluster->zone_status >> 8),
        0x00,
        cluster->zone_id,
        0x00,
        0x00,
    };
    hal_zigbee_cmd cmd = {
        .endpoint            = cluster->endpoint,
        .profile_id          =                                0x0104,
        .cluster_id          = ZCL_CLUSTER_IAS_ZONE,
        .command_id          = ZCL_CMD_IAS_ZONE_STATUS_CHANGE_NOTIFY,
        .cluster_specific    =                                     1,
        .direction           = HAL_ZIGBEE_DIR_SERVER_TO_CLIENT,
        .disable_default_rsp =                                     1,
        .manufacturer_code   =                                     0,
        .payload             = payload,
        .payload_len         = sizeof(payload),
    };

    hal_zigbee_send_cmd_to_bindings(&cmd);
}

void ias_zone_cluster_add_to_endpoint(zigbee_ias_zone_cluster *cluster,
                                      hal_zigbee_endpoint *endpoint) {
    if (endpoint->endpoint < MAX_ENDPOINTS) {
        zone_by_endpoint[endpoint->endpoint] = cluster;
    }
    cluster->endpoint = endpoint->endpoint;
    if (cluster->zone_type == 0) {
        cluster->zone_type = ZCL_IAS_ZONE_TYPE_CONTACT;
    }
    cluster->zone_state = ZCL_IAS_ZONE_STATE_NOT_ENROLLED;
    cluster->zone_id    = ZCL_IAS_ZONE_ID_INVALID;

    cluster->enroll_task.handler = send_enroll_request;
    cluster->enroll_task.arg     = cluster;
    hal_tasks_init(&cluster->enroll_task);

    SETUP_ATTR(0, ZCL_ATTR_IAS_ZONE_STATE, ZCL_DATA_TYPE_ENUM8, ATTR_READONLY,
               cluster->zone_state);
    SETUP_ATTR(1, ZCL_ATTR_IAS_ZONE_TYPE, ZCL_DATA_TYPE_ENUM16, ATTR_READONLY,
               cluster->zone_type);
    SETUP_ATTR(2, ZCL_ATTR_IAS_ZONE_STATUS, ZCL_DATA_TYPE_BITMAP16,
               ATTR_READONLY, cluster->zone_status);
    SETUP_ATTR(3, ZCL_ATTR_IAS_ZONE_CIE_ADDRESS, ZCL_DATA_TYPE_IEEE_ADDR,
               ATTR_WRITABLE, cluster->cie_address);
    SETUP_ATTR(4, ZCL_ATTR_IAS_ZONE_ID, ZCL_DATA_TYPE_UINT8, ATTR_READONLY,
               cluster->zone_id);

    endpoint->clusters[endpoint->cluster_count].cluster_id =
        ZCL_CLUSTER_IAS_ZONE;
    endpoint->clusters[endpoint->cluster_count].attribute_count = 5;
    endpoint->clusters[endpoint->cluster_count].attributes      = cluster->attr_infos;
    endpoint->clusters[endpoint->cluster_count].is_server       = 1;
    endpoint->clusters[endpoint->cluster_count].cmd_callback    =
        ias_zone_cluster_callback_cmd;
    endpoint->cluster_count++;
}

void ias_zone_cluster_set_alarm(zigbee_ias_zone_cluster *cluster, bool alarm) {
    uint16_t updated = alarm
                     ? (cluster->zone_status | ZCL_IAS_ZONE_STATUS_ALARM1)
                     : (cluster->zone_status & ~ZCL_IAS_ZONE_STATUS_ALARM1);

    if (updated == cluster->zone_status) {
        return;
    }
    cluster->zone_status = updated;

    // Both roads out, because a coordinator may have taken either: the
    // notification is what the spec says a zone sends, and the attribute is
    // what anyone who configured reporting is waiting on.
    send_status_change_notification(cluster);
    hal_zigbee_notify_attribute_changed(cluster->endpoint, ZCL_CLUSTER_IAS_ZONE,
                                        ZCL_ATTR_IAS_ZONE_STATUS);
}

void ias_zone_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                     uint16_t attribute_id) {
    if (endpoint >= MAX_ENDPOINTS || zone_by_endpoint[endpoint] == NULL) {
        return;
    }
    zigbee_ias_zone_cluster *cluster = zone_by_endpoint[endpoint];

    if (attribute_id != ZCL_ATTR_IAS_ZONE_CIE_ADDRESS) {
        return;
    }
    if (!cie_address_is_set(cluster)) {
        // Cleared rather than set: the coordinator is letting go of the zone.
        cluster->zone_state = ZCL_IAS_ZONE_STATE_NOT_ENROLLED;
        cluster->zone_id    = ZCL_IAS_ZONE_ID_INVALID;
        return;
    }
    printf("IAS CIE address written, asking to enrol\r\n");
    hal_tasks_schedule(&cluster->enroll_task, ENROLL_DELAY_MS);
}

hal_zigbee_cmd_result_t ias_zone_cluster_callback_cmd(uint8_t endpoint,
                                                      uint16_t cluster_id,
                                                      uint8_t command_id,
                                                      void *cmd_payload,
                                                      uint16_t cmd_payload_len) {
    if (endpoint >= MAX_ENDPOINTS || zone_by_endpoint[endpoint] == NULL) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    zigbee_ias_zone_cluster *cluster = zone_by_endpoint[endpoint];
    uint8_t *payload = (uint8_t *)cmd_payload;

    if (command_id != ZCL_CMD_IAS_ZONE_ENROLL_RESPONSE || cmd_payload_len < 2) {
        return HAL_ZIGBEE_CMD_SKIPPED;
    }
    if (payload[0] != ZCL_IAS_ZONE_ENROLL_SUCCESS) {
        printf("IAS enrolment refused: %d\r\n", payload[0]);
        return HAL_ZIGBEE_CMD_PROCESSED;
    }
    cluster->zone_state = ZCL_IAS_ZONE_STATE_ENROLLED;
    cluster->zone_id    = payload[1];
    printf("IAS enrolled as zone %d\r\n", cluster->zone_id);
    return HAL_ZIGBEE_CMD_PROCESSED;
}

uint16_t ias_zone_type_from_char(char code) {
    switch (code) {
    case 'C':
        return ZCL_IAS_ZONE_TYPE_CONTACT;

    case 'M':
        return ZCL_IAS_ZONE_TYPE_MOTION;

    case 'W':
        return ZCL_IAS_ZONE_TYPE_WATER;

    case 'F':
        return ZCL_IAS_ZONE_TYPE_FIRE;

    case 'G':
        return ZCL_IAS_ZONE_TYPE_CO;

    case 'V':
        return ZCL_IAS_ZONE_TYPE_VIBRATION;

    default:
        return 0;
    }
}

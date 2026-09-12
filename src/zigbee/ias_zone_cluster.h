#ifndef _IAS_ZONE_CLUSTER_H_
#define _IAS_ZONE_CLUSTER_H_

#include "hal/tasks.h"
#include "hal/zigbee.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * IAS Zone (0x0500) - what turns a contact on a pin into a door sensor.
 *
 * Without it a reed is just a switch: the firmware reports which way it is
 * pointing as a switch action, and a coordinator has no way to know that the
 * thing on the other end is a door. With it the device says what kind of
 * sensor it is once, and every coordinator renders it the same way - a
 * contact, a leak, a motion detector - rather than as a button that sometimes
 * presses itself.
 *
 * The zone rides along on a switch's endpoint rather than replacing it: the
 * switch keeps its action and its bindings, and the zone mirrors the same
 * input. Nothing that worked before stops working.
 */
typedef struct {
    uint8_t              endpoint;
    // What this sensor is, from the ZCL zone type list (contact, leak, ...).
    uint16_t             zone_type;
    // The alarm bitmap the coordinator reads. Bit 0 (alarm1) is the sensor
    // itself; the rest are tamper, low battery and friends, which this
    // firmware does not have wired anywhere yet.
    uint16_t             zone_status;
    uint8_t              zone_state;
    uint8_t              zone_id;
    // The coordinator writes its own address here to say "report to me". That
    // write is the trigger for enrolment, not a setting anyone reads back.
    uint8_t              cie_address[8];
    hal_zigbee_attribute attr_infos[5];
    hal_task_t           enroll_task;
} zigbee_ias_zone_cluster;

void ias_zone_cluster_add_to_endpoint(zigbee_ias_zone_cluster *cluster,
                                      hal_zigbee_endpoint *endpoint);

/** The sensor tripped, or stopped being tripped. */
void ias_zone_cluster_set_alarm(zigbee_ias_zone_cluster *cluster, bool alarm);

void ias_zone_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                     uint16_t attribute_id);

hal_zigbee_cmd_result_t ias_zone_cluster_callback_cmd(uint8_t endpoint,
                                                      uint16_t cluster_id,
                                                      uint8_t command_id,
                                                      void *cmd_payload,
                                                      uint16_t cmd_payload_len);

/** Parse a zone type letter from the config string; 0 if it names nothing. */
uint16_t ias_zone_type_from_char(char code);

#endif // _IAS_ZONE_CLUSTER_H_

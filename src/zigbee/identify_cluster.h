#ifndef _IDENTIFY_CLUSTER_H_
#define _IDENTIFY_CLUSTER_H_

#include "hal/zigbee.h"
#include <stdint.h>

// genIdentify (0x0003) on endpoint 1: "make yourself known".
//
// Z2M renders an Identify button for any device carrying this cluster without
// needing converter support, and sends the duration with the command - so the
// device does not need a setting of its own for how long to blink.
//
// While identifying, everything the device can light up blinks: the network
// LED, the relay indicators and any light output, each honouring its own
// configured transition. Afterwards the LEDs are put back to whatever they
// should be showing, which is why this has to run through the owning clusters
// rather than poking the leds directly.
#define IDENTIFY_BLINK_ON_MS     500
#define IDENTIFY_BLINK_OFF_MS    500

typedef struct {
    uint8_t              endpoint;
    uint16_t             identify_time; // seconds remaining, counted down
    hal_zigbee_attribute attr_infos[1];
} zigbee_identify_cluster;

void identify_cluster_add_to_endpoint(zigbee_identify_cluster *cluster,
                                      hal_zigbee_endpoint *endpoint);

// Start/stop identifying for `seconds` (0 stops).
void identify_cluster_set_time(zigbee_identify_cluster *cluster,
                               uint16_t seconds);

void identify_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                     uint16_t attribute_id);

#endif

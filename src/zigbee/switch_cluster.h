#ifndef _SWITCH_CLUSTER_H_
#define _SWITCH_CLUSTER_H_

#include "base_components/button.h"
#include "base_components/led.h"
#include "hal/zigbee.h"
#include <stdbool.h>
#include <stdint.h>

// relay_index picks which relay a switch drives: 0 = detached, 1..N a specific
// relay, and 0xFF every relay at once (a master button). 0xFF is out of the
// 1..N range on purpose, so an older firmware or a stale NV value can never be
// mistaken for it.
#define SWITCH_RELAY_INDEX_ALL    0xFF

typedef struct {
    uint8_t  mode;
    uint8_t  action;
    uint8_t  relay_mode;
    uint8_t  relay_index;
    uint16_t button_long_press_duration;
    uint8_t  level_move_rate;
    uint8_t  binded_mode;
} zigbee_switch_cluster_config;

typedef struct {
    uint8_t              switch_idx;
    uint8_t              endpoint;
    uint8_t              mode;
    uint8_t              action;
    uint8_t              relay_mode;
    uint8_t              relay_index;
    uint8_t              binded_mode;
    // Whether hammering this input resets the device to factory defaults.
    // On by default, because on most boards the switch is the only way in.
    // An input that is not a button - a reed contact, a float switch - can
    // reach the press count on its own, so it can be taken out of the reset.
    uint8_t              multi_press_reset;
    // The short confirmation flash of this switch's indicator LED: whether it
    // happens at all, and how bright. Only in play where no relay owns the LED
    // - with a relay attached the relay's own state drives it instead.
    uint8_t              flash_indicator;
    uint8_t              flash_brightness;
    button_t *           button;
    hal_zigbee_attribute attr_infos[11];
    uint16_t             multistate_state;
    hal_zigbee_attribute multistate_attr_infos[4];
    uint8_t              level_move_rate;
    uint8_t              level_move_direction;
    led_t *              indicator_led;
    // Optional companion endpoint (0 = none) that carries its own OnOff client
    // cluster. On a long press the switch sends an OnOff toggle to this
    // endpoint's bindings, so a short press and a long press can drive two
    // different bound targets. Enabled per device with the `2EP` config token.
    uint8_t              long_press_endpoint;
} zigbee_switch_cluster;

void switch_cluster_add_to_endpoint(zigbee_switch_cluster *cluster,
                                    hal_zigbee_endpoint *endpoint);

void switch_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                   uint16_t attribute_id);

/** Does hammering this switch still trigger the factory reset? */
bool switch_cluster_multi_press_resets(const zigbee_switch_cluster *cluster);

void update_switch_clusters(void);

#endif

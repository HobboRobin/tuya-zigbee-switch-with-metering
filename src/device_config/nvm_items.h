#ifndef DEVICE_CONFIG_NVM_ITEMS_H_
#define DEVICE_CONFIG_NVM_ITEMS_H_

#define MAX_RELAYS                       5
#define MAX_SWITCHES                     5
#define MAX_COVER_SWITCHES               3
#define MAX_COVERS                       3

#define NV_ITEM_CURRENT_VERSION_IN_NV    1
#define NV_ITEM_DEVICE_CONFIG            2
#define NV_ITEM_BASIC_CLUSTER_DATA       3
// switch_idx and relay_idx below are zero indexes, e.g. first switch has
// switch_idx = 0
#define NV_ITEM_SWITCH_CLUSTER_DATA(switch_idx) \
        (NV_ITEM_BASIC_CLUSTER_DATA + 1 + switch_idx)
#define NV_ITEM_RELAY_CLUSTER_DATA(relay_idx) \
        (NV_ITEM_BASIC_CLUSTER_DATA + MAX_SWITCHES + 1 + relay_idx)
#define NV_ITEM_COVER_SWITCH_CONFIG(cover_switch_idx) \
        (NV_ITEM_BASIC_CLUSTER_DATA + MAX_SWITCHES + MAX_RELAYS + 1 + cover_switch_idx)
#define NV_ITEM_COVER_CONFIG(cover_idx)                                                    \
        (NV_ITEM_BASIC_CLUSTER_DATA + MAX_SWITCHES + MAX_RELAYS + MAX_COVER_SWITCHES + 1 + \
         cover_idx)

// 3 + 5 (switches) + 5 (relays) + 3 (cover switches) + 3 (covers) = 19
// Adding room for future items, so starting from 32
#define NV_ITEM_DEVICE_TYPE                32

#define NV_ITEM_MULTI_PRESS_RESET_COUNT    33
#define NV_ITEM_POLL_CONTROL_CONFIG        34

// Energy monitoring NVM items (starting from 40)
// endpoint is 1-based (1-4)
#define NV_ITEM_ENERGY_ACCUMULATION(endpoint)    (40 + (endpoint) - 1)

// Persisted HLW8012 calibration multipliers (44), set via the on-device
// calibrate fields and re-applied on boot.
#define NV_ITEM_ENERGY_CALIBRATION    44

// Persisted dimmable-indicator-LED settings (brightness + transition), one per
// relay (45..). Kept separate from the relay config so growing it never resets
// existing startup/indicator settings.
#define NV_ITEM_LED_DIMMING(relay_idx)    (45 + (relay_idx))

// Persisted dimmable network/status LED settings (brightness + transition).
// Relay LED dimming uses 45..49 (MAX_RELAYS), so the next free slot is 50.
#define NV_ITEM_NET_LED_DIMMING    50

// Overload protection configuration (single metering endpoint).
#define NV_ITEM_OVERLOAD_CONFIG    51

// Per-light state (startup mode, level, colour temperature, transition), one
// per light output (52..). "Previous" restores brightness and colour, not just
// on/off, so this follows every change rather than only the startup setting.
#define MAX_LIGHTS    5
#define NV_ITEM_LIGHT_CLUSTER_DATA(light_idx)    (52 + (light_idx))

// Per-switch settings added after the original switch config: whether the
// switch takes part in the multi-press factory reset, and its confirmation
// flash. Separate from the switch config for the same reason as the LED
// dimming above: the NV layer rejects a record whose length does not match, so
// growing that struct would silently reset every stored switch setting on
// upgrade. Growing *this* one has the same cost, so it only ever loses the
// settings below - all of which have a safe default.
#define NV_ITEM_SWITCH_EXTRA_CONFIG(switch_idx)    (57 + (switch_idx))

#endif /* DEVICE_CONFIG_NVM_ITEMS_H_ */

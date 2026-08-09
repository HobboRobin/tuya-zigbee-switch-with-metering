#ifndef _LIGHT_CLUSTER_H_
#define _LIGHT_CLUSTER_H_

#include "base_components/led.h"
#include "hal/zigbee.h"
#include <stdint.h>

// A dimmable light output: genOnOff + genLevelCtrl on one endpoint, driving one
// or two PWM channels.
//
//   1 channel  -> plain dimmer
//   2 channels -> tunable white, and genLightingColorCtrl is added; the
//                 firmware mixes the cold and warm channel from the colour
//                 temperature so the pair behaves as a single light.
//
// The channels are plain `led_t`s: they already own the PWM, the 0..255
// brightness and the fade, so this cluster only has to translate ZCL into
// them. `transition_ms` lives here rather than on the channels because it is
// one setting per *light* - a tunable white fades both channels together.
#define LIGHT_MAX_CHANNELS    2

// Colour temperature range in mireds (mired = 1e6 / kelvin, so the *lower*
// bound is the coldest light). 167 = 6000 K, 333 = 3000 K.
//
// This has to match the strip: quoting a wider range than the hardware can
// reach means the outer part of the slider does nothing, because the mix is
// already fully cold or fully warm before the end of the scale. If a strip
// with a different span turns up, this becomes a per-device value passed as a
// -D define from device_db, the same way the meter calibration is.
#define LIGHT_COLOR_TEMP_MIN_MIREDS    167
#define LIGHT_COLOR_TEMP_MAX_MIREDS    333

typedef struct {
    uint8_t              light_idx;
    uint8_t              endpoint;
    uint8_t              channel_count;
    led_t *              channels[LIGHT_MAX_CHANNELS]; // [0] = cold / single, [1] = warm

    uint8_t              on;
    uint8_t              level;         // genLevelCtrl currentLevel, 1..254
    uint16_t             color_temp;    // mireds, tunable white only
    uint16_t             transition_ms; // fade time for on/off/level/colour

    uint8_t              startup_mode;  // 0 = off, 1 = on, 2 = previous
    uint8_t              startup_level; // level restored in "previous" mode
    uint16_t             startup_color_temp;

    // startUpColorTemperature: 0xFFFF ("previous") restores startup_color_temp,
    // anything else is the colour the light comes back at.
    uint16_t             startup_color_temp_setting;
    // colorOptions bit 0 decides whether a colour command is obeyed while the
    // light is off. The stack checks this itself, before our callback runs.
    uint8_t              color_options;

    hal_zigbee_attribute onoff_attrs[3];
    hal_zigbee_attribute level_attrs[3];
    hal_zigbee_attribute color_attrs[7];
} zigbee_light_cluster;

void light_cluster_add_to_endpoint(zigbee_light_cluster *cluster,
                                   hal_zigbee_endpoint *endpoint);

void light_cluster_on(zigbee_light_cluster *cluster);
void light_cluster_off(zigbee_light_cluster *cluster);
void light_cluster_toggle(zigbee_light_cluster *cluster);
void light_cluster_set_level(zigbee_light_cluster *cluster, uint8_t level);

// Push on/off and level to the coordinator (same heartbeat rationale as the
// relays: the stack sends no periodic report for discrete attributes).
void light_clusters_report_state(void);

// Blink every light channel, used by the identify cluster. `times` follows
// led_blink (LED_BLINK_FOREVER blinks until stopped).
void light_clusters_blink(uint16_t on_time_ms, uint16_t off_time_ms,
                          uint16_t times);

// Restore the lights to their real state after a blink run.
void light_clusters_restore(void);

void light_cluster_callback_attr_write_trampoline(uint8_t endpoint,
                                                  uint16_t attribute_id);

#endif

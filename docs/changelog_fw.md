_Open the **Outline** (table of contents) from the top right._

# Firmware Changelog

<!------------------------------------------------------

Please describe what you are working on, under ## Upcoming

------------------------------------------------------->

## Upcoming

### Features

- **Dimmable light outputs.** New `W<pin>` (single channel) and
  `T<cold><warm>` (tunable white) config tokens turn PWM pins into real Zigbee
  lights with brightness, colour temperature and a fade time. Two `W` tokens
  give two independent channels where one `T` over the same pins gives a single
  tunable white - same hardware, decided by the config string.
  - New device: Gledopto GL-C-006P, in two flavours (two tunable whites or five
    dimmers); switching between them needs no reflash.
  - New `Y<r><g><b>` token: a three-colour status LED whose colour names the
    configured light mode.
- **Identify** (`genIdentify`) on every device. Z2M shows the button without
  any converter support; the device blinks everything it can light up, each led
  with its own transition, then goes back to what it was showing.
- Switches can drive **every relay at once** (`relay_index` = `all`): anything
  on means everything goes off, otherwise everything goes on.
- **Switches can drive lights**, not only relays. `relay_index` now numbers all
  of the device's outputs - relays first, then lights - so a lamp with no
  relays at all can still work its own light from its buttons instead of only
  through a binding. `all` covers the lights too.
- Raised the ZCL cluster limit from 32 to 48, which also makes a **4-gang
  switch with `2EP`** possible for the first time.

- **Cover cluster** (window covering) for controlling the motor of curtains, blinds, and shutters.
  Supports open, close, and stop commands with motor safety delays.
- **Cover switch cluster** for handling user input from window covering switches.
  Supports toggle/momentary switches, stop-on-repeat, stop button, local control, and remote device binding.
- Relays now respond to *MoveToLevelWithOnOff*
  - Level = 0 -> Turn off relay
  - Level > 0 -> Turn on  relay
- Configurable multi-press factory reset count (set 1-255 or 0 to disable)
- **Indicator LED press confirmation**:  LEDs briefly flash on button press/release as visual feedback.
- **Push-button without relay** support for battery-powered scene switches
  - Switches auto-detect absence of relays and default to detached mode
  - New device: Moes 4-gang scene switch (`REMOTE_MOES_SWITCH_TS0044`)
- **Battery measurement & reporting** (Zigbee `genPowerCfg` cluster) for Telink devices
- **Deep retention sleep** for Telink end devices
- **Button actions as Home Assistant events**: every switch, cover switch and
  `2EP` long-press endpoint now publishes an `action`, so HA creates an event
  entity like the one a Hue remote gets. The `… press action` sensors stay as
  they were. See [actions.md](/docs/usage/actions.md).
  - Each button additionally gets an **event entity of its own**
    (`action_switch_left`, `action_switch_right`, …) whose types are unprefixed
    (`press`, `long_press`, `toggle`, …), because the entity already names the
    button. A `2EP` long press appears on its parent button as `long_toggle`.
    The combined device-wide `action` is unchanged.

### Changes

- Add `D<N>` config option to customize button debounce delay in milliseconds
- **Bi-stable (latching) relays** have been reworked
  - They now use proper pulses instead of continuously driving the coil
  - Pressing multiple buttons will toggle the relays with small delays in-between (safe)
  - Add `SLP;` to the config string for simultaneous toggles (risky, might damage the device)
- **Power management for battery end devices**
  - Reduced TX power (~3 dBm instead of ~10 dBm) to save battery
  - Poll rate controller: fast polling + slow polling support
  - PowerCfg cluster for battery level monitoring
  - PollCtrl cluster to allow settings poll rate via ZCL
  - Add `BT<pin>` config option to enable battery mode

### Bugs

- **Fixed**
  - **An overload trip was never announced.** The alarm attribute is only ever
    pushed by the device and nothing polls it, but no reporting was configured
    for it - so the relay switched off correctly and Z2M went on showing
    `none`. It is now reported the moment it changes, re-sent within the hour
    so a lost report cannot leave a tripped relay looking fine all day, and
    read once during configure so the entity starts with a value instead of
    `unknown`. Existing devices need one **Reconfigure** in Z2M.
  - **"Previous" as the startup colour temperature is written as 0.** The ZCL
    sentinel for it is 0xFFFF, but zigbee-herdsman 10.6.1 caps
    `startUpColorTemperature` at 0xFEFF and never consults the sentinel its own
    definition declares, so the write was refused with INVALID_VALUE before it
    left the coordinator (and a read of 0xFFFF came back as NaN). Both are
    fixed upstream, but only in later versions. The converter now puts 0 on the
    wire, which the firmware reads as "previous" as well - 0 mireds is not a
    colour any light can show - and keeps publishing 65535 so the preset stays
    selected in the UI.
  - **A light's power-on behaviour was mismapped.** `startUpOnOff` is a ZCL
    enum - 0 off, 1 on, 2 toggle, 0xFF previous - but the light cluster
    numbered "previous" as 2, so a coordinator asking for *previous* (0xFF)
    fell through to *off* and the light never came back, while *toggle* was
    silently read as *previous*. The on/off state is now also stored in its
    own right rather than derived from the level, which is clamped to 1..254
    and so would have made "previous" always mean on. Coming back on now also
    restores the brightness the light had instead of jumping to full.
  - Lights no longer advertise an **effect** control. It maps onto
    `genIdentify` triggerEffect, which light endpoints do not carry - Identify
    is one cluster for the whole device on endpoint 1 - so the control could
    only ever answer UNSUP_COMMAND.
  - **Lights and covers could not join a Zigbee group.** The Groups cluster
    was only attached to relay endpoints, so `genGroups.add` on a light or
    cover endpoint was answered with UNSUP_COMMAND and Z2M reported the group
    add as failed. Switch endpoints stay out on purpose - a switch is a
    client, so pointing it at a group is a binding, not a membership.
  - **Fades ran longer than their transition time.** The fade stepped by a
    fixed amount per scheduler tick, so every rounded-down step cost an extra
    tick and a tick that arrived late was never made up for - the error only
    ever ran one way, and a nominal 1 s fade visibly overshot. The duty is now
    interpolated from the elapsed time, so the fade ends when it says it does
    however coarsely the scheduler ticks.
  - **Colour temperature did nothing on a tunable white.** The colour cluster
    was advertised in the endpoint descriptor but never registered with the
    Telink ZCL stack, which needs a per-cluster register function and the
    matching build flag. Z2M therefore saw the light, showed the slider, and
    every `moveToColorTemp` was dropped before it reached the firmware, while
    on/off and brightness worked normally. Reads of the colour attributes
    failed the same way — that is where the `startUpColorTemperature`
    UNSUPPORTED_ATTRIBUTE came from. Brightness-only lights (`W`) were never
    affected.
  - Lights ignored every attribute write: startup behaviour, transition time
    and startup colour temperature were applied to the running light but never
    dispatched to it or written to NV, so they were back to their defaults
    after a power cut. Writing one of them on a light endpoint also
    dereferenced a null relay and could crash the device.
  - `colorCapabilities` (mandatory) and `colorOptions` were missing. Without
    the latter the stack drops any colour command that arrives while the light
    is off, which is exactly the order a coordinator sends "on at 3000 K" in.
  - **A config string could permanently brick a device.** The parser held
    peripherals in fixed-size tables (4 switches, 6 relays, 3 cover switches,
    3 covers, 12 endpoints) but never checked them, so a longer config wrote
    past the end and corrupted the endpoint and attribute tables. The device
    stayed on the network and still answered ZCL, but every read returned
    UNSUPPORTED_ATTRIBUTE — including `device_config`, so the offending config
    could not be written back and only a re-flash by wire helped. Over-capacity
    configs now reset to the compiled-in default instead, and the Z2M converter
    refuses them before they are written.
  - Latching relays not working with off_pin A0
  - Silabs version updates not working
  - Telink End_device unreachable from Z2M after a while ([#217](https://github.com/romasku/tuya-zigbee-switch/issues/217))
  - AC noise affecting Telink GPIO
  - Changing device type breaks Silabs NVM data
  - Reset needed 11 presses instead of 10
  - ZHA quirk silently failed to apply on relay-less devices with LED indicators (scene remotes), leaving firmware attributes unreachable from the UI
- **New**
  - SONOFF ZBMINIL2 version updates broken?

## v1.1.2

_Bug-fix update_

### Bugs

- **Fixed**
  - Setting 'long press duration' to 0ms crashes device
  - Can't change device imageType in config string
  - Option 'Relay indicator - manual on' is not kept after reboot
  - Relay indicators sometimes go out-of-sync ([#38](https://github.com/romasku/tuya-zigbee-switch/issues/38))

## v1.1.1

_Critical bug-fix update for previous version_

### Changes

- Added a **hardware watchdog** on Telink devices that automatically reboots the device when it's stuck

### Bugs

- **Fixed:** Floating pin (or other device conditions) freezes the device

## v1.1.0

_Contains **substantial restructuring** of the firmware architecture, but doesn't bring new features_

### Changes

- Updated **Telink SDK** to v3.7.2.0 for **stability**
- Added support for **Silabs chips** by introducing **HAL middleware**
- Replaced GPIO polling with GPIO interrupts for **power efficiency**
- Improve **versioning** to include commit hash
- Many more technical improvements

### Bugs

- **Fixed**
  - Old SDK freezes device in some network conditions
- **New**
  - Floating pin (or other device conditions) **freezes device**
  - Setting 'long press duration' to 0ms **crashes device**
  - Can't change device imageType in config string
  - Option 'Relay indicator - manual on' is not kept after reboot
  - Relay indicators sometimes go out-of-sync ([#38](https://github.com/romasku/tuya-zigbee-switch/issues/38))
  - Telink End_device unreachable from Z2M after a while ([#217](https://github.com/romasku/tuya-zigbee-switch/issues/217))
  - Switch randomly toggles on TLSR8253 512KB devices ([#289](https://github.com/romasku/tuya-zigbee-switch/issues/289))
    (HOBEIAN and Zbeacon)
  - _Power-on behavior_ doesn't fully work on some devices
  - _momentary_nc_ not working after power loss.  
    (Apply the setting again)

## v1.0.21

### Changes

- Keep device configuration (user settings) when it is removed from the network

### New features

- Add support for Zigbee commands:
  - **off_with_effect** (0x40)
  - **on_with_recall_global_scene** (0x41)
- Add support for **normally-closed momentary buttons**
- Add **action states for toggle buttons**: position_on and position_off

## v1.0.20

### Changes

- (technical) Updated memory map: moved NV items from ZCL to APP.  
  **Due to this change, device configuration (user settings) may reset after OTA update.**

### Bugs

- **Fixed**
  - Canging config string crashed 3-4 gang devices
  - Detached mode didn't work for Toggle switches

## v1.0.19

### New features

- Add support for the **levelCtrl** cluster
  - This enables brightness control of compatible Zigbee bulbs via Zigbee binding.
  - The feature works only for momentary switches using long press: once a long press is detected, brightness will begin to slowly change. Each subsequent long press reverses the direction (increase/decrease).
  - Requires manual update of converters and reconfiguration.

### Changes

- Increase the number of **presses required to reset the device to 10.**
- Update manufacturer names to match the stock firmware.  
  (requires interview; but it's not mandatory, as backwards compatibility is kept)

### Bugs

- New bug: detached mode doesn't work for Toggle switches

## v1.0.18

- Partly fix an issue where setting the config string could brick the device.
- Technical: introduce a method to update data stored in NVRAM in new releases.

## v1.0.17

- Fix once again power on behavior = OFF not working if toggle in pressed state during boot.

## v1.0.16

- Add new toggle modes: TOGGLE_SMART_SYNC/TOGGLE_SMART_OPPOSITE (requires re-download of `switch_custom.js`).

## v1.0.15

- Add support for Zigbee groups. Read [doc](/docs/usage/endpoints.md) for details about endpoints.

## v1.0.14

- Improve code logic for Indicator LED on for switches.

## v1.0.13

- Fix power on behavior = OFF not working if toggle in pressed state during boot.
- Add way to control network state led state (requires re-download of `switch_custom.js`).

## v1.0.12

- Fix led indicator state in manual mode not preserved after reboot.
- Add forced device announcement after boot to make sure device is seen as "available" as soon as it boots.
- Restored device pictures in z2m (requires re-download of `switch_custom.js`).
- Cleaned-up z2m converter (fix typos, inconsistent names, etc.). **Warning!** This may break your automations as it changes .
  property names (requires re-download of `switch_custom.js`).

## v1.0.11

- Improve join behaviour by decreasing timeout between tries to join.
- Fix leave network: now device will send LeaveNetwork command properly.
- Display firmware version in a human-readable form.

## v1.0.10

- Add support for bi-stable relays controlled by 2 pins.
- Fix Led indicator mode not preserved after reboot.

## v1.0.9

- Fix reporting of indicator led status.

## v1.0.8

- Add support for indicator leds.
- Add way to force momentary mode as default via config.

## v1.0.7

- Add SUSPEND-based sleep to EndDevice firmware to decrease power usage ~10x.

## v1.0.6

- Add way to change device pinout on the fly, to allow easier porting of firmware .

## v1.0.5

- Keep status LED on when device is connected.
- Add separate firmwares for End Device/Router.
- Improve device boot time significantly by removing unnecessary logs .

## v1.0.4

- Fix bug that caused report to be sent every second.

## v1.0.3

- Add support of startup behaviour: ON, OFF, TOGGLE, PREVIOUS.
- Add support of button actions: 'released', 'press', 'long_press'. This is only useful for momentary (doorbell-like) switches.

## v1.0.2

- Add way to reset the device by pressing any switch button 5 times in a row .
- Fix support for ON_OFF, OFF_ON actions.

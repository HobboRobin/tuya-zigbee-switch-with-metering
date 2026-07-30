# Button actions and Home Assistant events

> Read this document if you want to trigger automations from the physical
> buttons instead of (or next to) the relay state.

Every button publishes an `action` in Zigbee2MQTT. Home Assistant turns that
into an **event entity** — the same thing you get from a Hue dimmer remote —
so a button press can start an automation without any helper or template.

The per-button `… press action` sensors are still there and unchanged. Both
representations arrive at the same time, so nothing that already listens to
the sensor needs to be touched.

## Naming

Endpoint names differ with the gang count (`switch_left`, `switch_right`, …),
so actions use a stable positional prefix instead:

| Prefix                             | Refers to                                             |
|------------------------------------|-------------------------------------------------------|
| `switch_0`, `switch_1`, …          | The first, second, … button, in endpoint order        |
| `switch_0_long`, `switch_1_long`   | The long-press companion endpoint (`2EP` config only)  |
| `cover_switch_0`, `cover_switch_1` | The first, second, … cover button                     |

## Actions

Which action a button produces follows the mode it is configured in — the
firmware already reports different states for a toggle and a momentary switch,
and the action follows automatically.

| Action                             | When                                                        |
|------------------------------------|-------------------------------------------------------------|
| `switch_0_press`                   | Momentary mode: button pushed                               |
| `switch_0_long_press`              | Momentary mode: held past the long-press duration           |
| `switch_0_release`                 | Momentary mode: button let go                               |
| `switch_0_position_on`             | Toggle mode: rocker moved to the ON position                |
| `switch_0_position_off`            | Toggle mode: rocker moved to the OFF position               |
| `switch_0_on` / `_off` / `_toggle` | The command the button sent to its bindings                 |
| `switch_0_brightness_move_up`      | Dimming started upwards (long press)                        |
| `switch_0_brightness_move_down`    | Dimming started downwards (the direction alternates)        |
| `switch_0_brightness_stop`         | Dimming stopped (button released)                           |
| `switch_0_long_toggle`             | Long press toggled the `2EP` companion endpoint's bindings  |
| `cover_switch_0_open` / `_close` / `_stop` | Cover button pushed                                 |
| `cover_switch_0_long_open` / `_long_close` | Cover button held                                   |
| `cover_switch_0_release`           | Cover button let go                                         |
| `cover_switch_0_cover_open` / `_cover_close` / `_cover_stop` | The command the cover button sent to its bindings |

The two groups come from two independent paths — the button's reported state
and the commands it sends to whatever it is bound to. Zigbee2MQTT is bound
alongside your own bindings, so both arrive and neither depends on the other.
Your own bindings are not affected.

If a device was already paired, run **Reconfigure** in Zigbee2MQTT once so the
new bindings are applied.

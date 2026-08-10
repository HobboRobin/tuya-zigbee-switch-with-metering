"""A switch can drive a light, not only a relay.

`relay_index` numbers the device's local outputs: the relays first, then the
lights. On a lamp there are no relays at all, so without this a button on the
board could only ever work through a Zigbee binding - the device would be
unable to switch its own light.

The mapping is checked on the PWM duties, because that is the only thing that
distinguishes "the switch drove the light" from "the switch drove nothing".
"""

import pytest

from client import StubProc
from conftest import Device
from zcl_consts import (
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
)

RELAY_INDEX_ALL = 0xFF

# The Gledopto layout: two buttons, two tunable whites and no relays at all.
CONFIG = "gled;X;BC0u;SB7u;SB1u;TC4C3;TC2D2;M;"
BUTTONS = ["B7", "B1"]
LIGHT_0 = ["C4", "C3"]
LIGHT_1 = ["C2", "D2"]


@pytest.fixture
def device():
    p = StubProc(device_config=CONFIG).start()
    try:
        yield Device(p)
    finally:
        p.stop()


def lit(device: Device, pins: list[str]) -> bool:
    """Is this light putting anything out?"""
    return any(
        int(device.p.exec(f"read_pwm {device._parse_pin(pin)}").payload["duty"]) > 0
        for pin in pins
    )


def press(device: Device, pin: str) -> None:
    device.press_button(pin)
    device.release_button(pin)


def set_target(device: Device, switch_endpoint: int, index: int) -> None:
    device.write_zigbee_attr(
        switch_endpoint,
        ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
        ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX,
        index,
    )


def test_each_button_starts_on_its_own_light(device: Device):
    """With no relays, output 1 and 2 are the two lights, in config order."""
    press(device, BUTTONS[0])
    assert lit(device, LIGHT_0)
    assert not lit(device, LIGHT_1)

    press(device, BUTTONS[1])
    assert lit(device, LIGHT_0)
    assert lit(device, LIGHT_1)


def test_a_button_can_be_pointed_at_the_other_light(device: Device):
    set_target(device, 1, 2)  # first switch -> second light

    press(device, BUTTONS[0])
    assert lit(device, LIGHT_1)
    assert not lit(device, LIGHT_0)


def test_all_covers_the_lights_too(device: Device):
    """`all` is about outputs, so on a lamp it means every light."""
    set_target(device, 1, RELAY_INDEX_ALL)

    press(device, BUTTONS[0])
    assert lit(device, LIGHT_0)
    assert lit(device, LIGHT_1)

    # Anything on -> everything off, the master-button rule.
    press(device, BUTTONS[0])
    assert not lit(device, LIGHT_0)
    assert not lit(device, LIGHT_1)


def test_a_mixed_state_is_levelled_rather_than_inverted(device: Device):
    press(device, BUTTONS[0])  # light_0 on, light_1 off
    set_target(device, 1, RELAY_INDEX_ALL)

    press(device, BUTTONS[0])
    assert not lit(device, LIGHT_0)
    assert not lit(device, LIGHT_1)


def test_an_index_past_the_last_output_is_rejected(device: Device):
    """Two lights and no relays means 3 is not a target - it must not be stored
    and then used as an index."""
    set_target(device, 1, 3)
    stored = int(
        device.read_zigbee_attr(
            1,
            ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
            ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX,
        )
    )
    assert stored == 1

    press(device, BUTTONS[0])
    assert lit(device, LIGHT_0)

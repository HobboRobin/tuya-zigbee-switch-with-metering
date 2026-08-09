"""A switch set to `all` drives every relay as one group.

relay_index picks what a switch drives: 0 detached, 1..N one relay, 0xFF all of
them. The last one is what makes a master button - the Opt button on the
Gledopto, or the single button on the UseeLink strip - useful.

Toggling `all` is deliberately resolved as a group rather than per relay: if
anything is on, everything goes off, otherwise everything goes on. Toggling
each relay individually would only deepen a mixed state (on/off/on becomes
off/on/off) instead of levelling it, which is not what a master button is for.
"""

import pytest

from client import StubProc
from conftest import Device
from zcl_consts import (
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
)

RELAY_INDEX_ALL = 0xFF

# One button on A0, three relays on B0/B1/B2.
CONFIG = "X;Y;SA0u;RB0;RB1;RB2;M;"
BUTTON = "A0"
RELAYS = ["B0", "B1", "B2"]


@pytest.fixture
def device():
    p = StubProc(device_config=CONFIG).start()
    try:
        d = Device(p)
        # Endpoint 1 is the switch; point it at every relay.
        d.write_zigbee_attr(1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
                            ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX,
                            RELAY_INDEX_ALL)
        yield d
    finally:
        p.stop()


def relay_states(device: Device) -> list[bool]:
    return [device.get_gpio(pin, refresh=True) for pin in RELAYS]


def test_all_relays_switch_together(device: Device):
    assert relay_states(device) == [False, False, False]

    device.click_button(BUTTON)
    assert relay_states(device) == [True, True, True]

    device.click_button(BUTTON)
    assert relay_states(device) == [False, False, False]


def test_mixed_state_is_levelled_not_inverted(device: Device):
    """The case that makes per-relay toggling wrong."""
    device.click_button(BUTTON)
    assert relay_states(device) == [True, True, True]

    # Switch one relay off behind the switch's back, as a Z2M command would.
    device.call_zigbee_cmd(2, 0x0006, 0x00)  # relay 1 off
    assert relay_states(device) == [False, True, True]

    # Anything on -> everything off. Per-relay toggling would have produced
    # [True, False, False] instead.
    device.click_button(BUTTON)
    assert relay_states(device) == [False, False, False]

    device.click_button(BUTTON)
    assert relay_states(device) == [True, True, True]


def test_all_survives_a_restart(device: Device):
    """0xFF has to pass the NV validation, which clamps 1..N."""
    value = device.read_zigbee_attr(1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
                                    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_RELAY_INDEX)
    assert int(value) == RELAY_INDEX_ALL

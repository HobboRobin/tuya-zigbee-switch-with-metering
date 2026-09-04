"""Ten activations must not wipe a device that has a sensor for an input.

The multi-press factory reset exists because on most boards the wall switch is
the only way back in. But a `S` input is not always a button someone presses:
on a door contact it is a reed, and a door opened and closed ten times in quick
succession would reset the device - which is how a Moes contact reset itself
during testing.

So the reset is a per-switch setting rather than only the device-wide press
count: the reed can be taken out of it while the tactile button next to it
keeps working as the way in.
"""

import pytest

from client import StubProc
from conftest import Device
from zcl_consts import (
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MULTI_PRESS_RESET,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
)

# A reed on PD7 and a tactile button on PD3, the Moes contact's layout.
CONFIG = "8yhypbo7;TS0203-MOES;SD7n;SD3u;"
REED, BUTTON = "D7", "D3"
RESET_PRESSES = 10


@pytest.fixture
def device():
    p = StubProc(device_config=CONFIG).start()
    try:
        yield Device(p)
    finally:
        p.stop()


def was_reset(device: Device) -> bool:
    """The stub's factory reset leaves the network, so that is the tell.

    Read from the event stream rather than the joined flag: the device starts
    steering again straight after leaving, so the flag is back up by the time
    it could be asked.
    """
    return any(e.kind == "zcl_leave_network" for e in device._events)


def hammer(device: Device, pin: str, times: int = RESET_PRESSES) -> None:
    device.clear_events()
    for _ in range(times):
        device.click_button(pin)


def set_reset(device: Device, endpoint: int, on: bool) -> None:
    device.write_zigbee_attr(
        endpoint,
        ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
        ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MULTI_PRESS_RESET,
        1 if on else 0,
    )


def test_a_switch_resets_the_device_by_default(device: Device):
    """Nobody may be locked out of a device by an upgrade."""
    hammer(device, BUTTON)
    assert was_reset(device)


def test_a_switch_taken_out_of_the_reset_does_not(device: Device):
    set_reset(device, 1, False)

    hammer(device, REED)
    assert not was_reset(device)


def test_the_other_switch_still_does(device: Device):
    """The point of doing this per switch: one input keeps the way in."""
    set_reset(device, 1, False)

    hammer(device, REED)
    assert not was_reset(device)
    hammer(device, BUTTON)
    assert was_reset(device)


def test_the_setting_is_readable_back(device: Device):
    assert int(device.read_zigbee_attr(
        1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
        ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MULTI_PRESS_RESET)) == 1
    set_reset(device, 1, False)
    assert int(device.read_zigbee_attr(
        1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
        ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MULTI_PRESS_RESET)) == 0


def test_the_setting_survives_a_restart():
    """It lives in its own NV item, so it must come back on its own."""
    with StubProc(device_config=CONFIG) as p:
        set_reset(Device(p), 1, False)

    with StubProc(device_config=CONFIG) as p:
        d = Device(p)
        assert int(d.read_zigbee_attr(
            1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
            ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_MULTI_PRESS_RESET)) == 0
        hammer(d, REED)
        assert not was_reset(d)

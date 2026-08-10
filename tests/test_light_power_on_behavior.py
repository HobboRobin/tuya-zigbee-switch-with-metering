"""What a light does after a power cut, per `startUpOnOff`.

The values are a ZCL enum and not ours to choose: 0 off, 1 on, 2 toggle and
0xFF previous. Numbering them by hand is how "previous" ends up meaning
"toggle" to every coordinator while the real 0xFF falls through to off - the
light then simply never comes back, and the setting looks broken rather than
mismapped.

"Previous" also needs the on/off state stored in its own right. Deriving it
from the level cannot work: the level is clamped to 1..254, so it is never 0
and "previous" would always mean on.
"""

import pytest

from client import StubProc
from conftest import Device
from zcl_consts import ZCL_CLUSTER_ON_OFF

ATTR_STARTUP_ONOFF = 0x4003

OFF, ON, TOGGLE, PREVIOUS = 0x00, 0x01, 0x02, 0xFF

PIN = "C4"
CONFIG = f"gled;X;BC0u;W{PIN};"
LEVEL = 100


def duty(device: Device) -> int:
    res = device.p.exec(f"read_pwm {device._parse_pin(PIN)}")
    assert res.ok, res.payload
    return int(res.payload["duty"])


def boot(startup_mode: int, leave_on: bool) -> int:
    """Set the mode, leave the light on or off, power-cycle, report the duty."""
    with StubProc(device_config=CONFIG) as p:
        d = Device(p)
        d.write_zigbee_attr(1, ZCL_CLUSTER_ON_OFF, ATTR_STARTUP_ONOFF, startup_mode)
        d.call_zigbee_cmd(1, 0x0006, 0x01)
        d.call_zigbee_cmd(1, 0x0008, 0x04, bytes([LEVEL, 0, 0]))
        if not leave_on:
            d.call_zigbee_cmd(1, 0x0006, 0x00)

    with StubProc(device_config=CONFIG) as p:
        return duty(Device(p))


def test_off_stays_dark_whatever_it_was():
    assert boot(OFF, leave_on=True) == 0
    assert boot(OFF, leave_on=False) == 0


def test_on_comes_back_at_the_brightness_it_had():
    """Not at full: coming up brighter than the user left it is its own bug."""
    assert boot(ON, leave_on=False) == LEVEL


def test_previous_restores_both_states():
    assert boot(PREVIOUS, leave_on=True) == LEVEL
    assert boot(PREVIOUS, leave_on=False) == 0


def test_toggle_comes_back_the_other_way():
    assert boot(TOGGLE, leave_on=True) == 0
    assert boot(TOGGLE, leave_on=False) == LEVEL


def test_previous_is_not_confused_with_toggle():
    """The regression: 2 and 0xFF must not be the same behaviour."""
    assert boot(PREVIOUS, leave_on=True) != boot(TOGGLE, leave_on=True)

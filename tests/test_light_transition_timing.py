"""A fade has to last as long as it says it does.

The transition time is a user-facing number in Z2M, so "1000 ms" that takes
noticeably longer on the strip is a bug even though the light ends up in the
right place. The fade is therefore interpolated from the elapsed time rather
than stepped by a fixed amount per tick: with fixed steps every rounded-down
step costs an extra tick and a late tick is never made up for, so the error is
one-directional and the fade only ever overruns.

Time is frozen here and stepped by hand, which also stands in for a scheduler
that delivers its ticks coarsely - the case that made the overrun visible.
"""

import pytest

from client import StubProc
from conftest import Device

PIN = "C4"
TRANSITION_MS = 1000
LEVEL = 254

CLUSTER_LEVEL = 0x0008
ATTR_TRANSITION = 0xFF00


@pytest.fixture
def light():
    p = StubProc(device_config=f"gled;X;BC0u;W{PIN};").start()
    try:
        d = Device(p)
        d.freeze_time()
        d.write_zigbee_attr(1, CLUSTER_LEVEL, ATTR_TRANSITION, TRANSITION_MS)
        d.call_zigbee_cmd(1, 0x0006, 0x01)                       # on
        d.call_zigbee_cmd(1, 0x0008, 0x04, bytes([LEVEL, 0, 0]))  # to full
        d.step_time(TRANSITION_MS)  # let the fade in settle before measuring
        yield d
    finally:
        p.stop()


def duty(device: Device) -> int:
    res = device.p.exec(f"read_pwm {device._parse_pin(PIN)}")
    assert res.ok, res.payload
    return int(res.payload["duty"])


def test_fade_is_finished_when_the_transition_is_over(light: Device):
    assert duty(light) == LEVEL
    light.call_zigbee_cmd(1, 0x0006, 0x00)  # off, fading out

    light.step_time(TRANSITION_MS)
    assert duty(light) == 0


def test_fade_is_only_half_way_at_half_time(light: Device):
    light.call_zigbee_cmd(1, 0x0006, 0x00)

    light.step_time(TRANSITION_MS // 2)
    half = duty(light)
    assert 100 < half < 155, f"expected roughly half of {LEVEL}, got {half}"


def test_a_coarse_tick_does_not_stretch_the_fade(light: Device):
    """Two long ticks instead of fifty short ones still end on time.

    This is the regression: stepping by a fixed amount per tick would have got
    a fiftieth of the way each time and needed 50 ticks no matter how much time
    had passed.
    """
    light.call_zigbee_cmd(1, 0x0006, 0x00)

    light.step_time(TRANSITION_MS // 2)
    light.step_time(TRANSITION_MS // 2)
    assert duty(light) == 0

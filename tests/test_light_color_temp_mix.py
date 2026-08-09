"""A tunable white has to hand the level over from one channel to the other.

The cold and warm channels are mixed from where the requested colour
temperature sits in the mired range, so at the cold end the warm channel must
be fully off and at the warm end the cold one must be - anything less and the
light never actually reaches the colour it was asked for.

Checked on the PWM duties rather than by eye on a strip: the firmware is then
provably right or wrong on its own, which matters because "cold stays on" looks
identical whether the mixing is broken or the two outputs are simply wired the
other way round.
"""

import pytest

from client import StubProc
from conftest import Device

COLD, WARM = "C4", "C3"
LEVEL = 254

# (mireds, cold duty, warm duty) over the firmware's 167..333 range.
CASES = [
    (167, 254, 0),    # 6000 K, fully cold
    (208, 192, 62),
    (250, 127, 127),  # halfway
    (291, 65, 189),
    (333, 0, 254),    # 3000 K, fully warm
]


@pytest.fixture
def light():
    p = StubProc(device_config=f"gled;X;BC0u;T{COLD}{WARM};").start()
    try:
        d = Device(p)
        d.call_zigbee_cmd(1, 0x0006, 0x01)                      # on
        d.call_zigbee_cmd(1, 0x0008, 0x04, bytes([LEVEL, 0, 0]))  # moveToLevel
        yield d
    finally:
        p.stop()


def duty(device: Device, pin: str) -> int:
    res = device.p.exec(f"read_pwm {device._parse_pin(pin)}")
    assert res.ok, res.payload
    return int(res.payload["duty"])


def inverted(device: Device, pin: str) -> int:
    res = device.p.exec(f"read_pwm {device._parse_pin(pin)}")
    assert res.ok, res.payload
    return int(res.payload["inverted"])


def set_color_temp(device: Device, mireds: int) -> None:
    device.call_zigbee_cmd(1, 0x0300, 0x0A,
                           bytes([mireds & 0xFF, mireds >> 8, 0, 0]))


@pytest.mark.parametrize("mireds,cold,warm", CASES)
def test_mix_follows_colour_temperature(light: Device, mireds, cold, warm):
    set_color_temp(light, mireds)
    assert (duty(light, COLD), duty(light, WARM)) == (cold, warm)


def test_total_output_stays_constant(light: Device):
    """Moving the colour must not change how bright the light is."""
    for mireds, *_ in CASES:
        set_color_temp(light, mireds)
        assert duty(light, COLD) + duty(light, WARM) == LEVEL


def test_channels_default_to_active_high(light: Device):
    assert (inverted(light, COLD), inverted(light, WARM)) == (0, 0)


def test_i_flag_inverts_every_channel_of_a_light():
    """`T<cold><warm>i` is the answer to a board that drives its LEDs low.

    The flags sit after *both* pins, so a tunable white must not apply them to
    the cold channel alone - half an inversion would look like a colour bug on
    the strip rather than like a wiring one.
    """
    p = StubProc(device_config=f"gled;X;BC0u;T{COLD}{WARM}i;").start()
    try:
        d = Device(p)
        assert (inverted(d, COLD), inverted(d, WARM)) == (1, 1)
        # The duty the firmware asks for is unchanged; only the pin polarity is.
        d.call_zigbee_cmd(1, 0x0006, 0x01)
        d.call_zigbee_cmd(1, 0x0008, 0x04, bytes([LEVEL, 0, 0]))
        set_color_temp(d, 167)
        assert (duty(d, COLD), duty(d, WARM)) == (254, 0)
    finally:
        p.stop()


def test_i_flag_inverts_a_single_channel_dimmer():
    p = StubProc(device_config=f"gled;X;BC0u;W{COLD}i;W{WARM};").start()
    try:
        d = Device(p)
        assert (inverted(d, COLD), inverted(d, WARM)) == (1, 0)
    finally:
        p.stop()


def test_out_of_range_clamps_to_the_ends(light: Device):
    """Z2M clamps to the advertised range, but nothing else has to."""
    set_color_temp(light, 50)
    assert (duty(light, COLD), duty(light, WARM)) == (254, 0)
    set_color_temp(light, 600)
    assert (duty(light, COLD), duty(light, WARM)) == (0, 254)

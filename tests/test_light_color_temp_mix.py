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


COLOR_CLUSTER = 0x0300
ATTR_COLOR_TEMP = 0x0007
ATTR_COLOR_OPTIONS = 0x000F
ATTR_COLOR_CAPABILITIES = 0x400A
ATTR_STARTUP_COLOR_TEMP = 0x4010


def test_colour_cluster_carries_the_attributes_a_coordinator_reads(light: Device):
    """A missing attribute here is not cosmetic - it is a failed read in Z2M.

    `colorCapabilities` is how a coordinator tells a tunable white from a full
    colour light, and `startUpColorTemperature` is what the "colour temp
    startup" option writes; both used to come back UNSUPPORTED_ATTRIBUTE.
    """
    # Bitmaps read back as hex, plain integers as decimal.
    caps = int(light.read_zigbee_attr(1, COLOR_CLUSTER, ATTR_COLOR_CAPABILITIES), 16)
    assert caps == 0x0010  # colour temperature, and nothing else
    options = int(light.read_zigbee_attr(1, COLOR_CLUSTER, ATTR_COLOR_OPTIONS), 16)
    assert options & 0x01  # obey colour commands while off
    assert int(light.read_zigbee_attr(1, COLOR_CLUSTER, ATTR_STARTUP_COLOR_TEMP)) == 0xFFFF


def test_startup_colour_temperature_is_honoured():
    """0xFFFF means "come back at the previous colour"; anything else pins it."""
    config = f"gled;X;BC0u;T{COLD}{WARM};"

    with StubProc(device_config=config) as p:
        Device(p).write_zigbee_attr(1, COLOR_CLUSTER, ATTR_STARTUP_COLOR_TEMP, 333)

    with StubProc(device_config=config) as p:
        d = Device(p)
        assert int(d.read_zigbee_attr(1, COLOR_CLUSTER, ATTR_COLOR_TEMP)) == 333
        d.call_zigbee_cmd(1, 0x0006, 0x01)
        d.call_zigbee_cmd(1, 0x0008, 0x04, bytes([LEVEL, 0, 0]))
        assert (duty(d, COLD), duty(d, WARM)) == (0, 254)


def test_out_of_range_clamps_to_the_ends(light: Device):
    """Z2M clamps to the advertised range, but nothing else has to."""
    set_color_temp(light, 50)
    assert (duty(light, COLD), duty(light, WARM)) == (254, 0)
    set_color_temp(light, 600)
    assert (duty(light, COLD), duty(light, WARM)) == (0, 254)

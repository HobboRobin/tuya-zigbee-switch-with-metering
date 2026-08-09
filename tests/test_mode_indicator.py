"""The Y status LED names the light mode by colour.

Y<r><g><b> registers three leds as one indicator and lights only the
combination standing for the configured mode: white = RGB+CCT, yellow = RGBW,
blue = RGB, green = tunable white, red = plain dimmer. That way a strip's mode
is readable off the device instead of only out of the config string.

The leds not in the colour are driven off rather than left alone - otherwise a
mode change would leave the previous colour's leds burning.
"""

import pytest

from client import StubProc
from conftest import Device

RED, GREEN, BLUE = "A0", "A1", "A7"

CASES = [
    # (light tokens, leds expected to be lit)
    pytest.param("TC4C3;TC2D2;", {GREEN}, id="tunable-white-is-green"),
    pytest.param("WC4;WC3;WC2;", {RED}, id="dimmer-is-red"),
    # A tunable white next to a plain dimmer is still a tunable-white device:
    # the colour follows the widest light, not the first one.
    pytest.param("WC4;TC3C2;", {GREEN}, id="widest-light-wins"),
    # Nothing to report a mode for, so the indicator stays a plain status LED.
    pytest.param("RB0;", {RED, GREEN, BLUE}, id="no-light-uses-all"),
]


@pytest.mark.parametrize("tail,expected", CASES)
def test_mode_colour(tail: str, expected: set[str]):
    p = StubProc(device_config=f"gled;X;BC0u;Y{RED}{GREEN}{BLUE};{tail}").start()
    try:
        device = Device(p)
        lit = {pin for pin in (RED, GREEN, BLUE) if device.get_gpio(pin, refresh=True)}
        assert lit == expected
    finally:
        p.stop()

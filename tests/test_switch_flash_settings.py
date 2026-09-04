"""The confirmation flash has to be configurable on a board that has no relay.

On a relay-less board - a remote, a door contact - the indicator LED's only job
is the short flash that says "the press registered". Until now that flash could
be neither dimmed nor turned off: brightness and transition lived on the relay
endpoint, and a device with no relay has none, so the settings simply did not
exist for the devices that need them most.

They now sit on the switch itself, where the flash actually belongs, and only
where no relay owns the LED - with a relay attached the LED shows the relay's
state and never flashes, so offering the controls there would advertise
settings that do nothing.
"""

import pytest

from client import StubProc
from conftest import Device
from zcl_consts import (
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH,
    ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH_BRIGHTNESS,
    ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG,
)

# A switch and a dimmable indicator LED, no relay: the Moes contact's shape.
CONFIG = "mfg;X;SA0u;IB4p;"
SWITCH, LED = "A0", "B4"


@pytest.fixture
def device():
    p = StubProc(device_config=CONFIG).start()
    try:
        yield Device(p)
    finally:
        p.stop()


def duty(device: Device) -> int:
    res = device.p.exec(f"read_pwm {device._parse_pin(LED)}")
    assert res.ok, res.payload
    return int(res.payload["duty"])


def read(device: Device, attr: int) -> int:
    return int(device.read_zigbee_attr(1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, attr))


def write(device: Device, attr: int, value: int) -> None:
    device.write_zigbee_attr(1, ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG, attr, value)


def flashed(device: Device) -> bool:
    """Press and watch the LED come up during the blink's on-phase."""
    device.set_gpio(SWITCH, 0)
    device.step_time(60)  # past the debounce, inside the 50 ms on-phase
    lit = duty(device) > 0
    device.set_gpio(SWITCH, 1)
    device.step_time(200)
    return lit


def test_the_flash_is_on_by_default(device: Device):
    """It is the only feedback a relay-less board gives, so it stays default."""
    assert read(device, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH) == 1
    assert flashed(device)


def test_the_flash_can_be_turned_off(device: Device):
    write(device, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH, 0)

    assert not flashed(device)


def test_turning_it_back_on_brings_it_back(device: Device):
    write(device, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH, 0)
    write(device, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH, 1)

    assert flashed(device)


def test_the_flash_brightness_reaches_the_led(device: Device):
    write(device, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH_BRIGHTNESS, 40)

    device.set_gpio(SWITCH, 0)
    device.step_time(60)
    dimmed = duty(device)
    device.set_gpio(SWITCH, 1)
    device.step_time(200)

    assert 0 < dimmed <= 40


def test_both_settings_survive_a_restart():
    with StubProc(device_config=CONFIG) as p:
        d = Device(p)
        write(d, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH, 0)
        write(d, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH_BRIGHTNESS, 40)

    with StubProc(device_config=CONFIG) as p:
        d = Device(p)
        assert read(d, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH) == 0
        assert read(d, ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH_BRIGHTNESS) == 40


def test_a_switch_with_a_relay_is_not_offered_the_settings():
    """The relay owns the LED there, so the flash never happens."""
    with StubProc(device_config="mfg;X;SA0u;RB0;IB4p;") as p:
        res = p.exec(
            f"zcl_read 1 0x{ZCL_CLUSTER_ON_OFF_SWITCH_CONFIG:04X}"
            f" 0x{ZCL_ATTR_ONOFF_CONFIGURATION_SWITCH_FLASH:04X}"
        )
        assert not res.ok

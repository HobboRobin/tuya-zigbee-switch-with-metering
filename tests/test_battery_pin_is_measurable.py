"""A battery pin the chip cannot measure reports an empty cell, not an error.

Only ten pins on the TLSR8258 are wired to the converter (PB0-PB7, PC4, PC5).
Give `adc_vbat_pin_init` anything else and its lookup falls through with
channel 0 - "no input" - so the chip measures nothing and the cell reads flat.
Nothing logs, nothing fails; the device just claims its battery is dead.

The second way to get it wrong is to name a pin another peripheral already
owns. Sensing drives the pin high to measure it against ground, so a shared pin
gets driven behind the back of the switch or LED that owns it.

Both are easy to write when a config string is copied from a stock pinout, so
the firmware takes any free measurable pin instead - the reading is the supply
voltage whichever pin it is - and the device database is held to the same rule
so the string says what actually happens.
"""

import re
from pathlib import Path

import pytest
import yaml

from client import StubProc
from conftest import Device
from zcl_consts import (
    ZCL_ATTR_POWER_CFG_BATTERY_VOLTAGE,
    ZCL_CLUSTER_POWER_CFG,
)

DEVICE_DB = Path("device_db.yaml")
ADC_PINS = {f"B{i}" for i in range(8)} | {"C4", "C5"}
BATTERY_REFRESH_INTERVAL_MS = 300000


def pins_used_by(parts: list[str]) -> set[str]:
    """Every pin the config claims, battery aside. Pins are two chars."""
    used = set()
    for entry in parts[2:]:
        if entry.startswith("BT") or entry in ("M", "SLP", "2EP"):
            continue
        if not entry or entry[0] not in "SRIXLBWTCY":
            continue
        if entry[0] == "Y" and len(entry) >= 7:  # mode LED: three pins
            used.update({entry[1:3], entry[3:5], entry[5:7]})
            continue
        if len(entry) >= 3:
            used.add(entry[1:3])
        if entry[0] in ("T", "C") and len(entry) >= 5:  # two-pin peripherals
            used.add(entry[3:5])
    return used


@pytest.fixture(scope="module")
def battery_devices() -> dict[str, tuple[str, set[str]]]:
    db = yaml.safe_load(DEVICE_DB.read_text())
    devices = db.get("devices", db)
    out = {}
    for name, device in devices.items():
        if not isinstance(device, dict):
            continue
        config_str = device.get("config_str") or ""
        parts = [p for p in config_str.split(";") if p]
        battery = [p for p in parts if p.startswith("BT")]
        if not battery:
            continue
        out[name] = (battery[0][2:4], pins_used_by(parts))
    return out


def test_every_battery_pin_can_actually_be_measured(battery_devices):
    wrong = {
        name: pin
        for name, (pin, _) in battery_devices.items()
        if pin not in ADC_PINS
    }
    assert not wrong, f"battery pins with no ADC channel: {wrong}"


def test_no_battery_pin_is_shared_with_another_peripheral(battery_devices):
    shared = {
        name: pin for name, (pin, used) in battery_devices.items() if pin in used
    }
    assert not shared, f"battery pins another peripheral already owns: {shared}"


def voltage(device: Device) -> int:
    """The ZCL attribute is in 100 mV units."""
    return int(device.read_zigbee_attr(
        1, ZCL_CLUSTER_POWER_CFG, ZCL_ATTR_POWER_CFG_BATTERY_VOLTAGE))


def test_an_unmeasurable_pin_falls_back_to_one_that_works():
    """`BTA0` has no channel, so the firmware must not take it at its word."""
    with StubProc(device_config="mfg;X;SC0u;BTA0;") as p:
        d = Device(p)
        d.set_battery_voltage(2900)
        d.step_time(BATTERY_REFRESH_INTERVAL_MS + 1)
        assert voltage(d) == 29


def test_a_pin_another_peripheral_owns_is_not_taken_either():
    with StubProc(device_config="mfg;X;SB5u;BTB5;") as p:
        d = Device(p)
        d.set_battery_voltage(2900)
        d.step_time(BATTERY_REFRESH_INTERVAL_MS + 1)
        assert voltage(d) == 29
        # The switch still owns its pin: pressing it has to still work.
        d.click_button("B5")


def test_a_good_pin_is_left_alone():
    with StubProc(device_config="mfg;X;SC0u;BTB5;") as p:
        d = Device(p)
        d.set_battery_voltage(2900)
        d.step_time(BATTERY_REFRESH_INTERVAL_MS + 1)
        assert voltage(d) == 29

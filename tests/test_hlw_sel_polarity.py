"""SEL-pin polarity switch for HLW8012 vs BL0937 boards.

The SEL pin selects whether CF1 carries voltage or current, and the HLW8012 and
the BL0937 use opposite levels for that choice. A board fitted with the other
chip therefore reads voltage and current swapped until `I` is appended to the
EP token, which flips the mapping without a firmware rebuild.
"""

import pytest

from client import StubProc
from conftest import Device

ZCL_CLUSTER_ELEC_MEAS = 0x0B04
ATTR_RMS_VOLTAGE = 0x0505
ATTR_RMS_CURRENT = 0x0508

CF1_PIN = "C2"  # EP<CF=C0><CF1=C2><SEL=C1>
NORMAL_CONFIG = "Stub;Stub;SB5u;RD2;EPC0C2C1;M;"
INVERTED_CONFIG = "Stub;Stub;SB5u;RD2;EPC0C2C1I;M;"


def _feed_cf1_and_read(config: str) -> tuple[int, int]:
    """Feed a fixed CF1 pulse count and return (rms_voltage, rms_current)."""
    with StubProc(device_config=config) as proc:
        device = Device(proc)
        device.freeze_time()
        pin = device._parse_pin(CF1_PIN)
        # Run a few sample intervals so a measurement is actually latched
        # (the sample right after a SEL toggle is skipped by design).
        for _ in range(3):
            device.p.exec(f"set_counter {pin} 5000")
            device.step_time(5000)
            device.step_time(50)
        voltage = int(
            device.read_zigbee_attr(1, ZCL_CLUSTER_ELEC_MEAS, ATTR_RMS_VOLTAGE)
        )
        current = int(
            device.read_zigbee_attr(1, ZCL_CLUSTER_ELEC_MEAS, ATTR_RMS_CURRENT)
        )
        return voltage, current


def test_default_polarity_reads_cf1_as_voltage():
    """Without the flag (HLW8012), the pulses land on the voltage channel."""
    voltage, current = _feed_cf1_and_read(NORMAL_CONFIG)
    assert voltage > 0
    assert current == 0


def test_inverted_polarity_reads_cf1_as_current():
    """With `I` (BL0937), the very same pulses land on the current channel."""
    voltage, current = _feed_cf1_and_read(INVERTED_CONFIG)
    assert current > 0
    assert voltage == 0


def test_inverted_flag_coexists_with_calibration_markers():
    """The I flag must not be mistaken for / swallowed by the V/A/W markers."""
    voltage, current = _feed_cf1_and_read(
        "Stub;Stub;SB5u;RD2;EPC0C2C1IV154714A137765W16193;M;"
    )
    assert current > 0
    assert voltage == 0


@pytest.mark.parametrize("key", ["OUTLET_BSEED_PM_TS011F_2uollq9d"])
def test_bl0937_device_is_registered(key: str):
    """The BL0937 variant of the BSEED PM outlet exists and stays short."""
    from pathlib import Path

    import yaml

    db = yaml.safe_load(Path("device_db.yaml").read_text())
    assert key in db, f"{key} missing from device_db.yaml"
    config = db[key]["config_str"]
    assert len(config) <= 71, f"config_str too long: {len(config)}"

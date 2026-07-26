"""Factory calibration must live in the device_db multiplier fields, not in the
config_str.

Baking the multipliers into the firmware (hlw8012_*/bl0942_* fields, passed as
-D defines by board.mk) means a freshly flashed device reads correctly without
anyone writing calibration_values by hand. Keeping them out of the config_str
also keeps it short enough to be read over Zigbee in a single packet — inline
markers are what made the BSEED PM outlets' config_str unreadable.

A calibration stored in NVM still takes precedence over the compiled default,
so per-unit fine-tuning is unaffected.
"""

import re
from pathlib import Path

import pytest
import yaml

MULTIPLIER_FIELDS = {
    "EP": ("hlw8012_voltage_multiplier", "hlw8012_current_multiplier",
           "hlw8012_power_multiplier"),
    "EB": ("bl0942_voltage_multiplier", "bl0942_current_multiplier",
           "bl0942_power_multiplier"),
}

# Metering devices whose calibration has been measured on hardware. These must
# carry compile-time multipliers so a flashed unit is calibrated out of the box.
CALIBRATED_DEVICES = [
    "OUTLET_NOUS_A1Z_TS011F",
    "OUTLET_BSEED_PM_TS011F_b28wrpvx",
    "OUTLET_BSEED_PM_TS011F_4ux0ondb",
    "OUTLET_BSEED_PM_TS011F_2uollq9d",
]


def _db() -> dict:
    return yaml.safe_load(Path("device_db.yaml").read_text())


def _meter_token(config: str) -> tuple[str, str] | None:
    """Return (meter kind, trailing calibration markers) for EP/EB configs."""
    match = re.search(r";(EP|EB)((?:[A-D]\d){2,3})((?:[VAWS]\d+)*)", config)
    if not match:
        return None
    return match.group(1), match.group(3)


def test_no_metering_device_has_inline_calibration():
    """No config_str may carry inline V/A/W calibration markers any more."""
    offenders = {}
    for key, dev in _db().items():
        if not dev.get("build", True):
            continue
        config = dev.get("config_str")
        if not config or str(config).startswith("null"):
            continue
        parsed = _meter_token(config)
        if not parsed:
            continue
        markers = re.findall(r"[VAW]\d+", parsed[1])
        if markers:
            offenders[key] = markers
    assert not offenders, (
        "calibration belongs in the hlw8012_*/bl0942_* device_db fields, "
        f"not in the config_str: {offenders}"
    )


@pytest.mark.parametrize("key", CALIBRATED_DEVICES)
def test_calibrated_devices_ship_compile_time_multipliers(key: str):
    """Hardware-measured devices carry all three multipliers for their meter."""
    dev = _db()[key]
    parsed = _meter_token(dev["config_str"])
    assert parsed, f"{key} has no EP/EB meter token"
    fields = MULTIPLIER_FIELDS[parsed[0]]
    missing = [f for f in fields if not dev.get(f)]
    assert not missing, f"{key} is missing compile-time calibration: {missing}"


def test_multipliers_match_the_meter_kind():
    """A device must not carry multipliers for a meter it does not have."""
    wrong = {}
    for key, dev in _db().items():
        config = dev.get("config_str")
        if not config or str(config).startswith("null"):
            continue
        parsed = _meter_token(config)
        used = [
            f
            for fields in MULTIPLIER_FIELDS.values()
            for f in fields
            if dev.get(f)
        ]
        if not used:
            continue
        if not parsed:
            wrong[key] = "multipliers but no meter"
            continue
        expected = set(MULTIPLIER_FIELDS[parsed[0]])
        stray = [f for f in used if f not in expected]
        if stray:
            wrong[key] = f"{parsed[0]} device carries {stray}"
    assert not wrong, wrong

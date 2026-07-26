"""Guard against config_str values that are too long to survive a ZCL read or
write over the air.

The config_str is exposed as genBasic attribute 0xFF00 (LONG_CHAR_STR). A ZCL
read response costs `len(config_str) + 9` bytes (3 ZCL header + 4 attribute
header + 2 length), and a Zigbee packet carries only ~74-80 bytes of payload
once NWK security overhead is subtracted. APS fragmentation is not enabled in
this project, so anything beyond that is silently dropped: Z2M then shows an
empty config_str and writes appear to "reset" themselves.

Observed in the field: 72 and 74 character config_strs (81/83 ZCL bytes) were
unreadable, while 62 characters (71 bytes) still worked.
"""

import re
from pathlib import Path

import pytest
import yaml

# Bytes of ZCL overhead around the string itself.
ZCL_OVERHEAD = 9
# Conservative single-packet payload budget.
MAX_ZCL_BYTES = 80
MAX_CONFIG_CHARS = MAX_ZCL_BYTES - ZCL_OVERHEAD  # 71

# Devices that already exceeded the budget before it was enforced. They are
# long because of their pin count (4-gang and similar), not because of anything
# removable, so they cannot simply be shortened. They are expected to show the
# same symptoms and need a real fix (shorter model name, or APS fragmentation).
# Do not add new entries here — shorten the config_str instead.
KNOWN_TOO_LONG = {
    "SWITCH_MILFRA_TS0004",
    "SWITCH_MOES_SCENE_TS0726_3GANG_3SCENE",
    "SWITCH_TUYA_A_TS0004",
}


def _devices():
    db = yaml.safe_load(Path("device_db.yaml").read_text())
    for key, dev in db.items():
        if not dev.get("build", True):
            continue
        config = dev.get("config_str")
        if not config or str(config).startswith("null"):
            continue
        yield key, config


def test_no_new_config_str_exceeds_single_packet():
    """Every config_str must fit in one Zigbee packet (except known legacy)."""
    offenders = {
        key: len(config)
        for key, config in _devices()
        if len(config) > MAX_CONFIG_CHARS and key not in KNOWN_TOO_LONG
    }
    assert not offenders, (
        "config_str too long to read/write over Zigbee "
        f"(max {MAX_CONFIG_CHARS} chars): {offenders}"
    )


@pytest.mark.parametrize(
    "key",
    ["OUTLET_BSEED_PM_TS011F_b28wrpvx", "OUTLET_BSEED_PM_TS011F_4ux0ondb"],
)
def test_metering_outlets_have_no_inline_calibration(key: str):
    """The BSEED PM outlets must keep calibration out of the config_str: the
    inline V/A/W multipliers pushed them to 72/74 chars, which broke reading and
    writing the config entirely. The values live in calibration_values instead."""
    config = dict(_devices())[key]
    assert len(config) <= MAX_CONFIG_CHARS, f"{key} is {len(config)} chars"
    # The meter token is EP<CF><CF1><SEL> (three 2-char pins); calibration
    # markers, if any, follow those 6 characters as V/A/W plus digits.
    meter_token = config.split("EP")[1].split(";")[0]
    calibration = re.findall(r"[VAW]\d+", meter_token[6:])
    assert not calibration, (
        f"{key} still has inline calibration markers: {calibration}"
    )


def test_known_too_long_list_is_accurate():
    """Keep the legacy list honest: an entry that now fits must be removed."""
    lengths = dict(_devices())
    stale = {
        key
        for key in KNOWN_TOO_LONG
        if key in lengths and len(lengths[key]) <= MAX_CONFIG_CHARS
    }
    assert not stale, f"These now fit and must leave KNOWN_TOO_LONG: {stale}"

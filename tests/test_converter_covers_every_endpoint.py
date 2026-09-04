"""Every switch a device declares must be wired up in the generated converter.

A switch endpoint that the converter does not know about is silent in the most
confusing way possible: the firmware detects the press and updates its
multistate attribute, but nothing is bound and no reporting is configured, so
nothing goes on the air at all. From the outside it looks exactly like a dead
input - which is how one Moes contact cost an evening of chasing a GPIO
interrupt bug that was not there.

So the invariant is checked straight from `device_db.yaml`: for every device,
count the switches in its config string and require the converter to bind,
configure reporting on, and expose an action for each of their endpoints.
"""

import re
from pathlib import Path

import pytest
import yaml

CONVERTER = Path("zigbee2mqtt/converters/switch_custom.js")
DEVICE_DB = Path("device_db.yaml")


def switch_count(config_str: str) -> int:
    """`S<pin>` is a switch; `SLP` is the sleep flag and no input at all."""
    return sum(
        1
        for entry in config_str.split(";")
        if entry.startswith("S") and not entry.startswith("SLP")
    )


@pytest.fixture(scope="module")
def blocks() -> dict[str, str]:
    """The converter, split into one text block per device definition."""
    out = {}
    for block in CONVERTER.read_text().split("        zigbeeModel: [")[1:]:
        for model in re.findall(r'"([^"]+)"', block.split("]", 1)[0]):
            out[model] = block
    return out


@pytest.fixture(scope="module")
def devices() -> dict[str, str]:
    """zigbee model -> config string, for every device that has switches."""
    db = yaml.safe_load(DEVICE_DB.read_text())
    devices = db.get("devices", db)
    out = {}
    for name, device in devices.items():
        if not isinstance(device, dict):
            continue
        config_str = device.get("config_str") or ""
        parts = config_str.split(";")
        if len(parts) < 2 or not switch_count(config_str):
            continue
        out[parts[1]] = config_str
    return out


def test_the_db_and_the_converter_agree_on_how_many_switches_there_are(
    blocks, devices
):
    wrong = {}
    for model, config_str in devices.items():
        block = blocks.get(model)
        if block is None:
            continue
        declared = len(re.findall(r"\{endpoint: \d+, prefix: \"switch_\d+\"",
                                  block))
        if declared != switch_count(config_str):
            wrong[model] = (switch_count(config_str), declared)
    assert not wrong, f"config_str switches vs actionEvent entries: {wrong}"


def test_every_switch_endpoint_is_bound_and_reports(blocks, devices):
    """Without both of these the endpoint never sends anything."""
    missing = {}
    for model, config_str in devices.items():
        block = blocks.get(model)
        if block is None:
            continue
        configure = block.split("configure: async", 1)[-1]
        for endpoint in range(1, switch_count(config_str) + 1):
            bound = f'reporting.bind(endpoint{endpoint}, coordinatorEndpoint, ["genMultistateInput"])'
            reports = re.search(
                rf"endpoint{endpoint}\.configureReporting\(\"genMultistateInput\"",
                configure,
            )
            if bound not in configure or not reports:
                missing.setdefault(model, []).append(endpoint)
    assert not missing, f"switch endpoints that would stay silent: {missing}"


def test_the_moes_contact_exposes_both_of_its_inputs(devices):
    """The reed and the tactile button are two switches, not one.

    Kept explicit because this device is the reason the check exists: it ran
    with a one-switch config string while the board had two inputs wired.
    """
    assert switch_count(devices["TS0203-MOES"]) == 2

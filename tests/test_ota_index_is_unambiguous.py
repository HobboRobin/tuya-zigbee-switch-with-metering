"""Two boards must not answer to the same stock name with different hardware.

Zigbee2MQTT picks a migration image by manufacturer code, image type and the
device's manufacturer name. For the migration image those first two are the
*stock* ones, shared across dozens of boards, so the manufacturer name is all
that separates them. If two entries claim the same one, a stock device can be
handed the other board's firmware - and since the pinout comes from the config
string baked into that firmware, that means relays and switches on the wrong
pins.

Sharing a name is fine as long as the boards are the same hardware: several
entries legitimately cover one module under an old shared name, and either
image drives it correctly. What must not happen is a shared name across
different pinouts, which is how a two-gang switch came to be offered the
three-gang image.

Only the names a *stock* device reports count here - its own manufacturer name
and any earlier ones. The custom name from the config string is what a device
reports once it already runs this firmware; it rides along in the index for
recovery, and a device that reports it is past the point this check protects.
"""

from collections import defaultdict
from pathlib import Path

import pytest
import yaml

DEVICE_DB = Path("device_db.yaml")

# Device-wide defaults rather than wiring: they change how the switches behave,
# not which pin anything is on, and they are settable afterwards anyway.
NOT_WIRING = {"M", "SLP", "2EP"}


def peripherals(config_str: str) -> tuple[str, ...]:
    """The hardware a config string describes: everything but the names."""
    parts = [p for p in config_str.split(";") if p]
    return tuple(sorted(p for p in parts[2:] if p not in NOT_WIRING))


@pytest.fixture(scope="module")
def migration_claims() -> dict[tuple, dict[str, tuple]]:
    """(manufacturer code, image type, stock name) -> {board: its hardware}."""
    db = yaml.safe_load(DEVICE_DB.read_text())
    devices = db.get("devices", db)
    claims = defaultdict(dict)
    for name, device in devices.items():
        if not isinstance(device, dict):
            continue
        code = device.get("stock_manufacturer_id")
        image_type = device.get("stock_image_type")
        config_str = device.get("config_str") or ""
        if code is None or image_type is None or not config_str:
            continue
        stock_names = []
        if device.get("stock_manufacturer_name"):
            stock_names.append(device["stock_manufacturer_name"])
        stock_names.extend(device.get("old_manufacturer_names") or [])
        for claimed in stock_names:
            claims[(code, image_type, claimed)][name] = peripherals(config_str)
    return claims


def test_no_stock_name_maps_to_two_different_pinouts(migration_claims):
    clashes = {
        key: sorted(boards)
        for key, boards in migration_claims.items()
        if len(set(boards.values())) > 1
    }
    assert not clashes, (
        "a stock device answering one of these names could be handed either "
        f"board's image, and they are wired differently: {clashes}"
    )


def test_the_lonsonho_two_and_three_gang_stay_apart(migration_claims):
    """The case this check was written for.

    `_TZ3000_aa5t61rh` is the two-gang X702A and `_TZ3000_rhkfbfcv` the
    three-gang X703A - both per zigbee-herdsman-converters, and the three-gang's
    own config string already said `rhkfbfcv`. Its stock name had been copied
    from the two-gang, so either device could be offered the other's firmware.
    """
    def boards_for(stock_name: str) -> set[str]:
        return {
            board
            for (_, _, name), boards in migration_claims.items()
            if name == stock_name
            for board in boards
        }

    assert boards_for("_TZ3000_aa5t61rh") == {"SWITCH_TUYA_A_TS0002"}
    assert boards_for("_TZ3000_rhkfbfcv") == {"SWITCH_TUYA_A_TS0003"}


def test_every_board_is_reachable_by_some_name(migration_claims):
    """A board offering a migration image nobody can match is dead weight."""
    reachable = {board for boards in migration_claims.values() for board in boards}
    db = yaml.safe_load(DEVICE_DB.read_text())
    devices = db.get("devices", db)
    unreachable = sorted(
        name
        for name, device in devices.items()
        if isinstance(device, dict)
        and device.get("stock_manufacturer_id") is not None
        and device.get("stock_image_type") is not None
        and device.get("stock_manufacturer_name")
        and name not in reachable
    )
    assert not unreachable, unreachable

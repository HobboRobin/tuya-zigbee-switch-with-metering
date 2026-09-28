"""Empty the Zigbee2MQTT OTA index files before a full rebuild.

Every board build appends its own entries, so the release build starts from
empty indexes. With --keep-mcu, entries for boards on that MCU family are kept
instead: those boards are not being rebuilt, and dropping their entries would
leave every device of that kind without an update to offer.

That is the situation when the Silicon Labs tools cannot be downloaded. Their
entries point at files pinned to a commit, so the old files stay reachable.
"""

import argparse
import json
import re
from pathlib import Path

import yaml

INDEX_FILES = [
    "index_router.json",
    "index_end_device.json",
    "index_router-FORCE.json",
    "index_end_device-FORCE.json",
]

BOARD_IN_URL = re.compile(r"/bin/(?:router|end_device)/([^/]+)/")


def board_of(entry: dict) -> str | None:
    match = BOARD_IN_URL.search(entry.get("url", ""))
    if not match:
        return None
    return re.sub(r"_END_DEVICE$", "", match.group(1))


def boards_on_mcu(db: dict, mcu_prefix: str) -> set[str]:
    return {
        name
        for name, device in db.items()
        if str(device.get("mcu", "")).startswith(mcu_prefix)
    }


def clean(index_dir: Path, keep: set[str]) -> dict[str, int]:
    kept = {}
    for name in INDEX_FILES:
        path = index_dir / name
        entries = json.loads(path.read_text()) if path.exists() else []
        remaining = [e for e in entries if board_of(e) in keep]
        path.write_text(json.dumps(remaining, indent=2))
        kept[name] = len(remaining)
    return kept


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index_dir", default="zigbee2mqtt/ota")
    parser.add_argument("--db_file", default="device_db.yaml")
    parser.add_argument(
        "--keep-mcu",
        default=None,
        help="Keep the entries of boards whose mcu starts with this, e.g. EFR32",
    )
    args = parser.parse_args()

    keep: set[str] = set()
    if args.keep_mcu:
        db = yaml.safe_load(Path(args.db_file).read_text())
        keep = boards_on_mcu(db, args.keep_mcu)

    kept = clean(Path(args.index_dir), keep)
    for name, count in kept.items():
        print(f"{name}: kept {count} entries")


if __name__ == "__main__":
    main()

"""A release build that cannot rebuild the Silabs boards must keep their OTA
entries.

The release build empties the OTA indexes and lets every board build add its
entries back. When the Silicon Labs tools cannot be downloaded, the Silabs
boards are skipped - and without --keep-mcu their entries would vanish, and
every Silabs device would stop being offered an update.
"""

import json
import pathlib
import shutil
import subprocess
import sys

import pytest
import yaml

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "helper_scripts"))

import clean_z2m_index  # noqa: E402

DB = yaml.safe_load((ROOT / "device_db.yaml").read_text())
SILABS = clean_z2m_index.boards_on_mcu(DB, "EFR32")


@pytest.fixture
def index_dir(tmp_path) -> pathlib.Path:
    d = tmp_path / "ota"
    shutil.copytree(ROOT / "zigbee2mqtt" / "ota", d)
    return d


def _entries(d: pathlib.Path, name: str) -> list:
    return json.loads((d / name).read_text())


def _run(index_dir: pathlib.Path, *extra: str) -> None:
    subprocess.run(
        [sys.executable, str(ROOT / "helper_scripts" / "clean_z2m_index.py"),
         "--index_dir", str(index_dir), "--db_file", str(ROOT / "device_db.yaml"),
         *extra],
        check=True, capture_output=True,
    )


def test_there_are_silabs_boards_to_keep():
    assert SILABS, "no EFR32 boards in device_db.yaml - the test proves nothing"


def test_without_keep_everything_is_cleared(index_dir):
    _run(index_dir)
    for name in clean_z2m_index.INDEX_FILES:
        assert _entries(index_dir, name) == []


def test_keep_mcu_keeps_exactly_the_silabs_entries(index_dir):
    before = {n: _entries(index_dir, n) for n in clean_z2m_index.INDEX_FILES}
    _run(index_dir, "--keep-mcu", "EFR32")

    for name in clean_z2m_index.INDEX_FILES:
        expected = [e for e in before[name] if clean_z2m_index.board_of(e) in SILABS]
        assert _entries(index_dir, name) == expected

    # The published index files do carry Silabs entries, so something was kept.
    assert _entries(index_dir, "index_router.json")


def test_end_device_directories_map_to_their_board():
    entry = {"url": "https://x/raw/abc/bin/end_device/FOO_BAR_END_DEVICE/f.zigbee"}
    assert clean_z2m_index.board_of(entry) == "FOO_BAR"
    entry = {"url": "https://x/raw/abc/bin/router/FOO_BAR/f.zigbee"}
    assert clean_z2m_index.board_of(entry) == "FOO_BAR"

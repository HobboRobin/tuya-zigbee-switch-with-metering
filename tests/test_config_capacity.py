"""A config string must never be able to outgrow the firmware's static tables.

The parser holds peripherals in fixed-size arrays (4 switches, 6 relays, 3
cover switches, 3 covers, 12 endpoints). It used to index past the end of them
without checking, which corrupted the endpoint and attribute tables for good:
the device stayed on the network and still answered ZCL, but every read came
back UNSUPPORTED_ATTRIBUTE - genBasic's device_config included, so the config
that caused it could not even be written back. The only way out was re-flashing
by wire.

An over-capacity config must therefore take the same route as an over-long
manufacturer name: reset_all(), which clears NVM and reboots into the
compiled-in default. In the stub that surfaces as a clean exit.

Every peripheral gets its own pin here - sharing one would trip the stub's
input/output conflict check instead, and the test would pass for the wrong
reason.
"""

import pytest

from client import StubProc
from conftest import Device
from zcl_consts import ZCL_ATTR_BASIC_MFR_NAME, ZCL_CLUSTER_BASIC

OVER_CAPACITY = [
    pytest.param("X;Y;SA0u;SA1u;SA2u;SA3u;SA4u;", id="5-switches-max-4"),
    pytest.param("X;Y;RB0;RB1;RB2;RB3;RB4;RB5;RB6;", id="7-relays-max-6"),
    pytest.param(
        "X;Y;XA0A1u;XA2A3u;XA4A5u;XA6A7u;", id="4-cover-switches-max-3"
    ),
    pytest.param("X;Y;CB0B1;CB2B3;CB4B5;CB6B7;", id="4-covers-max-3"),
    # Each table on its own is fine here; only the endpoint total overflows
    # (4 switches + 4 relays + 2 covers + 4 long-press endpoints = 14).
    pytest.param(
        "X;Y;SA0u;SA1u;SA2u;SA3u;RB0;RB1;RB2;RB3;CC0C1;CC2C3;2EP;",
        id="14-endpoints-max-12",
    ),
]

# The largest layouts that must still come up.
AT_CAPACITY = [
    pytest.param("X;Y;SA0u;SA1u;SA2u;SA3u;", id="4-switches"),
    pytest.param("X;Y;RB0;RB1;RB2;RB3;RB4;RB5;", id="6-relays"),
    pytest.param("X;Y;XA0A1u;XA2A3u;XA4A5u;", id="3-cover-switches"),
    pytest.param("X;Y;CB0B1;CB2B3;CB4B5;", id="3-covers"),
    pytest.param(
        "X;Y;SA0u;SA1u;SA2u;SA3u;RB0;RB1;RB2;RB3;2EP;", id="12-endpoints"
    ),
]


@pytest.mark.parametrize("cfg", OVER_CAPACITY)
def test_over_capacity_config_resets_instead_of_overflowing(cfg: str):
    p = StubProc(device_config=cfg)
    try:
        # start() also handshakes with the stub, which fails once it has
        # exited - that is the expected path here, so the handshake result is
        # not what we assert on.
        try:
            p.start()
        except Exception:
            pass
        # reset_all() ends in hal_system_reset(), which the stub implements as
        # exit(0). Staying up and serving requests would mean the parser ran
        # past the end of its tables instead.
        assert p.wait_for_exit(timeout=5.0), (
            "device kept running with an over-capacity config, so it parsed "
            "past the end of its tables instead of resetting"
        )
    finally:
        p.stop()


@pytest.mark.parametrize("cfg", AT_CAPACITY)
def test_config_at_capacity_still_boots(cfg: str):
    """The guards must not be off by one and reject a legitimate maximum."""
    p = StubProc(device_config=cfg).start()
    try:
        assert p.is_running()
        d = Device(p)
        assert d.read_zigbee_attr(1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_MFR_NAME)
    finally:
        p.stop()

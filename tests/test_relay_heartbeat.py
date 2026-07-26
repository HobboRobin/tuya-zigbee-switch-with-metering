"""Firmware-side relay-state heartbeat: app_task re-pushes every relay's onOff
state to the coordinator every 5 minutes, so a lost on-change report self-heals
(the Telink stack emits no periodic max-interval report for a boolean attribute,
so the Z2M reporting config alone cannot recover a lost onOff report)."""

import pytest

from client import StubProc
from conftest import Device, wait_for

ZCL_CLUSTER_ON_OFF = 0x0006
ZCL_ATTR_ONOFF = 0x0000
ZCL_CMD_ONOFF_ON = 0x01
RELAY_EP = 2  # SA0u -> switch EP1, RB0 -> relay EP2
HEARTBEAT_MS = 5 * 60 * 1000


@pytest.fixture
def device_config() -> str:
    return "Stub;Stub;SA0u;RB0;M;"


def _relay_reports(dev: Device) -> list:
    return [
        e
        for e in dev._events
        if e.kind == "zcl_report"
        and int(e.payload["ep"]) == RELAY_EP
        and int(e.payload["cluster"], 16) == ZCL_CLUSTER_ON_OFF
        and int(e.payload["attr"], 16) == ZCL_ATTR_ONOFF
    ]


def test_relay_state_reported_on_heartbeat_interval(device: Device):
    # step_time only advances hal_millis() while time is frozen.
    device.freeze_time()
    device.set_network(1)
    # Turn the relay on, then drop any reports emitted so far.
    device.call_zigbee_cmd(RELAY_EP, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_ON)
    device.clear_events()

    # Before the interval elapses: no heartbeat yet.
    device.step_time(HEARTBEAT_MS // 2)
    assert _relay_reports(device) == []

    # After the full interval: the relay state is pushed proactively. The report
    # event arrives asynchronously, so poll for it rather than checking once.
    device.step_time(HEARTBEAT_MS)
    wait_for(lambda: len(_relay_reports(device)) >= 1, timeout=2.0)


def test_no_heartbeat_while_not_joined(device: Device):
    # Not joined: the heartbeat must not fire (nothing to report to).
    device.freeze_time()
    device.set_network(0)
    device.clear_events()
    device.step_time(HEARTBEAT_MS * 2)
    assert _relay_reports(device) == []

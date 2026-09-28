"""A relay change is confirmed with an acknowledged report to the coordinator.

The stack reports a relay change once, unacknowledged, and treats the value as
reported as soon as the frame leaves. When that one frame is lost, the
coordinator keeps the old state while the power readings from the same device
- which change every few seconds - keep arriving. Observed on a Nous A1Z: load
on the plug, Z2M showing it off.

After every change the firmware sends the relay state once more, straight to
the coordinator and with an APS acknowledgement, so the stack retries it until
it is delivered.
"""

import pytest

from conftest import Device, wait_for

ZCL_CLUSTER_ON_OFF = 0x0006
ZCL_ATTR_ONOFF = 0x0000
ZCL_CMD_ONOFF_ON = 0x01
ZCL_CMD_ONOFF_OFF = 0x00
ZCL_CMD_ONOFF_TOGGLE = 0x02
RELAY_EP = 2  # SA0u -> switch EP1, RB0 -> relay EP2
CONFIRM_DELAY_MS = 1500  # RELAY_CONFIRM_DELAY_MS in relay_cluster.c


@pytest.fixture
def device_config() -> str:
    return "Stub;Stub;SA0u;RB0;M;"


def _confirmed_reports(dev: Device) -> list:
    return [
        e
        for e in dev._events
        if e.kind == "zcl_report"
        and e.payload.get("dst") == "coordinator"
        and int(e.payload["ep"]) == RELAY_EP
        and int(e.payload["cluster"], 16) == ZCL_CLUSTER_ON_OFF
        and int(e.payload["attr"], 16) == ZCL_ATTR_ONOFF
    ]


def _joined(device: Device) -> None:
    device.freeze_time()
    device.set_network(1)
    # Anything the boot sequence scheduled has run by now.
    device.step_time(CONFIRM_DELAY_MS * 2)
    device.clear_events()


def test_relay_change_is_confirmed_to_the_coordinator(device: Device):
    _joined(device)

    device.call_zigbee_cmd(RELAY_EP, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_ON)
    device.step_time(CONFIRM_DELAY_MS)

    wait_for(lambda: len(_confirmed_reports(device)) == 1, timeout=2.0)


def test_confirmation_waits_for_the_delay(device: Device):
    _joined(device)

    device.call_zigbee_cmd(RELAY_EP, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_ON)
    device.step_time(CONFIRM_DELAY_MS - 500)
    assert _confirmed_reports(device) == []

    device.step_time(500)
    wait_for(lambda: len(_confirmed_reports(device)) == 1, timeout=2.0)


def test_burst_of_changes_is_confirmed_once(device: Device):
    _joined(device)

    # Three changes inside the delay: each restarts it, so one report goes out,
    # carrying the state the relay ended in.
    for cmd in (ZCL_CMD_ONOFF_ON, ZCL_CMD_ONOFF_OFF, ZCL_CMD_ONOFF_TOGGLE):
        device.call_zigbee_cmd(RELAY_EP, ZCL_CLUSTER_ON_OFF, cmd)
        device.step_time(CONFIRM_DELAY_MS // 3)

    device.step_time(CONFIRM_DELAY_MS)
    wait_for(lambda: len(_confirmed_reports(device)) >= 1, timeout=2.0)
    device.step_time(CONFIRM_DELAY_MS * 2)
    assert len(_confirmed_reports(device)) == 1


def test_button_press_is_confirmed_too(device: Device):
    # A press on the device itself is the case where nothing on the Z2M side
    # knows the state changed - the report is the only news it gets.
    _joined(device)

    device.click_button("A0")  # SA0u: momentary, toggles the relay
    device.step_time(CONFIRM_DELAY_MS * 2)

    wait_for(lambda: len(_confirmed_reports(device)) == 1, timeout=2.0)


def test_nothing_is_sent_while_not_joined(device: Device):
    device.freeze_time()
    device.set_network(0)
    device.clear_events()

    device.call_zigbee_cmd(RELAY_EP, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_ON)
    device.step_time(CONFIRM_DELAY_MS * 2)

    assert _confirmed_reports(device) == []

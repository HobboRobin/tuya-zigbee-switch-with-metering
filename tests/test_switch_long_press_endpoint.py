"""Long-press companion binding endpoint (2EP token): a long press toggles a
second, separate endpoint's bindings, so a short press and a long press can
drive two different bound targets. The short press must not touch it."""

from client import StubProc
from conftest import Device

ZCL_CLUSTER_ON_OFF = 0x0006
ZCL_CMD_ONOFF_TOGGLE = 0x02

# 1-gang momentary switch: button B1, relay D3, plus the long-press endpoint.
CONFIG_2EP = "Stub;Stub;SB1u;RD3;M;2EP;"
CONFIG_PLAIN = "Stub;Stub;SB1u;RD3;M;"
BUTTON = "B1"


def test_long_press_toggles_companion_endpoint():
    with StubProc(device_config=CONFIG_2EP) as proc:
        d = Device(proc)
        d.clear_events()
        d.long_click_button(BUTTON, duration_ms=1200)
        # OnOff toggle emitted from the companion long-press endpoint (EP3),
        # which owns its own bindings independent of the switch endpoint.
        evt = d.wait_for_cmd_send(
            ep=3, cluster=ZCL_CLUSTER_ON_OFF, cmd=ZCL_CMD_ONOFF_TOGGLE
        )
        assert evt.ep == 3


def test_short_press_does_not_touch_companion_endpoint():
    with StubProc(device_config=CONFIG_2EP) as proc:
        d = Device(proc)
        d.clear_events()
        d.click_button(BUTTON)
        d.step_time(200)
        for e in d._events:
            if e.kind == "zcl_cmd_send":
                assert int(e.payload["ep"]) != 3, "short press hit the long-press EP"


def test_no_companion_send_without_2ep():
    with StubProc(device_config=CONFIG_PLAIN) as proc:
        d = Device(proc)
        d.clear_events()
        d.long_click_button(BUTTON, duration_ms=1200)
        d.step_time(200)
        # No long-press endpoint exists, so nothing is ever sent from EP3.
        for e in d._events:
            if e.kind == "zcl_cmd_send":
                assert int(e.payload["ep"]) != 3

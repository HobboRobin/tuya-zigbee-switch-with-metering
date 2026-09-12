"""A reed on a pin should be a door sensor, not a button that presses itself.

Without IAS Zone the firmware can only say "switch 1 changed position", and a
coordinator has no way to know the thing on the other end is a door: it renders
a switch action and every automation has to translate. The zone says what the
sensor *is* once, and then every coordinator shows the same contact.

The zone rides along on the switch's endpoint rather than replacing it, so the
action, the bindings and the relay targeting all keep working - the `Z` flag
adds a reading of the same input, it does not take one away.
"""

import pytest

from client import StubProc
from conftest import Device
from zcl_consts import (
    ZCL_ATTR_IAS_ZONE_CIE_ADDRESS,
    ZCL_ATTR_IAS_ZONE_ID,
    ZCL_ATTR_IAS_ZONE_STATE,
    ZCL_ATTR_IAS_ZONE_STATUS,
    ZCL_ATTR_IAS_ZONE_TYPE,
    ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE,
    ZCL_CLUSTER_IAS_ZONE,
    ZCL_CLUSTER_MULTISTATE_INPUT_BASIC,
)

# A reed on A0 declared as a contact, plus a plain button on A1.
CONFIG = "mfg;X;SA0uZC;SA1u;"
REED, BUTTON = "A0", "A1"

ZONE_TYPE_CONTACT = 0x0015
ZONE_STATE_NOT_ENROLLED = 0
ZONE_STATE_ENROLLED = 1
ZONE_ID_INVALID = 0xFF
ENROLL_REQUEST = 0x01
ENROLL_RESPONSE = 0x00
STATUS_CHANGE_NOTIFICATION = 0x00
COORDINATOR_IEEE = "0011223344556677"


@pytest.fixture
def device():
    p = StubProc(device_config=CONFIG).start()
    try:
        yield Device(p)
    finally:
        p.stop()


def attr(device: Device, attribute: int, endpoint: int = 1) -> int:
    """Enums and bitmaps read back as hex, plain integers as decimal."""
    value = device.read_zigbee_attr(endpoint, ZCL_CLUSTER_IAS_ZONE, attribute)
    if attribute in (ZCL_ATTR_IAS_ZONE_TYPE, ZCL_ATTR_IAS_ZONE_STATUS,
                     ZCL_ATTR_IAS_ZONE_STATE):
        return int(value, 16)
    return int(value)


def enroll(device: Device) -> None:
    device.write_zigbee_attr(
        1, ZCL_CLUSTER_IAS_ZONE, ZCL_ATTR_IAS_ZONE_CIE_ADDRESS, COORDINATOR_IEEE
    )
    device.step_time(500)  # the enroll request is sent off the write, not in it
    device.wait_for_cmd_send(1, ZCL_CLUSTER_IAS_ZONE, ENROLL_REQUEST)
    device.call_zigbee_cmd(
        1, ZCL_CLUSTER_IAS_ZONE, ENROLL_RESPONSE, bytes([0x00, 0x2A])
    )


def test_the_zone_says_what_kind_of_sensor_this_is(device: Device):
    assert attr(device, ZCL_ATTR_IAS_ZONE_TYPE) == ZONE_TYPE_CONTACT


def test_a_switch_without_the_flag_has_no_zone(device: Device):
    res = device.p.exec(
        f"zcl_read 2 0x{ZCL_CLUSTER_IAS_ZONE:04X} 0x{ZCL_ATTR_IAS_ZONE_TYPE:04X}"
    )
    assert not res.ok


def test_it_starts_unenrolled(device: Device):
    assert attr(device, ZCL_ATTR_IAS_ZONE_STATE) == ZONE_STATE_NOT_ENROLLED
    assert attr(device, ZCL_ATTR_IAS_ZONE_ID) == ZONE_ID_INVALID


def test_writing_the_cie_address_asks_to_be_enrolled(device: Device):
    device.clear_events()
    device.write_zigbee_attr(
        1, ZCL_CLUSTER_IAS_ZONE, ZCL_ATTR_IAS_ZONE_CIE_ADDRESS, COORDINATOR_IEEE
    )
    device.step_time(500)

    sent = device.wait_for_cmd_send(1, ZCL_CLUSTER_IAS_ZONE, ENROLL_REQUEST)
    # zoneType little-endian, then a zero manufacturer code.
    assert sent.data == bytes([0x15, 0x00, 0, 0])


def test_the_zone_talks_to_the_coordinator_not_to_bindings(device: Device):
    """Nothing is bound while the interview is running, so a zone that sends
    through the binding table sends into nowhere exactly when it matters."""
    device.clear_events()
    device.write_zigbee_attr(
        1, ZCL_CLUSTER_IAS_ZONE, ZCL_ATTR_IAS_ZONE_CIE_ADDRESS, COORDINATOR_IEEE
    )
    device.step_time(500)
    device.wait_for_cmd_send(1, ZCL_CLUSTER_IAS_ZONE, ENROLL_REQUEST)
    device.press_button(REED)
    device.wait_for_cmd_send(1, ZCL_CLUSTER_IAS_ZONE, STATUS_CHANGE_NOTIFICATION)

    for event in device._events:
        if event.kind != "zcl_cmd_send":
            continue
        if int(event.payload["cluster"], 16) != ZCL_CLUSTER_IAS_ZONE:
            continue
        assert event.payload.get("dst") == "coordinator", event.payload


def test_the_cie_write_is_itself_the_enrolment(device: Device):
    """Z2M sends the enroll response unprompted and then reads zoneState back,
    failing the whole interview if it is still zero. A device that waits to be
    asked first is a device that never finishes joining."""
    device.write_zigbee_attr(
        1, ZCL_CLUSTER_IAS_ZONE, ZCL_ATTR_IAS_ZONE_CIE_ADDRESS, COORDINATOR_IEEE
    )

    assert attr(device, ZCL_ATTR_IAS_ZONE_STATE) == ZONE_STATE_ENROLLED


def test_the_enroll_response_is_taken(device: Device):
    enroll(device)

    assert attr(device, ZCL_ATTR_IAS_ZONE_STATE) == ZONE_STATE_ENROLLED
    assert attr(device, ZCL_ATTR_IAS_ZONE_ID) == 0x2A


def test_a_refused_enrolment_leaves_it_unenrolled(device: Device):
    device.write_zigbee_attr(
        1, ZCL_CLUSTER_IAS_ZONE, ZCL_ATTR_IAS_ZONE_CIE_ADDRESS, COORDINATOR_IEEE
    )
    device.step_time(500)
    device.call_zigbee_cmd(
        1, ZCL_CLUSTER_IAS_ZONE, ENROLL_RESPONSE, bytes([0x03, 0x2A])  # too many zones
    )

    assert attr(device, ZCL_ATTR_IAS_ZONE_STATE) == ZONE_STATE_NOT_ENROLLED
    assert attr(device, ZCL_ATTR_IAS_ZONE_ID) == ZONE_ID_INVALID


def test_the_contact_moves_the_alarm_bit(device: Device):
    device.press_button(REED)
    assert attr(device, ZCL_ATTR_IAS_ZONE_STATUS) & 1

    device.release_button(REED)
    assert not attr(device, ZCL_ATTR_IAS_ZONE_STATUS) & 1


def test_a_change_is_announced_rather_than_waited_for(device: Device):
    """A sensor that only answers reads is useless: nobody is polling a door."""
    enroll(device)
    device.clear_events()

    device.press_button(REED)

    sent = device.wait_for_cmd_send(
        1, ZCL_CLUSTER_IAS_ZONE, STATUS_CHANGE_NOTIFICATION
    )
    # zoneStatus, extended status, zone id, delay
    assert sent.data[0] & 1
    assert sent.data[3] == 0x2A


def test_the_other_switch_does_not_move_the_zone(device: Device):
    device.press_button(BUTTON)
    assert not attr(device, ZCL_ATTR_IAS_ZONE_STATUS) & 1


def test_the_switch_action_still_works(device: Device):
    """The zone is additional. Losing the action would be a poor trade."""
    before = device.read_zigbee_attr(
        1, ZCL_CLUSTER_MULTISTATE_INPUT_BASIC, ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE
    )
    device.press_button(REED)
    after = device.read_zigbee_attr(
        1, ZCL_CLUSTER_MULTISTATE_INPUT_BASIC, ZCL_ATTR_MULTISTATE_INPUT_PRESENT_VALUE
    )
    assert before != after

"""Every switchable output must be able to join a Zigbee group.

The coordinator adds a device to a group by sending `genGroups.add` to the
endpoint it wants in the group. An endpoint without the Groups cluster answers
UNSUP_COMMAND, and Z2M reports the group add as failed - which is what happened
to lights and covers, because the cluster was only ever attached to relay
endpoints.

Switch endpoints deliberately stay out of it: a switch is a client, so group
*membership* means nothing there. Pointing a button at a group is a binding,
not a membership.
"""

import pytest

from client import StubProc
from conftest import Device
from zcl_consts import ZCL_ATTR_GROUP_NAME_SUPPORT, ZCL_CLUSTER_GROUPS


def groupable(device: Device, endpoint: int) -> bool:
    res = device.p.exec(
        f"zcl_read {endpoint} 0x{ZCL_CLUSTER_GROUPS:04X}"
        f" 0x{ZCL_ATTR_GROUP_NAME_SUPPORT:04X}"
    )
    return res.ok


def test_lights_can_join_a_group():
    """Two switches on ep1/ep2, two lights on ep3/ep4."""
    with StubProc(device_config="gled;X;BC0u;SB7u;SB1u;TC4C3;TC2D2;") as p:
        d = Device(p)
        assert groupable(d, 3)
        assert groupable(d, 4)


def test_relays_still_can():
    with StubProc(device_config="X;Y;SA0u;RB0;RB1;M;") as p:
        d = Device(p)
        assert groupable(d, 2)
        assert groupable(d, 3)


def test_covers_can_join_a_group():
    with StubProc(device_config="mfg;Y;BC5u;LA3;XA2B3u;CC4D2;") as p:
        d = Device(p)
        # ep1 cover switch, ep2 cover.
        assert groupable(d, 2)


def test_a_switch_endpoint_is_not_a_group_member():
    with StubProc(device_config="gled;X;BC0u;SB7u;SB1u;TC4C3;TC2D2;") as p:
        d = Device(p)
        assert not groupable(d, 1)
        assert not groupable(d, 2)

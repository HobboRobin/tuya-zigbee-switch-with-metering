import pytest

from client import StubProc
from conftest import Device
from tests.zcl_consts import (
  ZCL_CLUSTER_ON_OFF,
  ZCL_CMD_ONOFF_TOGGLE
)

@pytest.fixture
def encoder_device() -> Device:
    p = StubProc(device_config="X;Y;EA0uA1uA2u;").start() 
    # EA0uA1uA2u;
    d = Device(p)
    yield d  

def test_encoder_cluster_switch_pressing_causes_on_of_command(encoder_device: Device) -> None:
  
    # Test setup
    encoder_device.step_time(100)
    result = encoder_device.set_gpio("A2", 0) # Press
    encoder_device.step_time(100)
    result = encoder_device.set_gpio("A2", 1) # Release
    encoder_device.step_time(100)

    # Assertions
    encoder_device.wait_for_cmd_send(
        1, ZCL_CLUSTER_ON_OFF, ZCL_CMD_ONOFF_TOGGLE
    )
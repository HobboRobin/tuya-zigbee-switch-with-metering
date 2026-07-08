import pytest

from client import StubProc
from conftest import Device

ZCL_CLUSTER_BASIC = 0x0000
ZCL_CLUSTER_ON_OFF = 0x0006
ZCL_CLUSTER_ELECTRICAL_MEASUREMENT = 0x0B04

ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE = 0x0505
ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_VOLTAGE = 0xFF10
ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_POWER = 0xFF12
ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATION_VALUES = 0xFF20

ZCL_ATTR_BASIC_STATUS_LED_BRIGHTNESS = 0xFF05
ZCL_ATTR_BASIC_STATUS_LED_TRANSITION = 0xFF06

ZCL_ATTR_ONOFF_INDICATOR_BRIGHTNESS = 0xFF03


@pytest.fixture
def device_config() -> str:
    # BL0942 metering (EB token) + dimmable network LED + dimmable indicator.
    return "StubManufacturer;StubDevice;LA0ip;SB5u;RC2;IB4ip;EBB0B7;M;"


def test_bl0942_registers_electrical_measurement(device: Device):
    """The EB token brings up haElectricalMeasurement incl. calibrate attrs."""
    assert (
        device.read_zigbee_attr(
            1, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT, ZCL_ATTR_ELEC_MEAS_RMS_VOLTAGE
        )
        is not None
    )
    assert (
        device.read_zigbee_attr(
            1,
            ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
            ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_VOLTAGE,
        )
        is not None
    )
    assert (
        device.read_zigbee_attr(
            1,
            ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
            ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATE_POWER,
        )
        is not None
    )


def test_calibration_values_read_write_persist(device: Device):
    """0xFF20 mirrors the multipliers; writes apply, canonicalise and persist."""
    # BL0942 compile-time defaults: V=413, A=261, W=105.
    assert (
        device.read_zigbee_attr(
            1,
            ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
            ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATION_VALUES,
        )
        == "V413A261W105"
    )

    # Partial write: V and W change, missing A keeps its value; a read
    # afterwards returns the canonical full string.
    device.write_zigbee_attr(
        1,
        ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
        ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATION_VALUES,
        "V500W700",
    )
    assert (
        device.read_zigbee_attr(
            1,
            ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
            ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATION_VALUES,
        )
        == "V500A261W700"
    )

    # The imported multipliers survive a reboot (persisted like a normal
    # on-device calibration).
    with StubProc(
        device_config="StubManufacturer;StubDevice;LA0ip;SB5u;RC2;IB4ip;EBB0B7;M;"
    ) as proc:
        rebooted = Device(proc)
        assert (
            rebooted.read_zigbee_attr(
                1,
                ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
                ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATION_VALUES,
            )
            == "V500A261W700"
        )


def test_network_led_dimming_attrs_present_and_persisted(device: Device):
    """LA0ip exposes genBasic brightness/transition; values survive reboot."""
    assert (
        device.read_zigbee_attr(
            1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_STATUS_LED_BRIGHTNESS
        )
        == "255"
    )
    device.write_zigbee_attr(
        1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_STATUS_LED_BRIGHTNESS, "120"
    )
    device.write_zigbee_attr(
        1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_STATUS_LED_TRANSITION, "1500"
    )

    with StubProc(
        device_config="StubManufacturer;StubDevice;LA0ip;SB5u;RC2;IB4ip;EBB0B7;M;"
    ) as proc:
        rebooted = Device(proc)
        assert (
            rebooted.read_zigbee_attr(
                1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_STATUS_LED_BRIGHTNESS
            )
            == "120"
        )
        assert (
            rebooted.read_zigbee_attr(
                1, ZCL_CLUSTER_BASIC, ZCL_ATTR_BASIC_STATUS_LED_TRANSITION
            )
            == "1500"
        )


def test_network_led_dimming_absent_without_p_flag():
    """A plain LA0i network LED must not register the dimming attributes."""
    with StubProc(device_config="StubManufacturer;StubDevice;LA0i;SB5u;RC2;M;") as proc:
        device = Device(proc)
        res = device.p.exec(
            f"zcl_read 1 0x{ZCL_CLUSTER_BASIC:04X} "
            f"0x{ZCL_ATTR_BASIC_STATUS_LED_BRIGHTNESS:04X}"
        )
        assert not res.ok
        assert "attr_not_found" in res.payload


def test_indicator_dimming_still_present_alongside_network_led(device: Device):
    """Both dimmable LEDs coexist: indicator attrs live on the relay endpoint."""
    # Endpoint 2 is the relay endpoint for this config (1 switch + 1 relay).
    assert (
        device.read_zigbee_attr(
            2, ZCL_CLUSTER_ON_OFF, ZCL_ATTR_ONOFF_INDICATOR_BRIGHTNESS
        )
        is not None
    )


def test_config_str_calibration_markers_seed_multipliers():
    """V/A/W markers baked into the EB token (factory calibration templates)
    must land in the active multipliers, readable via calibration_values."""
    with StubProc(
        device_config="StubManufacturer;StubDevice;SB5u;RC2;IB4ip;EBB0B7V412A256W103;M;"
    ) as proc:
        device = Device(proc)
        assert (
            device.read_zigbee_attr(
                1,
                ZCL_CLUSTER_ELECTRICAL_MEASUREMENT,
                ZCL_ATTR_ELEC_MEAS_CUST_CALIBRATION_VALUES,
            )
            == "V412A256W103"
        )

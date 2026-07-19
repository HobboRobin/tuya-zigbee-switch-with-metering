"""Overload protection: deterministic state-machine tests via the `overload_sim`
stub command, plus integration tests through the real HLW8012 + relay wiring."""

from client import StubProc
from conftest import Device

ZCL_CLUSTER_ELECTRICAL_MEASUREMENT = 0x0B04
ATTR_POWER_LIMIT = 0xFF30
ATTR_CURRENT_LIMIT = 0xFF31
ATTR_TRIP_DELAY = 0xFF32
ATTR_OVERVOLTAGE = 0xFF33
ATTR_UNDERVOLTAGE = 0xFF34
ATTR_RECONNECT_DELAY = 0xFF35
ATTR_ALARM = 0xFF36

# overload_alarm_t
NONE, POWER, CURRENT, PEAK, V_HIGH, V_LOW, LOCKED = 0, 1, 2, 3, 4, 5, 6
# overload_action_t
ACT_NONE, ACT_OFF, ACT_ON = 0, 1, 2
# StartUpOnOff
SU_OFF, SU_ON, SU_PREV = 0x00, 0x01, 0xFF

# HLW metering + relay on EP1: relay D2, CF=A1, CF1=C2, SEL=B1.
CONFIG = "Stub;Stub;RD2;EPA1C2B1;M;"
CF_PIN = "A1"
RELAY_PIN = "D2"


def _sim(dev: Device, *args) -> dict:
    """Run one overload_sim step and return the parsed key=value result."""
    res = dev.p.exec("overload_sim " + " ".join(str(a) for a in args))
    assert res.ok, res.payload
    return {k: int(v) for k, v in res.payload.items() if v.lstrip("-").isdigit()}


def test_sim_peak_trips_immediately():
    with StubProc(device_config=CONFIG) as proc:
        d = Device(proc)
        _sim(d, "reset")
        # Below hard peak (3680 W), within soft limits -> nothing.
        r = _sim(d, 1000, 23000, 500, 100, 1, SU_ON)
        assert r["action"] == ACT_NONE and r["alarm"] == NONE
        # At/above the hard peak -> immediate trip, no grace.
        r = _sim(d, 1100, 23000, 5000, 3700, 1, SU_ON)
        assert r["action"] == ACT_OFF and r["alarm"] == PEAK and r["relay"] == 0


def test_sim_soft_trips_only_after_delay():
    with StubProc(device_config=CONFIG) as proc:
        d = Device(proc)
        _sim(d, "reset")
        # Default soft power limit 2500 W, trip delay 30 s. 3000 W is over soft
        # but under peak: warns immediately, must not trip before 30 s.
        r = _sim(d, 1000, 23000, 100, 3000, 1, SU_OFF)
        assert r["action"] == ACT_NONE and r["alarm"] == POWER
        r = _sim(d, 30000, 23000, 100, 3000, 1, SU_OFF)
        assert r["action"] == ACT_NONE and r["alarm"] == POWER
        # Past the grace period (30 s since 1000 ms) -> trip. Power-on OFF =>
        # no auto-reconnect.
        r = _sim(d, 31000, 23000, 100, 3000, 1, SU_OFF)
        assert r["action"] == ACT_OFF and r["alarm"] == POWER and r["relay"] == 0
        # Stays off (relay reported off, no reconnect scheduled).
        r = _sim(d, 200000, 23000, 100, 3000, 0, SU_OFF)
        assert r["action"] == ACT_NONE and r["relay"] == 0


def test_sim_load_drops_before_delay_resets_timer():
    with StubProc(device_config=CONFIG) as proc:
        d = Device(proc)
        _sim(d, "reset")
        _sim(d, 0, 23000, 100, 3000, 1, SU_OFF)  # over soft
        r = _sim(d, 10000, 23000, 100, 500, 1, SU_OFF)  # back to normal
        assert r["alarm"] == NONE
        # Re-exceeding starts a fresh grace period, so 20 s later still no trip.
        _sim(d, 20000, 23000, 100, 3000, 1, SU_OFF)
        r = _sim(d, 40000, 23000, 100, 3000, 1, SU_OFF)
        assert r["action"] == ACT_NONE and r["alarm"] == POWER


def test_sim_auto_reconnect_then_lockout_after_5_retries():
    with StubProc(device_config=CONFIG) as proc:
        d = Device(proc)
        _sim(d, "reset")
        now = 0
        # Peak trip with power-on ON -> auto-reconnect armed (60 s default).
        r = _sim(d, now, 23000, 100, 4000, 1, SU_ON)
        assert r["action"] == ACT_OFF and r["tripped"] == 1

        for expected_retry in range(1, 6):
            # Before the reconnect delay: still off.
            r = _sim(d, now + 59000, 23000, 100, 4000, 0, SU_ON)
            assert r["action"] == ACT_NONE and r["relay"] == 0
            # After the delay: auto-reconnect.
            now += 60000
            r = _sim(d, now, 23000, 100, 4000, 0, SU_ON)
            assert r["action"] == ACT_ON and r["retries"] == expected_retry
            # Load still over -> trips again immediately (same instant, relay on
            # now), arming the next reconnect exactly reconnect_delay later.
            r = _sim(d, now, 23000, 100, 4000, 1, SU_ON)
            assert r["action"] == ACT_OFF

        # After the 5th reconnect re-trips, it latches out.
        assert r["locked"] == 1 and r["alarm"] == LOCKED
        # No further reconnect even after a long wait.
        now += 200000
        r = _sim(d, now, 23000, 100, 4000, 0, SU_ON)
        assert r["action"] == ACT_NONE and r["alarm"] == LOCKED


def test_sim_manual_on_clears_lockout():
    with StubProc(device_config=CONFIG) as proc:
        d = Device(proc)
        _sim(d, "reset")
        # Force a trip.
        _sim(d, 0, 23000, 100, 4000, 1, SU_OFF)
        # User switches it back on manually with the load now gone.
        r = _sim(d, 5000, 23000, 100, 200, 1, SU_OFF)
        assert r["alarm"] == NONE and r["tripped"] == 0 and r["locked"] == 0


def test_sim_voltage_is_warn_only():
    with StubProc(device_config=CONFIG) as proc:
        d = Device(proc)
        _sim(d, "reset")
        # Overvoltage (>260 V) warns but never trips.
        r = _sim(d, 0, 27000, 100, 200, 1, SU_OFF)
        assert r["action"] == ACT_NONE and r["alarm"] == V_HIGH and r["relay"] == 1
        # Undervoltage (<210 V, above the 50 V floor) warns, no trip.
        r = _sim(d, 1000, 20000, 100, 200, 1, SU_OFF)
        assert r["action"] == ACT_NONE and r["alarm"] == V_LOW and r["relay"] == 1
        # A 0 V reading (unplugged / meter not up) must not warn.
        r = _sim(d, 2000, 0, 0, 0, 1, SU_OFF)
        assert r["alarm"] == NONE


def test_config_defaults_write_and_persist():
    with StubProc(device_config=CONFIG) as proc:
        d = Device(proc)
        read = lambda a: d.read_zigbee_attr(1, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT, a)
        assert read(ATTR_POWER_LIMIT) == "2500"
        assert read(ATTR_CURRENT_LIMIT) == "10000"
        assert read(ATTR_TRIP_DELAY) == "30"
        assert read(ATTR_OVERVOLTAGE) == "26000"
        assert read(ATTR_UNDERVOLTAGE) == "21000"
        assert read(ATTR_RECONNECT_DELAY) == "60"
        assert read(ATTR_ALARM) == "0"

        d.write_zigbee_attr(
            1, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT, ATTR_POWER_LIMIT, "2000"
        )
        # A too-high value is clamped to the hard peak (3680 W).
        d.write_zigbee_attr(
            1, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT, ATTR_CURRENT_LIMIT, "50000"
        )
        assert read(ATTR_POWER_LIMIT) == "2000"
        assert read(ATTR_CURRENT_LIMIT) == "16000"

    with StubProc(device_config=CONFIG) as proc:
        d = Device(proc)
        assert (
            d.read_zigbee_attr(
                1, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT, ATTR_POWER_LIMIT
            )
            == "2000"
        )


def test_integration_hlw_peak_trips_relay():
    """End-to-end: a peak load measured by the HLW8012 turns the relay off."""
    with StubProc(device_config=CONFIG) as proc:
        d = Device(proc)
        # First 5 s sample so the meter is valid; then switch the relay on.
        d.step_time(5000)
        d.step_time(20)
        d.zcl_relay_on(1)
        assert d.get_gpio(RELAY_PIN, refresh=True) is True

        # Inject a large CF pulse count (~well over the 3680 W peak). The fast
        # power estimate peeks this without waiting for the next 5 s sample.
        cf_pin_num = d._parse_pin(CF_PIN)
        d.p.exec(f"set_counter {cf_pin_num} 12000")
        d.step_time(1500)
        d.step_time(100)  # pump a poll so the protection check runs

        assert (
            d.read_zigbee_attr(
                1, ZCL_CLUSTER_ELECTRICAL_MEASUREMENT, ATTR_ALARM
            )
            == str(PEAK)
        )
        assert d.get_gpio(RELAY_PIN, refresh=True) is False

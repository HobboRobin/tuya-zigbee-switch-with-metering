"""Derived electrical quantities: apparent power (VA), reactive power (var,
magnitude) and power factor (signed %) computed in firmware from RMS voltage
(cV), RMS current (mA) and active power (W). Exercises the pure
elec_meas_derive_power() via the `elec_meas_derive` stub command."""

import math

import pytest

from client import StubProc
from conftest import Device


@pytest.fixture
def device_config() -> str:
    # Any config works; the derivation command is meter-independent.
    return "Stub;Stub;SA0u;RB0;"


def _derive(dev: Device, voltage_cv: int, current_ma: int, power_w: int) -> dict:
    res = dev.p.exec(f"elec_meas_derive {voltage_cv} {current_ma} {power_w}")
    assert res.ok, res.payload
    return {
        "va": int(res.payload["apparent_va"]),
        "var": int(res.payload["reactive_var"]),
        "pf": int(res.payload["power_factor"]),
    }


def test_apparent_power_is_voltage_times_current(device: Device):
    # 230 V * 10 A = 2300 VA
    r = _derive(device, 23000, 10000, 2000)
    assert r["va"] == 2300


def test_power_factor_is_p_over_s_percent(device: Device):
    # 2000 W / 2300 VA = 0.869... -> 86 %
    r = _derive(device, 23000, 10000, 2000)
    assert r["pf"] == 86


def test_reactive_power_is_sqrt_s2_minus_p2(device: Device):
    r = _derive(device, 23000, 10000, 2000)
    expected = int(math.isqrt(2300 * 2300 - 2000 * 2000))  # 1135
    assert abs(r["var"] - expected) <= 1


def test_purely_resistive_load_pf_100_var_0(device: Device):
    # P == S: unity power factor, no reactive power.
    r = _derive(device, 23000, 10000, 2300)
    assert r["pf"] == 100
    assert r["var"] == 0


def test_no_load_is_all_zero_no_divide_by_zero(device: Device):
    r = _derive(device, 23000, 0, 0)
    assert r == {"va": 0, "var": 0, "pf": 0}


def test_power_factor_clamped_to_100_on_noise(device: Device):
    # Calibration noise can push P slightly over S; PF must not exceed 100.
    r = _derive(device, 23000, 10000, 2400)  # P 2400 > S 2300
    assert r["pf"] == 100
    assert r["var"] == 0  # radicand clamped at 0, no bogus root


def test_rated_load_example(device: Device):
    # 230 V, 16 A, 3680 W -> unity (resistive) at the rating.
    r = _derive(device, 23000, 16000, 3680)
    assert r["va"] == 3680
    assert r["pf"] == 100
    assert r["var"] == 0

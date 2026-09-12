"""A coin cell's charge cannot be read off a straight line.

A CR2032 spends almost its whole life just under 3 V and then falls off a
cliff. Mapping 2.0-3.0 V linearly onto 0-100% therefore reports a nearly empty
cell as most of the way full: at 2.6 V, where such a cell has barely anything
left, the straight line says 60% - so the first warning a user gets is the
device going silent.

The curve here is taken off the light-load discharge characteristic these cells
are specified on, which is the load this firmware puts on one. Alkaline packs
(2xAAA and the like) do fall away steadily, so they keep the straight line and
say so with `A`.
"""

import pytest

from client import StubProc
from conftest import Device
from zcl_consts import (
    ZCL_ATTR_POWER_CFG_BATTERY_PERCENTAGE,
    ZCL_CLUSTER_POWER_CFG,
)

BATTERY_REFRESH_INTERVAL_MS = 300000

COIN_CELL = "mfg;X;BTC5;SA0u;"
ALKALINE = "mfg;X;BTC5A;SA0u;"


def percent_at(config: str, mv: int) -> float:
    """ZCL reports in 0.5% steps, so halve it to get a percentage."""
    with StubProc(device_config=config) as p:
        d = Device(p)
        d.set_battery_voltage(mv)
        d.step_time(BATTERY_REFRESH_INTERVAL_MS + 1)
        return int(d.read_zigbee_attr(
            1, ZCL_CLUSTER_POWER_CFG, ZCL_ATTR_POWER_CFG_BATTERY_PERCENTAGE)) / 2


def test_a_fresh_cell_is_full():
    assert percent_at(COIN_CELL, 3000) == 100


def test_the_plateau_is_not_treated_as_spent():
    """2.9 V is where a coin cell sits for most of its life."""
    assert percent_at(COIN_CELL, 2900) >= 80


def test_the_cliff_is_reported_as_the_cliff():
    """This is the case that matters: 2.6 V is nearly empty, not 60%."""
    assert percent_at(COIN_CELL, 2600) <= 20
    assert percent_at(ALKALINE, 2600) == 60


def test_an_exhausted_cell_reads_empty():
    assert percent_at(COIN_CELL, 2200) == 0
    assert percent_at(COIN_CELL, 1800) == 0


def test_the_curve_never_goes_back_up():
    """A charge that rises as the cell drains would be worse than no reading."""
    last = 101.0
    for mv in range(3100, 1999, -25):
        now = percent_at(COIN_CELL, mv)
        assert now <= last, f"{mv} mV reports more than the step above it"
        last = now


def test_alkaline_keeps_the_straight_line():
    assert percent_at(ALKALINE, 2500) == 50

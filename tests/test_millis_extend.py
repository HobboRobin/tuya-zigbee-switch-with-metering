"""hal_millis() on Telink must not wrap every 268 seconds.

The TLSR8258's system timer is a 32-bit counter at 16 MHz: it wraps after
2^32 / 16e6 = 268.4 s. hal_millis() used to be that counter divided by 16000,
so it fell from 268435 back to 0 every four and a half minutes. Every
`now - then >= interval` in the firmware saw a difference of about four billion
at that point and fired, and no interval longer than 268 s could ever expire
honestly - the relay heartbeat (300 s), the overload auto-reconnect target
(`now + delay` above 268435 is never reached), the energy NVM save interval.

millis_extend() keeps a running millisecond count from tick differences, which
unsigned arithmetic gets right across a wrap. These tests drive it with a
simulated 16 MHz counter, compiled on the host from the same header the
firmware uses.
"""

import pathlib
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
TICKS_PER_MS = 16000
TICK_PERIOD_MS = (1 << 32) / TICKS_PER_MS  # 268435.456

HARNESS = r"""
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "hal/millis_extend.h"

/* Reads "<tick>" lines, prints the clock after each. */
int main(void) {
    millis_extend_t s = {0};
    uint32_t tick;
    while (scanf("%" SCNu32, &tick) == 1) {
        printf("%" PRIu32 "\n", millis_extend(&s, tick, 16000u));
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def harness(tmp_path_factory) -> pathlib.Path:
    d = tmp_path_factory.mktemp("millis")
    src = d / "h.c"
    exe = d / "h"
    src.write_text(HARNESS)
    subprocess.run(
        ["gcc", "-std=c99", "-Wall", "-Werror", f"-I{ROOT / 'src'}", str(src),
         "-o", str(exe)],
        check=True,
    )
    return exe


def _run(exe: pathlib.Path, ticks: list[int]) -> list[int]:
    out = subprocess.run(
        [str(exe)],
        input="\n".join(str(t & 0xFFFFFFFF) for t in ticks),
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split()
    return [int(x) for x in out]


def _ticks_for(ms_points: list[float]) -> list[int]:
    return [int(ms * TICKS_PER_MS) for ms in ms_points]


def test_counts_straight_through_a_tick_counter_wrap(harness):
    # Read every 100 ms across three wraps of the tick counter.
    points = [i * 100.0 for i in range(int(3.2 * TICK_PERIOD_MS / 100))]
    got = _run(harness, _ticks_for(points))
    assert got == [int(p) for p in points]


def test_old_division_really_did_wrap(harness):
    # Guard against the test above passing for the wrong reason: the plain
    # division the firmware used to do is not monotonic over the same span.
    points = [TICK_PERIOD_MS - 50, TICK_PERIOD_MS + 50]
    old = [(t & 0xFFFFFFFF) // TICKS_PER_MS for t in _ticks_for(points)]
    assert old[1] < old[0]
    new = _run(harness, _ticks_for(points))
    assert new[1] - new[0] == 100


def test_intervals_longer_than_the_tick_period_elapse(harness):
    # The relay heartbeat is 300 s; with the old clock it could not elapse.
    start, end = 10_000.0, 10_000.0 + 300_000
    points = [start + i * 1000 for i in range(301)]
    got = _run(harness, _ticks_for(points))
    assert got[-1] - got[0] == 300_000
    assert all(b >= a for a, b in zip(got, got[1:]))


def test_sub_millisecond_remainders_are_not_lost(harness):
    # Reads 1.5 ms apart: a clock that dropped the remainder each call would
    # advance 1 ms per read and fall a third behind.
    points = [i * 1.5 for i in range(20_000)]
    got = _run(harness, _ticks_for(points))
    assert got[-1] == int(points[-1])


def test_long_gap_between_reads_within_one_period(harness):
    # A read 268 s after the previous one (just under a full tick period) is
    # still counted exactly.
    points = [0.0, 268_000.0, 268_001.0]
    assert _run(harness, _ticks_for(points)) == [0, 268_000, 268_001]

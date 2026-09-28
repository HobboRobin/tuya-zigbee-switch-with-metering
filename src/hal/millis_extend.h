#ifndef _HAL_MILLIS_EXTEND_H_
#define _HAL_MILLIS_EXTEND_H_

#include <stdint.h>

// Turns a free-running 32-bit hardware tick counter into a millisecond clock
// that only wraps where a uint32_t of milliseconds wraps: after 49.7 days.
//
// Dividing the tick counter by ticks-per-millisecond is not enough. The
// TLSR8258's system timer counts at 16 MHz, so its 32 bits run out after
// 268.4 s, and a clock derived by plain division drops from 268435 back to 0
// every four and a half minutes. Every `now - then >= interval` in the firmware
// then sees a difference of about four billion at the drop and treats its
// interval as expired, whatever it really was; and every interval longer than
// 268 s can never expire any other way. A fixed-point target such as
// `now + delay` that lands above 268435 is never reached at all.
//
// Only the tick difference since the previous call is used, and that
// difference is correct across a wrap of the tick counter by unsigned
// arithmetic. So the clock stays exact as long as it is read at least once per
// tick-counter period, which the main loop does thousands of times over.
typedef struct {
    uint32_t last_tick;
    uint32_t ms;
    uint32_t rem_ticks;
} millis_extend_t;

static inline uint32_t millis_extend(millis_extend_t *state, uint32_t tick,
                                     uint32_t ticks_per_ms) {
    uint32_t delta = tick - state->last_tick;

    state->last_tick  = tick;
    state->ms        += delta / ticks_per_ms;
    state->rem_ticks += delta % ticks_per_ms;
    if (state->rem_ticks >= ticks_per_ms) {
        state->rem_ticks -= ticks_per_ms;
        state->ms++;
    }
    return state->ms;
}

#endif

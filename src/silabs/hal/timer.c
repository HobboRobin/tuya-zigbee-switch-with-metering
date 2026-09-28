#include "hal/timer.h"
#include "sl_sleeptimer.h"


uint32_t hal_millis() {
    // The 32-bit tick count runs at 32768 Hz and wraps every 36.4 h; converted
    // to milliseconds it would drop from ~131 million back to 0 at that point
    // and every elapsed-time check in the firmware would misfire. The 64-bit
    // count does not wrap, so the milliseconds only wrap where a uint32_t of
    // milliseconds does, after 49.7 days, which unsigned differences handle.
    uint64_t ms = 0;

    sl_sleeptimer_tick64_to_ms(sl_sleeptimer_get_tick_count64(), &ms);
    return (uint32_t)ms;
}

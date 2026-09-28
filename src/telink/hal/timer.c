#include "hal/timer.h"
#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)
#include "hal/millis_extend.h"
#include <stdint.h>

static millis_extend_t millis_state;

uint32_t hal_millis() {
    // clock_time() is a 16 MHz counter that wraps every 268.4 s, so it cannot
    // be divided down to milliseconds directly - see millis_extend.h. The
    // state is shared with whatever calls this from interrupt context, so the
    // update must not be torn.
    u32      r  = drv_disable_irq();
    uint32_t ms = millis_extend(&millis_state, clock_time(),
                                CLOCK_16M_SYS_TIMER_CLK_1MS);

    drv_restore_irq(r);
    return ms;
}

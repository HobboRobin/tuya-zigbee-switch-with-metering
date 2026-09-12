#include "ota_reformating/ensure_ota_scheme.h"
#include "stdint.h"
#include <stdbool.h>

#pragma pack(push, 1)
#include "tl_common.h"
#include "zb_common.h"
#include "zcl_include.h"
#pragma pack(pop)

#include "telink_size_t_hack.h"

#include "device_config/config_parser.h"

#include "app.h"
#include "hal/gpio.h"
#include "hal/timer.h"
#include "hal/telink_zigbee_hal.h"
#include "hal/zigbee.h"

int real_main(startup_state_e state);

static _attribute_ram_code_sec_ bool is_bootloader_mode(void) {
    // Check if we are in bootloader mode by reading the flag
    return(*((u32 *)(BOOTLOADER_MODE_MAIN_ADDR + FLASH_TLNK_FLAG_OFFSET)) ==
           TL_START_UP_FLAG_WHOLE);
}

_attribute_ram_code_sec_ int main(void) {
    if (is_bootloader_mode()) {
        // In bootloader mode, system is partally initialized by bootloader,
        // so no need to call drv_platform_init here.
        // BUT! We cannot call any flash-resident code, as it was linked to run from
        // different offset. So only ram-code functions are allowed here.
        // For example, DO NOT use printf here!
        ensure_correct_ota_scheme();
        SYSTEM_RESET(); // Should not return from above, but just in case, reset
    }

    startup_state_e state = drv_platform_init();
    // Ensure we are not in small-OTA mode.
    ensure_correct_ota_scheme();

    return real_main(state);
}

#if PM_ENABLE
// How long an end device stays awake after joining, before it is allowed to
// start sleeping between polls.
//
// Joining is not the end of the conversation, it is the start of one: the
// coordinator then asks for the active endpoints, a simple descriptor for each
// of them, the basic attributes, and runs whatever enrolment and binding the
// device's clusters call for. That is a minute of back-and-forth on a busy
// network, and a device that goes to sleep the moment commissioning reports
// "done" answers none of it - the interview fails on the very first request
// and the device is left paired but useless, with no bindings and no
// reporting, so not even its buttons work.
//
// The cost is one minute of running current, once, per join.
#define ED_AWAKE_AFTER_JOIN_MS    60000

static uint32_t joined_at_ms = 0;
static uint8_t  was_joined   = 0;

// True once the device has been joined long enough for the coordinator to have
// finished asking. Rejoining restarts the clock, because a coordinator that
// lost us will ask again.
static bool ed_may_sleep_now(void) {
    uint8_t joined =
        (hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED) ? 1 : 0;

    if (joined && !was_joined) {
        joined_at_ms = hal_millis();
    }
    was_joined = joined;

    if (!joined) {
        return true;
    }
    return (hal_millis() - joined_at_ms) >= ED_AWAKE_AFTER_JOIN_MS;
}

#endif

int real_main(startup_state_e state) {
    uint8_t isRetention = (state == SYSTEM_DEEP_RETENTION) ? 1 : 0;

    os_init(isRetention);

    irq_enable();

    if (!isRetention) {
        app_init();
    }
#ifdef ZB_ED_ROLE
    else {
        // Re-configure radio PHY — hardware registers are lost during deep
        // retention.  Without this the MAC layer can hang on the next Data
        // Request (e.g. tl_zbNwkQuickDataPollCb), keeping the radio powered
        // (~4 mA) indefinitely.  Matches the Telink SDK pattern used in
        // sampleContactSensor / sampleSwitch.
        mac_phyReconfig();

        telink_gpio_reinit_after_deep_retention();
        telink_gpio_reinit_interrupts();
    }
#endif

    if (battery.pin != HAL_INVALID_PIN) {
        // Use lower TX power if battery powered
        g_zb_txPowerSet = RF_POWER_INDEX_P3p01dBm;
    }

    drv_wd_setInterval(1000);
    drv_wd_start();

    while (1) {
        drv_wd_clear();
        ev_main();
        drv_wd_clear();
        tl_zbTaskProcedure();
        drv_wd_clear();
        app_task();
        drv_wd_clear();
        report_handler();
        drv_wd_clear();

#if PM_ENABLE
        // bdb_isIdle() is the condition the SDK's own battery samples sleep
        // on, and it is not implied by the two below: commissioning lives in
        // the BDB state machine, which neither tl_stackBusy() nor
        // zb_isTaskDone() speaks for. Sleeping through it means the radio is
        // off while the coordinator is answering, so the device can spend its
        // whole join window asleep and never appear on the network at all -
        // while the router build, which never sleeps, joins first time.
        if (bdb_isIdle() && !tl_stackBusy() && zb_isTaskDone() &&
            ed_may_sleep_now()) {
            telink_gpio_hal_setup_wake_ups();
            // Only use deep retention for battery devices,
            // as it messes with GPIO output state, and relays cannot be
            // driven via PULL-ups, it may cause issues.
            if (battery.pin != HAL_INVALID_PIN) {
                // Never hand drv_pm_lowPowerEnter() an empty timer queue: with
                // nothing to wake it on a schedule it picks plain deep sleep,
                // which loses RAM and can only be ended by a pin. A contact
                // nobody touches then never comes back, and from the network
                // it looks like a device that announced once and died.
                if (ev_timer_nearestGet() == NULL) {
                    continue;
                }
                telink_gpio_to_pull_for_deep_retention();
                drv_pm_lowPowerEnter();
                // If we didn't actually enter deep retention, restore GPIO
                // as it was configured to use pulls for retention
                telink_gpio_reinit_after_deep_retention();
            } else {
                ev_timer_event_t *timerEvt = ev_timer_nearestGet();
                u32 sleepDuration          = 1000;
                if (timerEvt) {
                    sleepDuration = timerEvt->timeout < 1000 ? timerEvt->timeout : 1000;
                }
                drv_pm_sleep(PM_SLEEP_MODE_SUSPEND,
                             PM_WAKEUP_SRC_PAD | PM_WAKEUP_SRC_TIMER, sleepDuration);
            }
        }
#endif
    }

    return 0;
}

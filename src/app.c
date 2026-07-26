#include "device_config/config_parser.h"
#include "device_config/device_type.h"
#include "device_config/nvm_items.h"
#include "device_config/reset.h"
#include "hal/nvm.h"
#include "hal/printf_selector.h"
#include "hal/system.h"
#include "hal/timer.h"
#include "hal/zigbee.h"
#include "hal/zigbee_ota.h"
#include "zigbee/battery_cluster.h"
#include "zigbee/general_commands.h"
#include "zigbee/relay_cluster.h"
#ifdef END_DEVICE
#include "zigbee/poll_control_cluster.h"
#endif

void process_device_type_change() {
    // If device was updated from router to end device or vice versa,
    // we need to do a reset, as the network settings stored by SDK in NVM
    // are not compatible between these device types.
    // Read device type from NVM and compare with current configuration.
    enum device_type_t stored_device_type;
    hal_nvm_status_t   st =
        hal_nvm_read(NV_ITEM_DEVICE_TYPE, sizeof(stored_device_type),
                     (uint8_t *)&stored_device_type);

    if (st != HAL_NVM_SUCCESS) {
        // Unable to read device type from NVM, possibly first boot.
        stored_device_type = CURRENT_DEVICE_TYPE;
        hal_nvm_write(NV_ITEM_DEVICE_TYPE, sizeof(stored_device_type),
                      (uint8_t *)&stored_device_type);
        return;
    }
    if (stored_device_type != CURRENT_DEVICE_TYPE) {
        printf("Device type change detected: %d -> %d\r\n", stored_device_type,
               CURRENT_DEVICE_TYPE);
        // Device type has changed, update NVM and reset device.
        stored_device_type = CURRENT_DEVICE_TYPE;
        hal_nvm_write(NV_ITEM_DEVICE_TYPE, sizeof(stored_device_type),
                      (uint8_t *)&stored_device_type);
        // Perform a factory reset to clear incompatible network settings.
        hal_factory_reset();
        schedule_reboot(2000);
    }
}

void app_init(void) {
    handle_version_changes();
    parse_config(); // Does most of the setup, including all callbacks
                    // registration
    hal_zigbee_init_ota();
    init_global_attr_write_callback();

    process_device_type_change();
}

static bool boot_announce_sent = false;

// Firmware-side relay-state heartbeat: the Telink stack sends no periodic
// max-interval report for a boolean attribute, so a single lost onOff report
// would leave Z2M showing the wrong state indefinitely (observed: relay on,
// >500 W flowing, Z2M stuck "off"). Re-push every relay's state on this
// interval so the coordinator re-syncs within it regardless of the mesh.
#define RELAY_HEARTBEAT_INTERVAL_MS    (5u * 60u * 1000u)

void app_task() {
    energy_monitoring_tick();

#ifdef END_DEVICE
    poll_control_cluster_update();
#endif

    // TODO: add jitter to avoid all devices trying to join at once
    if (hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINED &&
        hal_zigbee_get_network_status() != HAL_ZIGBEE_NETWORK_JOINING) {
        hal_zigbee_start_network_steering();
    }
    if (!boot_announce_sent &&
        hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED) {
        hal_zigbee_send_announce();
        boot_announce_sent = true;
    }
    if (hal_zigbee_get_network_status() == HAL_ZIGBEE_NETWORK_JOINED) {
        init_energy_reporting();

        // Start the interval at the first tick after joining rather than
        // reporting immediately: joining already syncs the state, so the
        // heartbeat only needs to cover losses from then on.
        static uint8_t  heartbeat_armed         = 0;
        static uint32_t last_relay_heartbeat_ms = 0;
        uint32_t        now = hal_millis();
        if (!heartbeat_armed) {
            heartbeat_armed         = 1;
            last_relay_heartbeat_ms = now;
        } else if ((now - last_relay_heartbeat_ms) >=
                   RELAY_HEARTBEAT_INTERVAL_MS) {
            last_relay_heartbeat_ms = now;
            relay_clusters_report_state();
        }
    }
}

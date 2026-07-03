#ifndef _BL0942_H_
#define _BL0942_H_

#include <stdint.h>
#include "hal/gpio.h"
#include "hal/tasks.h"
#include "base_components/energy_meter.h"

// BL0942 energy metering IC, connected over UART (8N1, factory default
// 4800 baud). The MCU polls it with {0x58, 0xAA} ("read all") and receives a
// 23-byte packet: 0x55 header, I_RMS[3], V_RMS[3], I_FAST_RMS[3], WATT[3,
// signed], CF_CNT[3], FREQ[2], 4 status/reserved bytes, checksum. All values
// little-endian. Checksum = (0x58 + sum of bytes 0..21) ^ 0xFF.
//
// Default calibration for the usual Tuya wiring (1 mOhm shunt, 390k x 5/510R
// divider; reference constants: 15883.34 counts/V, 251065.7 counts/A,
// 623.03 counts/W). Physical value = raw * MULTIPLIER / FIXED_POINT_SCALE with
// the same output units as the HLW8012 driver: voltage in cV, current in mA,
// power in W. Field-adjustable via the on-device calibrate attributes or the
// config_str V/A/W markers.
#define BL0942_FIXED_POINT_SCALE     65536
#ifndef BL0942_VOLTAGE_MULTIPLIER
#define BL0942_VOLTAGE_MULTIPLIER    413  // 65536 * 100 / 15883.34
#endif
#ifndef BL0942_CURRENT_MULTIPLIER
#define BL0942_CURRENT_MULTIPLIER    261  // 65536 * 1000 / 251065.7
#endif
#ifndef BL0942_POWER_MULTIPLIER
#define BL0942_POWER_MULTIPLIER      105  // 65536 / 623.03
#endif

#define BL0942_DEFAULT_BAUDRATE      4800
#define BL0942_POLL_INTERVAL_MS      1000

// Temporary hardware bring-up aid: when 1, get_data() reports the UART link
// counters (polls/rx-bytes/headers/checksums) on the measurement tiles instead
// of the real readings, so the link can be diagnosed without a debug UART.
// Set back to 0 once BL0942 metering is confirmed working.
#ifndef BL0942_UART_DIAG
#define BL0942_UART_DIAG       1
#endif
#define BL0942_FRAME_LEN       23
#define BL0942_RX_RING_SIZE    64

// Energy accumulates power[W] * elapsed[ms]; 1 Wh = 3,600,000 W*ms. Kept in
// 32-bit with subtract-carry (TC32 has no 64-bit divide).
#define BL0942_ENERGY_WH_SUBUNIT    3600000u

typedef struct {
    uint16_t voltage;    // cV
    uint16_t current;    // mA
    int16_t  power;      // W
    uint32_t energy;     // Wh
    uint32_t energy_acc; // W*ms remainder (< 1 Wh)
    uint32_t last_frame_time;
    uint8_t  valid;
    // Raw register values behind the most recent reading, for on-device
    // calibration (multiplier = reference * FIXED_POINT_SCALE / raw).
    uint32_t raw_voltage;
    uint32_t raw_current;
    uint32_t raw_power;
} bl0942_data_t;

typedef struct {
    uint32_t voltage_multiplier;
    uint32_t current_multiplier;
    uint32_t power_multiplier;
} bl0942_calibration_t;

typedef struct {
    hal_gpio_pin_t       tx_pin;
    hal_gpio_pin_t       rx_pin;
    bl0942_data_t        data;
    bl0942_calibration_t cal;
    hal_task_t           poll_task;
    uint8_t              initialized;
    // Single-producer (rx irq) / single-consumer (poll task) byte ring.
    volatile uint8_t     rx_ring[BL0942_RX_RING_SIZE];
    volatile uint8_t     rx_head;
    uint8_t              rx_tail;
    energy_meter_t       meter;
    // Temporary link diagnostics surfaced through swBuildId.
    volatile uint16_t    diag_rx_bytes;
    uint16_t             diag_polls;
    uint8_t              diag_headers;
    uint8_t              diag_checksums;
} bl0942_t;

/** Initialize the driver and start polling. Returns 0 on success. */
int bl0942_init(bl0942_t *dev, hal_gpio_pin_t tx_pin, hal_gpio_pin_t rx_pin,
                uint32_t baudrate);

// Override calibration multipliers. A zero argument keeps the current value.
void bl0942_set_calibration(bl0942_t *dev, uint32_t voltage_mult,
                            uint32_t current_mult, uint32_t power_mult);

energy_meter_t *bl0942_as_energy_meter(bl0942_t *dev);

// Exposed for host tests: feed received bytes / parse as the poll task would.
void bl0942_rx_feed(bl0942_t *dev, const uint8_t *data, uint16_t len);
void bl0942_process_rx(bl0942_t *dev);

#endif /* _BL0942_H_ */

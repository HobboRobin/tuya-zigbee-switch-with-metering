#include "hal/uart.h"

// Silabs devices do not currently use the UART energy metering path. Report
// failure so the driver stays disabled; a real EUSART implementation can be
// added if a Silabs board ever needs it.
int hal_uart_init(hal_gpio_pin_t tx_pin, hal_gpio_pin_t rx_pin,
                  uint32_t baudrate, hal_uart_rx_callback_t rx_cb) {
    (void)tx_pin;
    (void)rx_pin;
    (void)baudrate;
    (void)rx_cb;
    return -1;
}

void hal_uart_send(const uint8_t *data, uint8_t len) {
    (void)data;
    (void)len;
}

void hal_uart_task(void) {
}

#ifndef HUB_UART_H
#define HUB_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t completed_writes;
    uint32_t bytes_queued;
    uint32_t timeouts;
} hub_uart_diagnostics_t;

extern volatile hub_uart_diagnostics_t hub_uart_diagnostics;

/* USART2 TX on PA2, 115200 baud, 8N1. */
void hub_uart_init(void);

/*
 * Call timing_init() and hub_uart_init() first.
 * Transmit a buffer, with a 100 ms timeout for the whole write.
 */
bool hub_uart_write(const char *text, size_t length);

#endif
#include "hub_uart.h"
#include "stm32_regs.h"
#include "timing.h"

#define HUB_RCC_APB1ENR REG32(0x40023840UL)
#define HUB_RCC_APB1RSTR REG32(0x40023820UL)

#define HUB_UART_SR  REG32(0x40004400UL)
#define HUB_UART_DR  REG32(0x40004404UL)
#define HUB_UART_BRR REG32(0x40004408UL)
#define HUB_UART_CR1 REG32(0x4000440CUL)
#define HUB_UART_CR2 REG32(0x40004410UL)
#define HUB_UART_CR3 REG32(0x40004414UL)

#define UART_TX_EMPTY (1UL << 7)
#define UART_COMPLETE (1UL << 6)

volatile hub_uart_diagnostics_t hub_uart_diagnostics = {0};

void hub_uart_init(void)
{
    /* Enable GPIOA and USART2 clocks. */
    RCC_AHB1ENR |= (1UL << 0);
    (void)RCC_AHB1ENR;

    HUB_RCC_APB1ENR |= (1UL << 17);
    (void)HUB_RCC_APB1ENR;

    /* Reset USART2 to a known state. */
    HUB_RCC_APB1RSTR |= (1UL << 17);
    HUB_RCC_APB1RSTR &= ~(1UL << 17);

    /* PA2: push-pull, medium speed, no pull-up/down. */
    GPIOA_OTYPER &= ~(1UL << 2);

    GPIOA_OSPEEDR =
        (GPIOA_OSPEEDR & ~(3UL << 4)) |
        (1UL << 4);

    GPIOA_PUPDR &= ~(3UL << 4);

    /* PA2 alternate function 7: USART2 TX. */
    GPIOA_AFRL =
        (GPIOA_AFRL & ~(15UL << 8)) |
        (7UL << 8);

    GPIOA_MODER =
        (GPIOA_MODER & ~(3UL << 4)) |
        (2UL << 4);

    HUB_UART_CR1 = 0U;
    HUB_UART_CR2 = 0U;
    HUB_UART_CR3 = 0U;

    /* APB1 = 16 MHz, oversampling by 16, approximately 115200 baud. */
    HUB_UART_BRR = 139U;

    /* Enable USART2 and its transmitter. */
    HUB_UART_CR1 = (1UL << 13) | (1UL << 3);
}

static bool wait_for_flag(uint32_t flag, uint32_t start)
{
    while ((HUB_UART_SR & flag) == 0U)
    {
        if (timing_has_elapsed(start, 100U))
        {
            hub_uart_diagnostics.timeouts++;
            return false;
        }
    }

    return true;
}

bool hub_uart_write(const char *text, size_t length)
{
    if ((text == 0) || (length == 0U))
    {
        return false;
    }

    uint32_t start = timing_cycles();

    for (size_t index = 0U; index < length; index++)
    {
        if (timing_has_elapsed(start, 100U))
        {
            hub_uart_diagnostics.timeouts++;
            return false;
        }

        if (!wait_for_flag(UART_TX_EMPTY, start))
        {
            return false;
        }

        HUB_UART_DR = (uint8_t)text[index];
        hub_uart_diagnostics.bytes_queued++;
    }

    /* Wait until the final byte has left the transmitter. */
    if (!wait_for_flag(UART_COMPLETE, start))
    {
        return false;
    }

    hub_uart_diagnostics.completed_writes++;

    return true;
}
#include "stm32_regs.h"

/*
 * Called before main(), by the startup assembly.
 * Keep the reset-default 16 MHz HSI clock.
 */
void SystemInit(void)
{
    SCB_VTOR = 0x08000000UL;

    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}

/* Hook called by the C library initialization routine. */
void _init(void)
{
}
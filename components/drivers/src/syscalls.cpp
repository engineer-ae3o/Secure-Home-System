#include "stm32f103xb.h"
#include "common.hpp"

#include "FreeRTOS.h"
#include "task.h"

#define PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD 1
#include "printf.h"

#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>


extern "C" {

    uint32_t SystemCoreClock;

    // Prevent name mangling: called from the reset handler
    void system_init() {
        // Enable the prefetch queue and set the flash latency to 2 wait states
        FLASH->ACR |= (FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_1);

        // Enable the HSE
        RCC->CR |= RCC_CR_HSEON;
        while (!(RCC->CR & RCC_CR_HSERDY));

        // Set HSE as PLL source and multiply the 8MHz HSE by 9 to get a 72MHz clock speed
        RCC->CFGR |= (RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9);

        // Set the buses' prescalers
        RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
        //            AHB: 72MHz         | APB1: 36MHz         | APB2: 72MHz
        RCC->CFGR |= (RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1);

        // Enable the PLL
        RCC->CR |= RCC_CR_PLLON;
        while (!(RCC->CR & RCC_CR_PLLRDY));

        // Switch the system clock to the PLL
        RCC->CFGR |= RCC_CFGR_SW_PLL;
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

        // Update core clock settings
        SystemCoreClock = 72'000'000UL;

        // Enable memfault, bus faults and usage fault exceptions
        SCB->SHCSR |= (SCB_SHCSR_MEMFAULTENA_Msk |
                       SCB_SHCSR_BUSFAULTENA_Msk |
                       SCB_SHCSR_USGFAULTENA_Msk);

        NVIC_EnableIRQ(HardFault_IRQn);
        NVIC_EnableIRQ(MemoryManagement_IRQn);
        NVIC_EnableIRQ(BusFault_IRQn);
        NVIC_EnableIRQ(UsageFault_IRQn);

        // Enable exceptions on divide by 0 and unaligned trapping
        SCB->CCR |= (SCB_CCR_DIV_0_TRP_Msk | SCB_CCR_UNALIGN_TRP_Msk);
    }

    void HardFault_Handler
    void MemManage_Handler
    void BusFault_Handler
    void UsageFault_Handler

    void putchar_(char c) {
        // TODO: Implement `putchar_(char)`
        (void)c;
    }

    void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
        (void)xTask;
        printf("Stack overflow in %s task. Activating debugger", pcTaskName);
        __ASM("bkpt 1");
    }

    int _close(int) {
        return 0;
    }

    int _lseek(int, int, int) {
        return 0;
    }

    int _read(int, char*, int) {
        return 0;
    }

    int _write(int, char*, int) {
        return 0;
    }

    caddr_t _sbrk(int) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    int _fstat(int, struct stat*) {
        return 0;
    }

    int _isatty(int) {
        return 0;
    }

    void _exit(int) {
        __asm("bkpt 1");
        while (1);
    }
}

const char* hal_err_to_string(hal_err_t err) {
    switch (err) {
        case hal_err_t::HAL_OK:                      return "HAL_OK";
        case hal_err_t::HAL_FAIL:                    return "HAL_FAIL";
        case hal_err_t::HAL_INVALID_ARG:             return "HAL_INVALID_ARG";
        case hal_err_t::HAL_INVALID_STATE:           return "HAL_INVALID_STATE";
        case hal_err_t::HAL_TIMEOUT:                 return "HAL_TIMEOUT";
        case hal_err_t::HAL_TX_ERROR:                return "HAL_TX_ERROR";
        case hal_err_t::HAL_RX_ERROR:                return "HAL_RX_ERROR";
        default:                                     return "";
    }
}

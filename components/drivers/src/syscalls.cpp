#include "stm32f103xb.h"
#include "config.hpp"

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
        // ST's engineers are crazy. _1 is for 2 wait states, not the _2 macro
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
        SystemCoreClock = config::CLOCK_SPEED_HZ;

        // Enable bus fault and usage fault exceptions
        SCB->SHCSR |= (SCB_SHCSR_BUSFAULTENA_Msk |
                       SCB_SHCSR_USGFAULTENA_Msk);
        
        // Enable exceptions on divide by 0 and unaligned trapping
        SCB->CCR |= (SCB_CCR_DIV_0_TRP_Msk | SCB_CCR_UNALIGN_TRP_Msk);
    }

    __attribute__((naked))
    void HardFault_Handler() {
        __asm volatile (
            "tst lr, #4\n"
            "ite eq\n"
            "mrseq r0, msp\n"
            "mrsne r0, psp\n"
            "b hard_fault_dump\n"
        );
    }
     
    __attribute__((naked))
    void BusFault_Handler() {
        __asm volatile (
            "tst lr, #4\n"
            "ite eq\n"
            "mrseq r0, msp\n"
            "mrsne r0, psp\n"
            "b bus_fault_dump\n"
        );
    }

    __attribute__((naked))
    void UsageFault_Handler() {
        __asm volatile (
            "tst lr, #4\n"
            "ite eq\n"
            "mrseq r0, msp\n"
            "mrsne r0, psp\n"
            "b usage_fault_dump\n"
        );
    }

    [[noreturn]] void hard_fault_dump(uint32_t* frame) {
        [[maybe_unused]] volatile uint32_t r0 = frame[0];
        [[maybe_unused]] volatile uint32_t r1 = frame[1];
        [[maybe_unused]] volatile uint32_t r2 = frame[2];
        [[maybe_unused]] volatile uint32_t r3 = frame[3];
        [[maybe_unused]] volatile uint32_t r12 = frame[4];
        [[maybe_unused]] volatile uint32_t lr = frame[5];
        [[maybe_unused]] volatile uint32_t pc = frame[6];
        [[maybe_unused]] volatile uint32_t psr = frame[7];
        __asm volatile ("bkpt #0");
        while (1);
    }

    [[noreturn]] void bus_fault_dump(uint32_t* frame) {
        [[maybe_unused]] volatile uint32_t r0 = frame[0];
        [[maybe_unused]] volatile uint32_t r1 = frame[1];
        [[maybe_unused]] volatile uint32_t r2 = frame[2];
        [[maybe_unused]] volatile uint32_t r3 = frame[3];
        [[maybe_unused]] volatile uint32_t r12 = frame[4];
        [[maybe_unused]] volatile uint32_t lr = frame[5];
        [[maybe_unused]] volatile uint32_t pc = frame[6];
        [[maybe_unused]] volatile uint32_t cfsr = SCB->CFSR;
        __asm volatile ("bkpt #0");
        while (1);
    }
    
    [[noreturn]] void usage_fault_dump(uint32_t* frame) {
        [[maybe_unused]] volatile uint32_t r0 = frame[0];
        [[maybe_unused]] volatile uint32_t r1 = frame[1];
        [[maybe_unused]] volatile uint32_t r2 = frame[2];
        [[maybe_unused]] volatile uint32_t r3 = frame[3];
        [[maybe_unused]] volatile uint32_t r12 = frame[4];
        [[maybe_unused]] volatile uint32_t lr = frame[5];
        [[maybe_unused]] volatile uint32_t pc = frame[6];
        [[maybe_unused]] volatile uint32_t cfsr = SCB->CFSR;
        __asm volatile ("bkpt #0");
        while (1);
    }
    
    void putchar_(char c) {
        // TODO: Implement `putchar_(char)`
        (void)c;
    }

    void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName) {
        (void)xTask;
        printf("Stack overflow in %s. Activating debugger", pcTaskName);
        __asm volatile ("bkpt #0");
        while (1);
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

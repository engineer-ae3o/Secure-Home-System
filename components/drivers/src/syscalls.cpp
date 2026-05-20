#include "stm32f1xx_hal.h"
#include "config.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include "etl/atomic.h"


extern "C" {

    uint32_t SystemCoreClock{};
    
    void system_init() {
        // Enable the prefetch queue and set the flash latency to 2 wait states
        // ST's engineers are crazy. _1 is for 2 wait states, not the _2 macro
        FLASH->ACR |= (FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_1);

        // Enable the HSE
        RCC->CR |= RCC_CR_HSEON;
        while (!(RCC->CR & RCC_CR_HSERDY));

        // Set HSE as PLL source and multiply the 8MHz HSE by 9 to get a 72MHz clock speed
        RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL);
        RCC->CFGR |= (RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9);

        // Set the buses' prescalers
        RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
        //            AHB: 72MHz         | APB1: 36MHz         | APB2: 72MHz
        RCC->CFGR |= (RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1);

        // Enable the PLL
        RCC->CR |= RCC_CR_PLLON;
        while (!(RCC->CR & RCC_CR_PLLRDY));

        // Switch the system clock to the PLL
        RCC->CFGR &= ~RCC_CFGR_SW;
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
        [[maybe_unused]] const volatile uint32_t r0 = frame[0];
        [[maybe_unused]] const volatile uint32_t r1 = frame[1];
        [[maybe_unused]] const volatile uint32_t r2 = frame[2];
        [[maybe_unused]] const volatile uint32_t r3 = frame[3];
        [[maybe_unused]] const volatile uint32_t r12 = frame[4];
        [[maybe_unused]] const volatile uint32_t lr = frame[5];
        [[maybe_unused]] const volatile uint32_t pc = frame[6];
        [[maybe_unused]] const volatile uint32_t psr = frame[7];
        [[maybe_unused]] const volatile uint32_t cfsr = SCB->CFSR;
        __asm volatile ("bkpt #0");
        while (1);
    }

    [[noreturn]] void bus_fault_dump(uint32_t* frame) {
        [[maybe_unused]] const volatile uint32_t r0 = frame[0];
        [[maybe_unused]] const volatile uint32_t r1 = frame[1];
        [[maybe_unused]] const volatile uint32_t r2 = frame[2];
        [[maybe_unused]] const volatile uint32_t r3 = frame[3];
        [[maybe_unused]] const volatile uint32_t r12 = frame[4];
        [[maybe_unused]] const volatile uint32_t lr = frame[5];
        [[maybe_unused]] const volatile uint32_t pc = frame[6];
        [[maybe_unused]] const volatile uint32_t cfsr = SCB->CFSR;
        [[maybe_unused]] const volatile uint32_t bfar = SCB->BFAR;
        __asm volatile ("bkpt #0");
        while (1);
    }
    
    [[noreturn]] void usage_fault_dump(uint32_t* frame) {
        [[maybe_unused]] const volatile uint32_t r0 = frame[0];
        [[maybe_unused]] const volatile uint32_t r1 = frame[1];
        [[maybe_unused]] const volatile uint32_t r2 = frame[2];
        [[maybe_unused]] const volatile uint32_t r3 = frame[3];
        [[maybe_unused]] const volatile uint32_t r12 = frame[4];
        [[maybe_unused]] const volatile uint32_t lr = frame[5];
        [[maybe_unused]] const volatile uint32_t pc = frame[6];
        [[maybe_unused]] const volatile uint32_t cfsr = SCB->CFSR;
        __asm volatile ("bkpt #0");
        while (1);
    }
    
    void vApplicationStackOverflowHook(TaskHandle_t, char*) {
        __asm volatile ("bkpt #0");
        while (1);
    }
    
    void vPortSetupTimerInterrupt() {
        SysTick->VAL = 0;
        SysTick->LOAD = (config::CLOCK_SPEED_HZ / configTICK_RATE_HZ);
        SysTick->CTRL |= (SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk);
    }

    void vApplicationIdleHook() {
        __WFI();
    }

    // Setup the timer to be used by the HAL since
    // FreeRTOS already consumes SysTick
    static volatile etl::atomic<uint32_t> s_hal_tick{};

    HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority) {
        // Enable TIM2 clock
        __HAL_RCC_TIM2_CLK_ENABLE();

        // Configure TIM2 for 1ms interrupts at 72MHz
        // Prescaler: 72MHz / 72 = 1MHz, so a PSC of 72 - 1 = 71
        // Auto reload: 1kHz, so an ARR of 1000 - 1 = 999
        TIM2->PSC = 71;
        TIM2->ARR = 999;
        TIM2->EGR = TIM_EGR_UG;
        TIM2->SR &= ~TIM_SR_UIF;
        TIM2->DIER |= TIM_DIER_UIE;
        TIM2->CR1 |= TIM_CR1_CEN;

        // Configure NVIC settings for TIM2
        NVIC_EnableIRQ(TIM2_IRQn);
        NVIC_SetPriority(TIM2_IRQn, TickPriority);

        return HAL_OK;
    }

    uint32_t HAL_GetTick() {
        return s_hal_tick;
    }
    
    void TIM2_IRQHandler() {
        if (TIM2->SR & TIM_SR_UIF) {
            // Clear update interrupt flag and increment the timer
            TIM2->SR &= ~TIM_SR_UIF;
            s_hal_tick++;
        }
    }
    
}

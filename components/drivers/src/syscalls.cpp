#include "stm32f103xb.h"
#include "common.hpp"


uint32_t SystemCoreClock;

extern "C" {
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
        case hal_err_t::HAL_I2C_DEVICE_NOT_FOUND:    return "HAL_I2C_DEVICE_NOT_FOUND";
        case hal_err_t::HAL_I2C_ARBITRATION_LOST:    return "HAL_I2C_ARBITRATION_LOST";
        case hal_err_t::HAL_UART_TC_FAILED_TO_SET:   return "HAL_UART_TC_FAILED_TO_SET";
        case hal_err_t::HAL_DMA_TE:                  return "HAL_DMA_TE";
        case hal_err_t::HAL_DMA_DME:                 return "HAL_DMA_DME";
        case hal_err_t::HAL_DMA_ERR_UNKNOWN:         return "HAL_DMA_ERR_UNKNOWN";
        default:                                     return "";
    }
}

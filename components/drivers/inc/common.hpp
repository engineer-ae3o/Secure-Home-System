#pragma once


#include <cstdint>


// Core clock speed being used
constexpr inline uint32_t CLOCK_SPEED_HZ = 72'000'000UL;

// At 72MHz, this is ~140us: suitable for most use cases
constexpr inline uint32_t TIMEOUT_CYCLES = 10'000UL;


enum class hal_err_t : uint8_t {
    // Success
    HAL_OK = 0U,

    // Generic failure
    HAL_FAIL,

    // More specific errors
    HAL_INVALID_ARG,
    HAL_INVALID_STATE,
    HAL_TIMEOUT,

    // Generic Transfer and Reception failures
    HAL_TX_ERROR,
    HAL_RX_ERROR,

    // I2C extensions
    HAL_I2C_DEVICE_NOT_FOUND,
    HAL_I2C_ARBITRATION_LOST,
    
    // UART extension
    HAL_UART_TC_FAILED_TO_SET,
    
    // DMA extensions
    HAL_DMA_TE,
    HAL_DMA_DME,
    HAL_DMA_ERR_UNKNOWN,
    
};

const char* hal_err_to_string(hal_err_t err);

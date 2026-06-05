#pragma once

#include "config.hpp"

namespace utils {

    enum class [[nodiscard]] error_t : uint8_t {
        // Success
        NONE,

        // Standard errors common to all modules
        ERR_FAIL,
        ERR_TIMEOUT,
        ERR_INVALID_STATE,
        ERR_INVALID_ARG,
        ERR_HAL_FAIL,

        // GSM module driver errors
        GSM_SIM_NOT_FOUND,
        GSM_SIM_NOT_REGISTERED,
        GSM_MODULE_NOT_ALIVE,
        GSM_BAD_NETWORK_CONN,
        GSM_SMS_SEND_FAIL,

        // File IO errors
        FILE_FAILED_TO_SEEK,
        FILE_FAILED_TO_SYNC,
        FILE_FAILED_TO_READ,
        FILE_FAILED_TO_WRITE,
        FILE_FAILED_TO_OPEN,
        FILE_FAILED_TO_CLOSE,
        FILE_FS_CORRUPTED,
        FILE_FS_FAILED_TO_FORMAT,
        FILE_FS_FAILED_TO_MOUNT,
        FILE_FS_FAILED_TO_UNMOUNT,
    };

    [[noreturn]] inline void panic() {
        __asm volatile("bkpt #0");
        while (true) {
        }
    }

    inline void assert_check(bool cond) {
        if constexpr (config::ASSERTS_ENABLED) {
            if (!cond) {
                panic();
            }
        }
    }

    inline error_t gpio_enable_clk(GPIO_TypeDef* handle) {
        if (handle == GPIOA) {
            __HAL_RCC_GPIOA_CLK_ENABLE();
        } else if (handle == GPIOB) {
            __HAL_RCC_GPIOB_CLK_ENABLE();
        } else if (handle == GPIOC) {
            __HAL_RCC_GPIOC_CLK_ENABLE();
        } else if (handle == GPIOD) {
            __HAL_RCC_GPIOD_CLK_ENABLE();
        } else {
            return error_t::ERR_INVALID_ARG;
        }
        return error_t::NONE;
    }

    // Needed for conversion since FreeRTOS uses words
    consteval size_t bytes_to_words(size_t byte) {
        return byte / 4;
    }

} // namespace utils

// Macros for error checking and propagating
#define TRY(func)                                                                                                                          \
    do {                                                                                                                                   \
        if (auto ret_ = (func); ret_ != utils::error_t::NONE) {                                                                            \
            return ret_;                                                                                                                   \
        }                                                                                                                                  \
    } while (0)

#define TRY_HAL(func)                                                                                                                      \
    do {                                                                                                                                   \
        if (auto ret_ = (func); ret_ != HAL_OK) {                                                                                          \
            return utils::error_t::ERR_HAL_FAIL;                                                                                           \
        }                                                                                                                                  \
    } while (0)

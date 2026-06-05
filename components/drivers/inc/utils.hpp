#pragma once

#include "config.hpp"

namespace utils {

    enum class [[nodiscard]] error_t : uint8_t {
        NONE,

        ERR_FAIL,
        ERR_TIMEOUT,
        ERR_INVALID_STATE,
        ERR_INVALID_ARG,
        ERR_HAL_FAILED_TO_INIT,
        ERR_HAL_FAILED_TO_DEINIT,

        // GSM module driver errors

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

    // Needed for conversion since FreeRTOS uses words
    consteval size_t bytes_to_words(size_t bytes) {
        return bytes / 4;
    }

} // namespace utils

// Macro for error checking and propagating
#define TRY(func)                                                                                  \
    do {                                                                                           \
        if (auto ret = (func()); ret != utils::error_t::NONE) {                                    \
            return ret;                                                                            \
        }                                                                                          \
    } while (0)

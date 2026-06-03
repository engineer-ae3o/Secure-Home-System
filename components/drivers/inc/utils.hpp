#pragma once

#include "config.hpp"

namespace utils {

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

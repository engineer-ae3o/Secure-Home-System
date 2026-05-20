#pragma once


#include "config.hpp"


namespace utils {
    
    [[noreturn]] inline void panic() {
        __asm volatile ("bkpt #0");
        while (1);
    }
    
    inline void assert_check(bool cond) {
        if constexpr (config::ASSERTS_ENABLED) {
            if (!cond) panic();
        }
    }

} // namespace utils

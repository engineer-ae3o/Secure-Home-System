#pragma once


#include "config.hpp"

#define PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD 1
#include "printf.h"

#include <utility>
#include <source_location>


namespace utils {

    template <config::log_level_t level, typename... Args>
    void log(const char* fmt, Args&&... args) {

        using enum config::log_level_t;
        static_assert(level != NONE);

        if constexpr (std::to_underlying(level) <= std::to_underlying(config::LOG_LEVEL)) {
            char msg[128]{};

            // Fixing compilation error because GCC is unable to figure out
            // what's in `fmt` since it's a `const char*`, not a format string
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wformat-nonliteral"
            snprintf(msg, sizeof(msg), fmt, std::forward<Args>(args)...);
            #pragma GCC diagnostic pop
            
            if constexpr (level == ERROR)     printf("[ERROR]: %s", msg);
            else if constexpr (level == WARN) printf("[WARN]: %s", msg);
            else if constexpr (level == INFO) printf("[INFO]: %s", msg);
        }
    }

    [[noreturn]] inline void panic(const char* msg) {
        log<config::log_level_t::ERROR>("Unrecoverable error. Halting system: %s", msg);
        __asm volatile ("bkpt #0");
        while (1);
    }
    
    inline void assert_check(bool cond, const char* msg, const std::source_location& loc = std::source_location::current()) {
        if constexpr (config::ASSERTS_ENABLED) {
            if (!cond) {
                log<config::log_level_t::ERROR>("Assert failed (%s). File: %s. Line: %u. Function: %s",
                                                msg, loc.file_name(), loc.line(), loc.function_name());
                panic(msg);
            }
        }
    }

} // namespace utils

#define ASSERT(cond) utils::assert_check((cond), #cond)

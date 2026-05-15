#pragma once


#include <cstdint>


namespace config {

    enum class log_level_t : uint8_t {
        NONE = 0,
        ERROR,
        WARN,
        INFO
    };

    constexpr inline log_level_t LOG_LEVEL = log_level_t::INFO;
    constexpr inline bool ASSERTS_ENABLED = true;

    // Core clock speed being used
    constexpr inline uint32_t CLOCK_SPEED_HZ = 72'000'000UL;

    consteval inline auto bytes_to_words(auto bytes) { return bytes / 4; }

} // namespace config

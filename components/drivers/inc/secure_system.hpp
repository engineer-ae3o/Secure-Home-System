#pragma once

#include "utils.hpp"

#include <cstdint>

namespace ss {

    constexpr inline uint32_t MAX_PNUMBERS     = 5;
    constexpr inline uint32_t MAX_PASSWORD_LEN = 16;

    /**
     * Initialize the secure system.
     */
    utils::error_t init();

    /**
     * Deinitialize the secure system.
     */
    utils::error_t deinit();

} // namespace ss

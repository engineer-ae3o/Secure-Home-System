#pragma once

#include <cstdint>

namespace ss {

    constexpr inline uint32_t MAX_PNUMBERS{5};

    /**
     * Initialize the secure system.
     */
    void init();

    /**
     * Deinitialize the secure system.
     */
    void deinit();

} // namespace ss

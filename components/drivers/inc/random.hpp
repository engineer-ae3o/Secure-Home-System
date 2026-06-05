#pragma once

#include "utils.hpp"

#include <cstdint>
#include <span>

namespace rnd {

    /**
     * @brief Initializes the random number generator subsystem.
     */
    utils::error_t init();

    /**
     * @brief Deinitializes the random number generator subsystem.
     */
    utils::error_t deinit();

    /**
     * @brief Gets a random number from the generator.
     * 
     * @param buffer The buffer to fill with random bytes.
     */
    utils::error_t get_random_numbers(std::span<uint8_t> buffer);

} // namespace rnd

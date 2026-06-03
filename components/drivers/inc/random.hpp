#pragma once

#include <cstdint>
#include <span>

namespace rnd {

    /**
     * @brief Initializes the random number generator subsystem.
     */
    void init();

    /**
     * @brief Deinitializes the random number generator subsystem.
     */
    void deinit();

    /**
     * @brief Gets a random number from the generator.
     * 
     * @param buffer The buffer to fill with random bytes.
     */
    void get_random_numbers(std::span<uint8_t> buffer);

} // namespace rnd

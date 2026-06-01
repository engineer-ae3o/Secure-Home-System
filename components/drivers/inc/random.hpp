#pragma once

#include <cstdint>
#include <span>

namespace rnd {

    /**
     * @brief Initializes the random number generator subsystem.
     */
    void init();

    /**
     * @brief Gets a random number from the generator.
     * 
     * @param buffer The buffer to fill with random bytes.
     */
    void get_random_numbers(std::span<uint8_t> buffer);

    /**
     * @brief Updates the state of the random number generator.
     *        Should be called periodically to ensure that the generator has fresh entropy.
     */
    void update_rng_state();

} // namespace rnd

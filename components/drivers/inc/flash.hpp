#pragma once

#include <span>
#include <cstdint>

namespace file {

    enum class name_t : uint8_t {
        COUNTER,
        PASSWORD,
        PNUMBERS,
        COUNT,
    };

    /**
     *
     */
    void init();

    /**
     *
     */
    void deinit();

    /**
     *
     */
    uint32_t get_count_value();

    /**
     *
     */
    void write(name_t file, const std::span<uint8_t>& data);

    /**
     *
     */
    void read(name_t file, std::span<uint8_t>& data);

    /**
     *
     */
    void sync(name_t file);

} // namespace file

#pragma once

#include "etl/span.h"

namespace file {

    enum class name_t : uint8_t {
        COUNTER,
        PASSWORD,
        PNUMBERS,
        COUNT,
    };

    void init();

    void deinit();

    uint32_t get_count_value();

    void write(name_t file, const etl::span<uint8_t>& data);

    void read(name_t file, etl::span<uint8_t>& data);

} // namespace file

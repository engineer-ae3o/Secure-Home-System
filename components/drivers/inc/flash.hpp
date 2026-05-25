#pragma once

#include "etl/bit.h"
#include "etl/span.h"
#include "etl/string_view.h"

namespace file {

    void init();

    void deinit();

    void create(const etl::string_view& file_path);

    void open(const etl::string_view& file_path);

    void write(const etl::string_view& file_path, const etl::span<etl::byte>& data);

    void read(const etl::string_view& file_path, etl::span<etl::byte>& data);

    void close(const etl::string_view& file_path);

    void remove(const etl::string_view& file_path);

} // namespace file

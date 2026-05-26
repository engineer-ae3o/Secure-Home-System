#include "flash.hpp"

namespace file {

    void init() {
    }

    void deinit() {
    }

    void create(const etl::string_view& file_path) {
        (void)file_path;
    }

    void open(const etl::string_view& file_path) {
        (void)file_path;
    }

    void write(const etl::string_view& file_path, const etl::span<etl::byte>& data) {
        (void)file_path;
        (void)data;
    }

    void read(const etl::string_view& file_path, etl::span<etl::byte>& data) {
        (void)file_path;
        (void)data;
    }

    void close(const etl::string_view& file_path) {
        (void)file_path;
    }

    void remove(const etl::string_view& file_path) {
        (void)file_path;
    }

} // namespace file

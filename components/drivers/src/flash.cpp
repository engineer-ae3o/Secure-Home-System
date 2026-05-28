#include "lfs.h"

#include "flash.hpp"
#include "utils.hpp"

#include <utility>
#include <string_view>

extern "C" {
    // Defined in linker script
    extern uint32_t lfs_start;
    extern uint32_t lfs_end;
}

namespace file {

    [[maybe_unused]] static constexpr auto LFS_PART_START = &lfs_start;
    [[maybe_unused]] static constexpr auto LFS_PART_END   = &lfs_end;

    // Flash programming and read sizes
    static constexpr uint8_t READ_SIZE_BYTES{8};
    static constexpr uint8_t PROG_SIZE_BYTES{2};

    // Flash block details
    static constexpr uint8_t  BLOCK_COUNT{32};
    static constexpr uint16_t BLOCK_SIZE_BYTES{1024};
    static constexpr uint32_t BLOCK_CYCLES{10'000};

    // Cache and lookahead sizes
    static constexpr uint8_t LOOKAHEAD_SIZE_BYTES{BLOCK_COUNT / 8};
    static constexpr uint8_t CACHE_SIZE_BYTES{BLOCK_SIZE_BYTES / 8};

    // File name and max file number limit
    static constexpr uint8_t MAX_NAME_LEN{8};
    static constexpr uint8_t MAX_FILE_SIZE_BYTES{128};

    // LittleFS buffers
    static std::array<uint8_t, CACHE_SIZE_BYTES>     s_read_buffer{};
    static std::array<uint8_t, CACHE_SIZE_BYTES>     s_prog_buffer{};
    static std::array<uint8_t, LOOKAHEAD_SIZE_BYTES> s_lookahead_buffer{};

    // LittleFS handle
    static lfs_t s_lfs_handle{};

    // File data
    struct file_t {
        lfs_file_t       file{};
        std::string_view file_path;
        lfs_file_config  file_config{};
    };

    // File caches
    static std::array<std::array<uint8_t, CACHE_SIZE_BYTES>, std::to_underlying(name_t::COUNT)> s_file_cache{};

    // Lookup table for the files being used
    static std::array<file_t, std::to_underlying(name_t::COUNT)> s_file_lut = {{
        // Add a little bit of obfuscation to the file names since they get stored directly in
        // flash. Besides, they will be accessed with their more readable enum counterparts.
        // Doesn't do a whole lot in the grand scheme of things, but still, doesn't hurt.
        [std::to_underlying(name_t::COUNTER)] =
            {
                .file      = {},
                .file_path = "fchdvqv",
                .file_config =
                    {
                        .buffer     = s_file_cache[std::to_underlying(name_t::COUNTER)].data(),
                        .attrs      = nullptr,
                        .attr_count = 0,
                    },
            },
        [std::to_underlying(name_t::PASSWORD)] =
            {
                .file      = {},
                .file_path = "yacnywo",
                .file_config =
                    {
                        .buffer     = s_file_cache[std::to_underlying(name_t::PASSWORD)].data(),
                        .attrs      = nullptr,
                        .attr_count = 0,
                    },
            },
        [std::to_underlying(name_t::PNUMBERS)] =
            {
                .file      = {},
                .file_path = "cqwogto",
                .file_config =
                    {
                        .buffer     = s_file_cache[std::to_underlying(name_t::PNUMBERS)].data(),
                        .attrs      = nullptr,
                        .attr_count = 0,
                    },
            },
    }};

    // Counter in flash counting number of boot cycles
    // Is part of what is used to seed the RNG
    static uint32_t s_boot_cycle_counter{};

    void init() {
        // Configuration data for the file system
        static constexpr lfs_config s_lfsconfig = {
            // Not needed
            .context = nullptr,

            // File read and write ops
            .read =
                [](const lfs_config* config, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
                    (void)config;
                    (void)block;
                    (void)off;
                    (void)buffer;
                    (void)size;
                    return 0;
                },
            .prog =
                [](const lfs_config* config, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) {
                    (void)config;
                    (void)block;
                    (void)off;
                    (void)buffer;
                    (void)size;
                    return 0;
                },
            .erase =
                [](const lfs_config* config, lfs_block_t block) {
                    (void)config;
                    (void)block;
                    return 0;
                },
            .sync =
                [](const lfs_config* config) {
                    (void)config;
                    return 0;
                },

            // Read and programming sizes
            .read_size = READ_SIZE_BYTES,
            .prog_size = PROG_SIZE_BYTES,

            // Flash block details
            .block_size   = BLOCK_SIZE_BYTES,
            .block_count  = BLOCK_COUNT,
            .block_cycles = BLOCK_CYCLES,

            // Cache and lookahead sizes
            .cache_size     = CACHE_SIZE_BYTES,
            .lookahead_size = LOOKAHEAD_SIZE_BYTES,
            .compact_thresh = 0,

            // Statically provided buffers
            .read_buffer      = s_read_buffer.data(),
            .prog_buffer      = s_prog_buffer.data(),
            .lookahead_buffer = s_lookahead_buffer.data(),

            // Limits
            .name_max     = MAX_NAME_LEN,
            .file_max     = MAX_FILE_SIZE_BYTES,
            .attr_max     = LFS_ATTR_MAX,
            .metadata_max = BLOCK_SIZE_BYTES,
            .inline_max   = BLOCK_SIZE_BYTES / 8,
        };

        // Setup filesystem
        auto ret = lfs_mount(&s_lfs_handle, &s_lfsconfig);
        if (ret < 0) {
            // This error should happen only once, at first boot
            lfs_format(&s_lfs_handle, &s_lfsconfig);
            lfs_mount(&s_lfs_handle, &s_lfsconfig);
        }

        // Open counter file. Create it if it doesn't yet exist
        ret = lfs_file_opencfg(&s_lfs_handle,
                               &s_file_lut[std::to_underlying(name_t::COUNTER)].file,
                               s_file_lut[std::to_underlying(name_t::COUNTER)].file_path.data(),
                               (LFS_O_CREAT | LFS_O_RDWR),
                               &s_file_lut[std::to_underlying(name_t::COUNTER)].file_config);
        if (ret < 0) {
            utils::assert_check(false);
        }

        // Read counter from file
        ret = lfs_file_read(
            &s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file, &s_boot_cycle_counter, sizeof(s_boot_cycle_counter));
        if (ret < 0) {
            utils::assert_check(false);
        }

        // Rewind file pointer back to starting position
        ret = lfs_file_rewind(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file);
        if (ret < 0) {
            utils::assert_check(false);
        }

        // Increment counter and write back to flash
        s_boot_cycle_counter++;
        ret = lfs_file_write(
            &s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file, &s_boot_cycle_counter, sizeof(s_boot_cycle_counter));
        if (ret < 0) {
            utils::assert_check(false);
        }

        // Close the counter file
        ret = lfs_file_close(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file);
        if (ret < 0) {
            utils::assert_check(false);
        }

        // Open password and phone number files. Create if it doesn't yet exist
        // Pnumbers file
        ret = lfs_file_opencfg(&s_lfs_handle,
                               &s_file_lut[std::to_underlying(name_t::PNUMBERS)].file,
                               s_file_lut[std::to_underlying(name_t::PNUMBERS)].file_path.data(),
                               (LFS_O_CREAT | LFS_O_RDWR),
                               &s_file_lut[std::to_underlying(name_t::PNUMBERS)].file_config);
        if (ret < 0) {
            utils::assert_check(false);
        }
        // Password file
        ret = lfs_file_opencfg(&s_lfs_handle,
                               &s_file_lut[std::to_underlying(name_t::PASSWORD)].file,
                               s_file_lut[std::to_underlying(name_t::PASSWORD)].file_path.data(),
                               (LFS_O_CREAT | LFS_O_RDWR),
                               &s_file_lut[std::to_underlying(name_t::PASSWORD)].file_config);
        if (ret < 0) {
            utils::assert_check(false);
        }
    }

    void deinit() {
        // Close both files before unmounting file system
        auto ret = lfs_file_close(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::PASSWORD)].file);
        if (ret < 0) {
            utils::assert_check(false);
        }

        ret = lfs_file_close(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::PNUMBERS)].file);
        if (ret < 0) {
            utils::assert_check(false);
        }

        ret = lfs_unmount(&s_lfs_handle);
        if (ret < 0) {
            utils::assert_check(false);
        }
    }

    uint32_t get_count_value() {
        return s_boot_cycle_counter;
    }

    void write(name_t file, const std::span<uint8_t>& data) {
        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            utils::assert_check(false);
        }

        (void)file;
        (void)data;
    }

    void read(name_t file, std::span<uint8_t>& data) {
        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            utils::assert_check(false);
        }

        (void)file;
        (void)data;
    }

    void sync(name_t file) {
        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            utils::assert_check(false);
        }

        auto ret = lfs_file_sync(&s_lfs_handle, &s_file_lut[std::to_underlying(file)].file);
        if (ret < 0) {
            utils::assert_check(false);
        }
    }

} // namespace file

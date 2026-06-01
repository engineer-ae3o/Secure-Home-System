#include "stm32f1xx_hal.h"

#include "flash.hpp"
#include "utils.hpp"

#include "lfs.h"

#include <utility>
#include <string_view>

extern "C" {
    // Defined in linker script
    extern uint32_t lfs_start;
}

namespace file {

    namespace {
        const uint32_t LFS_PARTITION_START = reinterpret_cast<uint32_t>(&lfs_start);

        // Flash programming and read sizes
        constexpr uint32_t MIN_READ_SIZE_BYTES{1};
        constexpr uint32_t PROG_SIZE_BYTES{2};

        // Flash block details
        constexpr uint32_t BLOCK_COUNT{32};
        constexpr uint32_t BLOCK_SIZE_BYTES{1024};
        constexpr uint32_t BLOCK_CYCLES{10'000};

        // File name and max file number limit
        constexpr uint32_t MAX_NAME_LEN{8};
        constexpr uint32_t MAX_FILE_SIZE_BYTES{4096};

        // Cache and lookahead sizes
        constexpr uint32_t LOOKAHEAD_SIZE_BYTES{8};
        constexpr uint32_t CACHE_SIZE_BYTES{BLOCK_SIZE_BYTES / 8};

        // LittleFS buffers
        std::array<uint8_t, CACHE_SIZE_BYTES>     s_read_buffer{};
        std::array<uint8_t, CACHE_SIZE_BYTES>     s_prog_buffer{};
        std::array<uint8_t, LOOKAHEAD_SIZE_BYTES> s_lookahead_buffer{};

        // LittleFS handle
        lfs_t s_lfs_handle{};

        // File data
        struct file_t {
            lfs_file_t       file{};
            std::string_view file_path;
            lfs_file_config  file_config{};
        };

        // File caches
        std::array<std::array<uint8_t, CACHE_SIZE_BYTES>, std::to_underlying(name_t::COUNT)> s_file_cache{};

        // Lookup table for the files being used
        std::array<file_t, std::to_underlying(name_t::COUNT)> s_file_lut = {{
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
        uint32_t s_boot_cycle_counter{};

        // Helper
        uint32_t page_idx_to_phy_addr(uint32_t idx) {
            return LFS_PARTITION_START + (idx * BLOCK_SIZE_BYTES);
        }
    } // namespace

    // Public API
    void init() {
        // Configuration data for the file system
        // Has to have static duration since LittleFS needs to be able to access it at all times
        static const lfs_config s_lfs_config = {
            // Not needed
            .context = nullptr,

            // File read, prog and erase ops
            .read =
                [](const lfs_config* config, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
                    (void)config;
                    // Get physical block address and offset into the block
                    const auto* phy_addr = reinterpret_cast<const void*>(page_idx_to_phy_addr(block) + off);
                    memcpy(buffer, phy_addr, size);
                    return 0;
                },
            .prog =
                [](const lfs_config* config, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) {
                    // Make sure data to write to flash is the a multiple of PROG_SIZE_BYTES
                    utils::assert_check((size % PROG_SIZE_BYTES) == 0);

                    (void)config;
                    // Must unlock the flash controller's control register before writing to the flash
                    HAL_FLASH_Unlock();

                    // Cast `buffer` to pointer to `uint16_t` since we'll be moving data in that size
                    const auto* buf = static_cast<const uint16_t*>(buffer);

                    // Get physical block address and offset into the block
                    auto phy_addr = page_idx_to_phy_addr(block) + off;

                    const size_t element_count = size / PROG_SIZE_BYTES;
                    int          rc{};

                    for (size_t i{}; i < element_count; i++, phy_addr += PROG_SIZE_BYTES) {
                        auto ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, phy_addr, buf[i]);
                        if (ret != HAL_OK) {
                            __HAL_FLASH_CLEAR_FLAG(HAL_FLASH_ERROR_PROG | HAL_FLASH_ERROR_WRP);
                            rc = LFS_ERR_CORRUPT;
                            break;
                        }
                    }

                    // Lock the flash controller's control register to prevent accidental writes to the flash
                    HAL_FLASH_Lock();
                    return rc;
                },
            .erase =
                [](const lfs_config* config, lfs_block_t block) {
                    (void)config;
                    // Must unlock the flash controller's control register before writing to the flash
                    HAL_FLASH_Unlock();

                    FLASH_EraseInitTypeDef erase = {
                        .TypeErase   = FLASH_TYPEERASE_PAGES,
                        .Banks       = FLASH_BANK_1,
                        .PageAddress = page_idx_to_phy_addr(block), // Get physical block address
                        .NbPages     = 1,
                    };
                    uint32_t page_error{};
                    int      rc{};

                    auto ret = HAL_FLASHEx_Erase(&erase, &page_error);
                    if (ret != HAL_OK || page_error != 0xFFFFFFFFU) {
                        __HAL_FLASH_CLEAR_FLAG(HAL_FLASH_ERROR_PROG | HAL_FLASH_ERROR_WRP);
                        rc = LFS_ERR_CORRUPT;
                    }

                    // Lock the flash controller's control register to prevent accidental writes to the flash
                    HAL_FLASH_Lock();
                    return rc;
                },
            .sync =
                [](const lfs_config* config) {
                    (void)config;
                    return 0;
                },

            // Read and programming sizes
            .read_size = MIN_READ_SIZE_BYTES,
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
        auto ret = lfs_mount(&s_lfs_handle, &s_lfs_config);
        if (ret < 0) {
            // This error should happen only once, at first boot
            lfs_format(&s_lfs_handle, &s_lfs_config);
            lfs_mount(&s_lfs_handle, &s_lfs_config);
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

    uint32_t get_boot_cycle_count() {
        return s_boot_cycle_counter;
    }

    void write(name_t file, std::span<const uint8_t> data) {
        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            utils::assert_check(false);
        }

        (void)file;
        (void)data;
    }

    void read(name_t file, std::span<uint8_t> data) {
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

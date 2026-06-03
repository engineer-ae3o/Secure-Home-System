#include "stm32f1xx_hal.h"

#include "flash.hpp"
#include "utils.hpp"

#include "lfs.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <utility>
#include <string_view>

extern "C" {
    // Defined in linker script
    extern uint32_t lfs_start;
}

namespace file {

    namespace {
        const uint32_t LFS_PARTITION_START = reinterpret_cast<uint32_t>(&lfs_start);

        // Synchronization across multi threaded access to the flash storage.
        SemaphoreHandle_t s_task_mutex{};
        StaticSemaphore_t s_task_mutex_buffer{};

        // RAII helper for taking and freeing the mutex
        struct mutex_t {
        public:
            // Block forever till mutex is taken. This is because the flash storage is critical
            // for the system's operation, and if a task is waiting to access it, it's likely
            // that it needs to access it to make progress. So we might as well block indefinitely
            // until we can take the mutex.
            mutex_t() {
                xSemaphoreTake(s_task_mutex, portMAX_DELAY);
            }

            ~mutex_t() {
                xSemaphoreGive(s_task_mutex);
            }

            mutex_t(const mutex_t&)            = delete;
            mutex_t& operator=(const mutex_t&) = delete;
            mutex_t(mutex_t&&)                 = delete;
            mutex_t& operator=(mutex_t&&)      = delete;
        };

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
            [std::to_underlying(name_t::ASCON_SEED)] =
                {
                    .file      = {},
                    .file_path = "sgscjhw",
                    .file_config =
                        {
                            .buffer     = s_file_cache[std::to_underlying(name_t::ASCON_SEED)].data(),
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
            utils::assert_check(lfs_format(&s_lfs_handle, &s_lfs_config) > 0);
            utils::assert_check(lfs_mount(&s_lfs_handle, &s_lfs_config) > 0);
        }

        // Open counter file. Create it if it doesn't yet exist
        ret = lfs_file_opencfg(&s_lfs_handle,
                               &s_file_lut[std::to_underlying(name_t::COUNTER)].file,
                               s_file_lut[std::to_underlying(name_t::COUNTER)].file_path.data(),
                               (LFS_O_CREAT | LFS_O_RDWR),
                               &s_file_lut[std::to_underlying(name_t::COUNTER)].file_config);
        utils::assert_check(ret > 0);

        // Read counter from file
        ret = lfs_file_read(
            &s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file, &s_boot_cycle_counter, sizeof(s_boot_cycle_counter));
        utils::assert_check(ret > 0);

        // Rewind file pointer back to starting position
        ret = lfs_file_rewind(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file);
        utils::assert_check(ret > 0);

        // Increment counter and write back to flash
        // If the boot cycle counter is not zero, then we know that this
        // is not the first boot, so we can safely increment the counter.
        if (s_boot_cycle_counter != 0) {
            s_boot_cycle_counter++;
        }

        ret = lfs_file_write(
            &s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file, &s_boot_cycle_counter, sizeof(s_boot_cycle_counter));
        utils::assert_check(ret > 0);

        // Close the counter file
        ret = lfs_file_close(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file);
        utils::assert_check(ret > 0);

        // Open password and phone number files. Create if it doesn't yet exist
        // Pnumbers file
        ret = lfs_file_opencfg(&s_lfs_handle,
                               &s_file_lut[std::to_underlying(name_t::PNUMBERS)].file,
                               s_file_lut[std::to_underlying(name_t::PNUMBERS)].file_path.data(),
                               (LFS_O_CREAT | LFS_O_RDWR),
                               &s_file_lut[std::to_underlying(name_t::PNUMBERS)].file_config);
        utils::assert_check(ret > 0);

        // Password file
        ret = lfs_file_opencfg(&s_lfs_handle,
                               &s_file_lut[std::to_underlying(name_t::PASSWORD)].file,
                               s_file_lut[std::to_underlying(name_t::PASSWORD)].file_path.data(),
                               (LFS_O_CREAT | LFS_O_RDWR),
                               &s_file_lut[std::to_underlying(name_t::PASSWORD)].file_config);
        utils::assert_check(ret > 0);

        // Ascon seed file
        ret = lfs_file_opencfg(&s_lfs_handle,
                               &s_file_lut[std::to_underlying(name_t::ASCON_SEED)].file,
                               s_file_lut[std::to_underlying(name_t::ASCON_SEED)].file_path.data(),
                               (LFS_O_CREAT | LFS_O_RDWR),
                               &s_file_lut[std::to_underlying(name_t::ASCON_SEED)].file_config);
        utils::assert_check(ret > 0);

        // Create the mutex
        s_task_mutex = xSemaphoreCreateMutexStatic(&s_task_mutex_buffer);
    }

    void deinit() {
        {
            // Take the mutex to make sure no other thread is using the SIM800L while we are
            // deinitializing it. We have to wait for all other tasks to finish use of the mutex
            [[maybe_unused]] mutex_t mutex;

            // Close both files before unmounting file system
            auto ret = lfs_file_close(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::PASSWORD)].file);
            utils::assert_check(ret > 0);

            ret = lfs_file_close(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::PNUMBERS)].file);
            utils::assert_check(ret > 0);

            ret = lfs_file_close(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::ASCON_SEED)].file);
            utils::assert_check(ret > 0);

            ret = lfs_unmount(&s_lfs_handle);
            utils::assert_check(ret > 0);
        }

        // Unregister queue from queue registry if it was put there during creation by FreeRTOS
        vSemaphoreDelete(s_task_mutex);
        s_task_mutex        = {};
        s_task_mutex_buffer = {};
    }

    uint32_t get_boot_cycle_count() {
        return s_boot_cycle_counter;
    }

    void write(name_t file, std::span<const uint8_t> data) {
        // RAII handling for mutex acquisition and releasing
        [[maybe_unused]] mutex_t mutex;

        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            utils::assert_check(false);
        }

        auto ret = lfs_file_rewind(&s_lfs_handle, &s_file_lut[std::to_underlying(file)].file);
        utils::assert_check(ret > 0);

        ret = lfs_file_write(&s_lfs_handle, &s_file_lut[std::to_underlying(file)].file, data.data(), data.size());
        utils::assert_check(ret == static_cast<int>(data.size()));
    }

    void read(name_t file, std::span<uint8_t> data) {
        // RAII handling for mutex acquisition and releasing
        [[maybe_unused]] mutex_t mutex;

        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            utils::assert_check(false);
        }

        auto ret = lfs_file_rewind(&s_lfs_handle, &s_file_lut[std::to_underlying(file)].file);
        utils::assert_check(ret > 0);

        ret = lfs_file_read(&s_lfs_handle, &s_file_lut[std::to_underlying(file)].file, data.data(), data.size());
        utils::assert_check(ret == static_cast<int>(data.size()));
    }

    void sync(name_t file) {
        // RAII handling for mutex acquisition and releasing
        [[maybe_unused]] mutex_t mutex;

        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            utils::assert_check(false);
        }

        auto ret = lfs_file_sync(&s_lfs_handle, &s_file_lut[std::to_underlying(file)].file);
        utils::assert_check(ret > 0);
    }

} // namespace file

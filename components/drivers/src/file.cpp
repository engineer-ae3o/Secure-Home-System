#include "stm32f1xx_hal.h"

#include "file.hpp"
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
        bool  s_is_initialized{};

        // File data
        struct file_t {
            lfs_file_t       file{};
            std::string_view file_path;
            lfs_file_config  file_config{};
        };

        // File caches
        std::array<std::array<uint8_t, CACHE_SIZE_BYTES>, std::to_underlying(name_t::COUNT)>
            s_file_cache{};

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
                            .buffer = s_file_cache[std::to_underlying(name_t::ASCON_SEED)].data(),
                            .attrs  = nullptr,
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

        // Helper to avoid verbose error checking from LittleFS function calls
#define TRY_LFS(func, err)                                                                         \
    do {                                                                                           \
        auto ret_ = func;                                                                          \
        if (ret_ == LFS_ERR_CORRUPT) {                                                             \
            return utils::error_t::FILE_FS_CORRUPTED;                                              \
        } else if (ret_ < 0) {                                                                     \
            return err;                                                                            \
        }                                                                                          \
    } while (0)

    } // namespace

    // Public API
    utils::error_t init() {

        if (s_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        // Create the mutex
        s_task_mutex = xSemaphoreCreateMutexStatic(&s_task_mutex_buffer);

        // Configuration data for the file system
        // Has to have static duration since LittleFS needs to be able to access it at all times
        static const lfs_config s_lfs_config = {
            // Not needed
            .context = nullptr,

            // File read, prog and erase ops
            .read =
                [](const lfs_config* config,
                   lfs_block_t       block,
                   lfs_off_t         off,
                   void*             buffer,
                   lfs_size_t        size) {
                    (void)config;
                    // Get physical block address and offset into the block
                    const auto* phy_addr =
                        reinterpret_cast<const void*>(page_idx_to_phy_addr(block) + off);
                    memcpy(buffer, phy_addr, size);
                    return 0;
                },
            .prog =
                [](const lfs_config* config,
                   lfs_block_t       block,
                   lfs_off_t         off,
                   const void*       buffer,
                   lfs_size_t        size) {
                    // Make sure data to write to flash is the a multiple of PROG_SIZE_BYTES
                    if (size % PROG_SIZE_BYTES != 0) {
                        return static_cast<int>(LFS_ERR_INVAL);
                    }

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
                            __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGERR);
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
                        // Clear flash error flag
                        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGERR);
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
        bool first_boot{};
        auto ret = lfs_mount(&s_lfs_handle, &s_lfs_config);
        if (ret < 0) {
            // This error should happen only once, at first boot
            TRY_LFS(lfs_format(&s_lfs_handle, &s_lfs_config),
                    utils::error_t::FILE_FS_FAILED_TO_FORMAT);

            TRY_LFS(lfs_mount(&s_lfs_handle, &s_lfs_config),
                    utils::error_t::FILE_FS_FAILED_TO_MOUNT);
            first_boot = true;
        }

        // Open counter file. Create it if it doesn't yet exist
        TRY_LFS(lfs_file_opencfg(&s_lfs_handle,
                                 &s_file_lut[std::to_underlying(name_t::COUNTER)].file,
                                 s_file_lut[std::to_underlying(name_t::COUNTER)].file_path.data(),
                                 (LFS_O_CREAT | LFS_O_RDWR),
                                 &s_file_lut[std::to_underlying(name_t::COUNTER)].file_config),
                utils::error_t::FILE_FAILED_TO_OPEN);

        // Read counter from file
        ret = lfs_file_read(&s_lfs_handle,
                            &s_file_lut[std::to_underlying(name_t::COUNTER)].file,
                            &s_boot_cycle_counter,
                            sizeof(s_boot_cycle_counter));
        if (ret != sizeof(s_boot_cycle_counter)) {
            return utils::error_t::FILE_FAILED_TO_READ;
        }

        // Rewind file pointer back to starting position
        TRY_LFS(
            lfs_file_rewind(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file),
            utils::error_t::FILE_FAILED_TO_SEEK);

        // Increment counter and write back to flash
        // If the boot cycle counter is not zero, then we know that this
        // is not the first boot, so we can safely increment the counter.
        if (!first_boot) [[likely]] {
            s_boot_cycle_counter++;
        }

        ret = lfs_file_write(&s_lfs_handle,
                             &s_file_lut[std::to_underlying(name_t::COUNTER)].file,
                             &s_boot_cycle_counter,
                             sizeof(s_boot_cycle_counter));
        if (ret != sizeof(s_boot_cycle_counter)) {
            return utils::error_t::FILE_FAILED_TO_READ;
        }

        // Close the counter file
        TRY_LFS(
            lfs_file_close(&s_lfs_handle, &s_file_lut[std::to_underlying(name_t::COUNTER)].file),
            utils::error_t::FILE_FAILED_TO_CLOSE);

        // Open password and phone number files. Create if it doesn't yet exist
        // Pnumbers file
        TRY_LFS(lfs_file_opencfg(&s_lfs_handle,
                                 &s_file_lut[std::to_underlying(name_t::PNUMBERS)].file,
                                 s_file_lut[std::to_underlying(name_t::PNUMBERS)].file_path.data(),
                                 (LFS_O_CREAT | LFS_O_RDWR),
                                 &s_file_lut[std::to_underlying(name_t::PNUMBERS)].file_config),
                utils::error_t::FILE_FAILED_TO_OPEN);

        // Password file
        TRY_LFS(lfs_file_opencfg(&s_lfs_handle,
                                 &s_file_lut[std::to_underlying(name_t::PASSWORD)].file,
                                 s_file_lut[std::to_underlying(name_t::PASSWORD)].file_path.data(),
                                 (LFS_O_CREAT | LFS_O_RDWR),
                                 &s_file_lut[std::to_underlying(name_t::PASSWORD)].file_config),
                utils::error_t::FILE_FAILED_TO_OPEN);

        // Ascon seed file
        TRY_LFS(
            lfs_file_opencfg(&s_lfs_handle,
                             &s_file_lut[std::to_underlying(name_t::ASCON_SEED)].file,
                             s_file_lut[std::to_underlying(name_t::ASCON_SEED)].file_path.data(),
                             (LFS_O_CREAT | LFS_O_RDWR),
                             &s_file_lut[std::to_underlying(name_t::ASCON_SEED)].file_config),
            utils::error_t::FILE_FAILED_TO_OPEN);

        s_is_initialized = true;

        return utils::error_t::NONE;
    }

    utils::error_t deinit() {
        {
            // Take the mutex to make sure no other thread is using any of the files while we are
            // deinitializing it. We have to wait for all other tasks to finish use of the mutex
            [[maybe_unused]] mutex_t mutex;

            if (!s_is_initialized) {
                return utils::error_t::ERR_INVALID_STATE;
            }

            // Close both files before unmounting file system
            TRY_LFS(lfs_file_close(&s_lfs_handle,
                                   &s_file_lut[std::to_underlying(name_t::PASSWORD)].file),
                    utils::error_t::FILE_FAILED_TO_CLOSE);

            TRY_LFS(lfs_file_close(&s_lfs_handle,
                                   &s_file_lut[std::to_underlying(name_t::PNUMBERS)].file);
                    , utils::error_t::FILE_FAILED_TO_CLOSE);

            TRY_LFS(lfs_file_close(&s_lfs_handle,
                                   &s_file_lut[std::to_underlying(name_t::ASCON_SEED)].file),
                    utils::error_t::FILE_FAILED_TO_CLOSE);

            TRY_LFS(lfs_unmount(&s_lfs_handle), utils::error_t::FILE_FS_FAILED_TO_UNMOUNT);
        }

        // Unregister queue from queue registry if it was put there during creation by FreeRTOS
        vSemaphoreDelete(s_task_mutex);
        s_task_mutex        = {};
        s_task_mutex_buffer = {};

        s_is_initialized = false;

        return utils::error_t::NONE;
    }

    std::expected<uint32_t, utils::error_t> get_boot_cycle_count() {
        if (!s_is_initialized) {
            return std::unexpected(utils::error_t::ERR_INVALID_STATE);
        }
        return s_boot_cycle_counter;
    }

    utils::error_t write(name_t file, std::span<const uint8_t> data, uint32_t byte_offset) {
        // RAII handling for mutex acquisition and releasing
        [[maybe_unused]] mutex_t mutex;

        if (!s_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            return utils::error_t::ERR_INVALID_ARG;
        }

        TRY_LFS(lfs_file_seek(&s_lfs_handle,
                              &s_file_lut[std::to_underlying(file)].file,
                              static_cast<int>(byte_offset),
                              LFS_SEEK_SET),
                utils::error_t::FILE_FAILED_TO_SEEK);

        auto ret = lfs_file_write(
            &s_lfs_handle, &s_file_lut[std::to_underlying(file)].file, data.data(), data.size());
        if (ret != static_cast<int>(data.size())) {
            return utils::error_t::FILE_FAILED_TO_WRITE;
        }

        return utils::error_t::NONE;
    }

    utils::error_t read(name_t file, std::span<uint8_t> data, uint32_t byte_offset) {
        // RAII handling for mutex acquisition and releasing
        [[maybe_unused]] mutex_t mutex;

        if (!s_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            return utils::error_t::ERR_INVALID_ARG;
        }

        TRY_LFS(lfs_file_seek(&s_lfs_handle,
                              &s_file_lut[std::to_underlying(file)].file,
                              static_cast<int>(byte_offset),
                              LFS_SEEK_SET),
                utils::error_t::FILE_FAILED_TO_SEEK);

        auto ret = lfs_file_read(
            &s_lfs_handle, &s_file_lut[std::to_underlying(file)].file, data.data(), data.size());
        if (ret != static_cast<int>(data.size())) {
            return utils::error_t::FILE_FAILED_TO_READ;
        }

        return utils::error_t::NONE;
    }

    utils::error_t sync(name_t file) {
        // RAII handling for mutex acquisition and releasing
        [[maybe_unused]] mutex_t mutex;

        if (!s_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        // The counter file is not to be accessed during normal operation
        if (file == name_t::COUNTER || file == name_t::COUNT) {
            return utils::error_t::ERR_INVALID_ARG;
        }

        TRY_LFS(lfs_file_sync(&s_lfs_handle, &s_file_lut[std::to_underlying(file)].file),
                utils::error_t::FILE_FAILED_TO_SYNC);

        return utils::error_t::NONE;
    }

} // namespace file

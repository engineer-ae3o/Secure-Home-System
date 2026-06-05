#pragma once

#include "utils.hpp"

#include <span>
#include <cstdint>
#include <expected>

namespace file {

    // Flash programming and read sizes
    constexpr uint32_t MIN_READ_SIZE_BYTES{1};
    constexpr uint32_t PROG_SIZE_BYTES{2};

    // Flash block details
    constexpr uint32_t BLOCK_COUNT{32};
    constexpr uint32_t BLOCK_SIZE_BYTES{1024};
    constexpr uint32_t BLOCK_CYCLES{5'000};

    // File name and max file number limit
    constexpr uint32_t MAX_NAME_LEN{8};
    constexpr uint32_t MAX_FILE_SIZE_BYTES{4096};

    // File identifiers for the different files being used
    enum class name_t : uint8_t {
        COUNTER,
        PASSWORD,
        PNUMBERS,
        ASCON_SEED,
        COUNT,
    };

    /**
     * @brief Initializes the underlying storage peripheral and file system metadata, as well
     *        as open the boot cycle counter file, updates it, closes it, and then opens the
     *        password and phone number files.
     * 
     * @note This function is not thread safe and must be called before performing any 
     *       file operations.
     */
    utils::error_t init();

    /**
     * @brief Closes password and phone number files, flushes any unwritten data to the non
     *        volatile storage, and unmounts the file system.
     * 
     * @note This function is not thread safe. Failing to call this before power-off or reset 
     *       may result in data corruption for unsynced files.
     */
    utils::error_t deinit();

    /**
     * @brief Gets the boot cycle count. Pretty straightforward.
     * 
     * @return The boot cycle count as stored in flash. This is incremented on every
     *         boot and is part of the entropy pool for the RNG seeding.
     */
    [[nodiscard]] std::expected<uint32_t, utils::error_t> get_boot_cycle_count();

    /**
     * @brief Writes raw binary data from the provided span buffer into the specified file.
     * 
     * @param[in] file Target file identifier where the data will be written.
     * @param[in] data Buffer containing data to be written to the file.
     */
    utils::error_t write(name_t file, std::span<const uint8_t> data, uint32_t byte_offset = 0);

    /**
     * @brief Reads raw binary data from the specified file directly into the provided destination span.
     * 
     * @param[in]  file Target file identifier to read the data from.
     * @param[out] data View over the pre-allocated contiguous memory buffer where the 
     *                  fetched bytes will be stored.
     */
    utils::error_t read(name_t file, std::span<uint8_t> data, uint32_t byte_offset = 0);

    /**
     * @brief Forces any pending cached or buffered write data associated with the specified file 
     *        to be committed immediately to non-volatile physical storage.
     * 
     * @param[in] file Target file identifier to flush and synchronize.
     */
    utils::error_t sync(name_t file);

} // namespace file

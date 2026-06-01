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
     * @brief Initializes the underlying storage peripheral and file system metadata, as well
     *        as open the boot cycle counter file, updates it, closes it, and then opens the
     *        password and phone number files.
     * 
     * @note This function is not thread safe and must be called before performing any 
     *       file operations.
     */
    void init();

    /**
     * @brief Closes password and phone number files, flushes any unwritten data to the non
     *        volatile storage, and unmounts the file system.
     * 
     * @note This function is not thread safe. Failing to call this before power-off or reset 
     *       may result in data corruption for unsynced files.
     */
    void deinit();

    /**
     * @brief Gets the boot cycle count. Pretty straightforward.
     * 
     * @return The boot cycle count as stored in flash. This is incremented on every
     *         boot and is part of the entropy pool for the RNG seeding.
     */
    uint32_t get_boot_cycle_count();

    /**
     * @brief Writes raw binary data from the provided span buffer into the specified file.
     * 
     * @param[in] file Target file identifier where the data will be written.
     * @param[in] data Buffer conataining data to be written to the file. The size of the span
     *                 determines how many bytes will be written in a single operation.
     */
    void write(name_t file, std::span<const uint8_t> data);

    /**
     * @brief Reads raw binary data from the specified file directly into the provided destination span.
     *        The operation attempts to populate the span completely based on its current size.
     * 
     * @param[in]  file Target file identifier to read the data from.
     * @param[out] data View over the pre-allocated contiguous memory buffer where the 
     *                  fetched bytes will be stored.
     */
    void read(name_t file, std::span<uint8_t> data);

    /**
     * @brief Forces any pending cached or buffered write data associated with the specified file 
     *        to be committed immediately to non-volatile physical storage.
     * 
     * @param[in] file Target file identifier to flush and synchronize.
     */
    void sync(name_t file);

} // namespace file

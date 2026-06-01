#include "stm32f1xx_hal.h"

#include "random.hpp"
#include "config.hpp"
#include "flash.hpp"
#include "utils.hpp"

#include "ascon/random.h"

namespace rnd {

    namespace {
        // The Random Number Generator being used. Only one instance is needed.
        ascon_random_state_t s_random_state{};

        // ASCON storage interface for saving and loading the seed from non-volatile storage
        constexpr ascon_storage_t s_ascon_storage = {
            .page_size      = file::PROG_SIZE_BYTES,
            .erase_size     = file::BLOCK_SIZE_BYTES,
            .address        = 0, // Not used since our read/write functions ignore the offset and always operate at offset zero
            .size           = file::BLOCK_SIZE_BYTES,
            .partial_writes = 0,
            .read =
                [](const ascon_storage_t* storage, size_t offset, unsigned char* data, size_t size) {
                    (void)storage;
                    (void)offset;

                    // We read the file at offset zero because `ascon_random_load_seed()` loads data at
                    // offset zero of the storage region, and the file size is exactly the same as the
                    // seed size, so we can be sure that the entire seed is read in one operation.
                    file::read(file::name_t::ASCON_SEED, {data, size});

                    return static_cast<int>(size);
                },
            .write =
                [](const ascon_storage_t* storage, size_t offset, const unsigned char* data, size_t size, int flags) {
                    (void)storage;
                    (void)offset;
                    (void)flags;

                    // We write the file at offset zero because `ascon_random_save_seed()` saves data at
                    // offset zero of the storage region, and the file size is exactly the same as the
                    // seed size, so we can be sure that the entire seed is written in one operation.
                    file::write(file::name_t::ASCON_SEED, {data, size});
                    file::sync(file::name_t::ASCON_SEED);

                    return static_cast<int>(size);
                },
        };

        uint32_t get_entropy_mixture();
    } // namespace

    // Public API
    void init() {

        // FInally, initialize the ASCON random state and load the seed from flash storage
        utils::assert_check(ascon_random_init(&s_random_state) != 0);
        utils::assert_check(ascon_random_load_seed(&s_random_state, &s_ascon_storage) == 0);
    }

    void get_random_numbers(std::span<uint8_t> buffer) {
        ascon_random_fetch(&s_random_state, buffer.data(), buffer.size());
    }

    void update_rng_state() {
        utils::assert_check(ascon_random_save_seed(&s_random_state, &s_ascon_storage) == 0);
    }

    namespace {
        uint32_t get_entropy_mixture() {
            return 0;
        }
    } // namespace

} // namespace rnd

extern "C" {
    // Used by ASCON's TRNG implementation to get random bytes
    int ascon_trng_get_bytes(unsigned char* out, size_t outlen) {
        for (size_t i{0}; i < outlen; i++) {
            out[i] = static_cast<unsigned char>(rnd::get_entropy_mixture() & 0xFF);
        }
        return 1;
    }
} // extern "C"

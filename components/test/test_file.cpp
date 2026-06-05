#include "utils.hpp"
#include "file.hpp"

#include <array>
#include <string_view>

namespace file {

    namespace {
        constexpr std::string_view SEED     = "gwdegjkbysfbdyce";
        constexpr std::string_view PASSWORD = "ehjfacrhdfshvmsn";
        constexpr std::string_view PNUMBERS = "08012345678";
    } // namespace

    void test_init() {
        auto ret = init();
        utils::assert_check(ret == utils::error_t::NONE);

        // Double init should fail
        ret = init();
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);
    }

    void test_deinit() {
        auto ret = deinit();
        utils::assert_check(ret == utils::error_t::NONE);

        // Double deinit should fail
        ret = deinit();
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);
    }

    void test_get_boot_count() {
        auto ret = get_boot_cycle_count();
        utils::assert_check(ret.has_value());
        (void)ret.value(); // Load value and discard
    }

    void test_file_write() {
        // Test file writing across the three files being used
        // File for ASCON seed storage
        auto ret =
            write(name_t::ASCON_SEED, {reinterpret_cast<const uint8_t*>(SEED.data()), SEED.size()});
        utils::assert_check(ret == utils::error_t::NONE);

        // File for storage of the hashed password
        ret = write(name_t::PASSWORD,
                    {reinterpret_cast<const uint8_t*>(PASSWORD.data()), PASSWORD.size()});
        utils::assert_check(ret == utils::error_t::NONE);

        // File for storage of the encrypted phone numbers
        ret = write(name_t::PNUMBERS,
                    {reinterpret_cast<const uint8_t*>(PNUMBERS.data()), PNUMBERS.size()});
        utils::assert_check(ret == utils::error_t::NONE);

        // Should return an error since the COUNTER file is not accessible at runtime
        ret = write(name_t::COUNTER, {});
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);

        // Should return an error since COUNT is not a valid file
        ret = write(name_t::COUNT, {});
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);

        // Hammer the file system with multiple writes
        for (uint8_t i{}; i < 50; i++) {
            // Ascon seed file
            ret = write(name_t::ASCON_SEED,
                        {reinterpret_cast<const uint8_t*>(SEED.data()), SEED.size()});
            utils::assert_check(ret == utils::error_t::NONE);

            // Password file
            ret = write(name_t::PASSWORD,
                        {reinterpret_cast<const uint8_t*>(PASSWORD.data()), PASSWORD.size()});
            utils::assert_check(ret == utils::error_t::NONE);

            // Phone numbers file
            ret = write(name_t::PNUMBERS,
                        {reinterpret_cast<const uint8_t*>(PNUMBERS.data()), PNUMBERS.size()});
            utils::assert_check(ret == utils::error_t::NONE);
        }
    }

    void test_file_read() {
        // Store the read data
        std::array<uint8_t, SEED.size()>     seed_buf{};
        std::array<uint8_t, PASSWORD.size()> password_buf{};
        std::array<uint8_t, PNUMBERS.size()> pnumber_buf{};

        // File for ASCON seed storage
        auto ret = read(name_t::ASCON_SEED, seed_buf);
        utils::assert_check(ret == utils::error_t::NONE);

        // File for storage of the hashed password
        ret = read(name_t::PASSWORD, password_buf);
        utils::assert_check(ret == utils::error_t::NONE);

        // File for storage of the encrypted phone numbers
        ret = read(name_t::PNUMBERS, pnumber_buf);
        utils::assert_check(ret == utils::error_t::NONE);

        // Ensure the read file contains what we wrote
        auto seed =
            std::string_view{reinterpret_cast<const char*>(seed_buf.data()), seed_buf.size()};
        auto password = std::string_view{reinterpret_cast<const char*>(password_buf.data()),
                                         password_buf.size()};
        auto pnumber =
            std::string_view{reinterpret_cast<const char*>(pnumber_buf.data()), pnumber_buf.size()};

        utils::assert_check(seed == SEED);
        utils::assert_check(password == PASSWORD);
        utils::assert_check(pnumber == PNUMBERS);

        // Should return an error since the COUNTER file is not accessible at runtime
        ret = read(name_t::COUNTER, {});
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);

        // Should return an error since COUNT is not a valid file
        ret = read(name_t::COUNT, {});
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);
    }

    void test_file_sync() {
        // File for ASCON seed storage
        auto ret = sync(name_t::ASCON_SEED);
        utils::assert_check(ret == utils::error_t::NONE);

        // File for storage of the hashed password
        ret = sync(name_t::PASSWORD);
        utils::assert_check(ret == utils::error_t::NONE);

        // File for storage of the encrypted phone numbers
        ret = sync(name_t::PNUMBERS);
        utils::assert_check(ret == utils::error_t::NONE);

        // Should return an error since the COUNTER file is not accessible at runtime
        ret = sync(name_t::COUNTER);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);

        // Should return an error since COUNT is not a valid file
        ret = sync(name_t::COUNT);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);
    }

    void test_uninit_guards() {
        // All public functions should reject calls before init
        auto ret = sync(name_t::ASCON_SEED);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);

        auto rc = get_boot_cycle_count();
        utils::assert_check(rc.error() == utils::error_t::ERR_INVALID_STATE);

        ret = deinit();
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);

        ret =
            write(name_t::ASCON_SEED, {reinterpret_cast<const uint8_t*>(SEED.data()), SEED.size()});
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);

        std::array<uint8_t, SEED.size()> buf{};
        ret = read(name_t::PASSWORD, buf);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);
    }

    void test_all() {
        test_uninit_guards();
        test_init();
        test_get_boot_count();
        test_file_write();
        test_file_sync();
        test_file_read();
        test_deinit();
        test_uninit_guards();
    }

} // namespace file

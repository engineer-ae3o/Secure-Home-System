extern "C" {
#include "unity.h"
}

#include "file.hpp"
#include "utils.hpp"

#include <array>
#include <string_view>

namespace file_test {

    namespace {
        constexpr std::string_view SEED     = "gwdegjkbysfbdyce";
        constexpr std::string_view PASSWORD = "ehjfacrhdfshvmsn";
        constexpr std::string_view PNUMBERS = "08012345678";
    } // namespace

    void test_uninit_guards() {
        auto ret = file::sync(file::name_t::ASCON_SEED);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        auto rc = file::get_boot_cycle_count();
        TEST_ASSERT_FALSE(rc.has_value());
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, rc.error());

        ret = file::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = file::write(file::name_t::ASCON_SEED,
                          {reinterpret_cast<const uint8_t*>(SEED.data()), SEED.size()});
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        std::array<uint8_t, SEED.size()> buf{};
        ret = file::read(file::name_t::PASSWORD, buf);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void test_init() {
        auto ret = file::init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init should fail
        ret = file::init();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void test_deinit() {
        auto ret = file::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit should fail
        ret = file::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void test_get_boot_count() {
        auto ret = file::get_boot_cycle_count();
        TEST_ASSERT_TRUE(ret.has_value());
        (void)ret.value();
    }

    void test_file_write() {
        // File for ASCON seed storage
        auto ret = file::write(file::name_t::ASCON_SEED,
                               {reinterpret_cast<const uint8_t*>(SEED.data()), SEED.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // File for storage of the hashed password
        ret = file::write(file::name_t::PASSWORD,
                          {reinterpret_cast<const uint8_t*>(PASSWORD.data()), PASSWORD.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // File for storage of the encrypted phone numbers
        ret = file::write(file::name_t::PNUMBERS,
                          {reinterpret_cast<const uint8_t*>(PNUMBERS.data()), PNUMBERS.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Should return an error since the COUNTER file is not accessible at runtime
        ret = file::write(file::name_t::COUNTER, {});
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Should return an error since COUNT is not a valid file
        ret = file::write(file::name_t::COUNT, {});
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Hammer the file system with multiple writes
        for (uint8_t i{}; i < 50; i++) {
            ret = file::write(file::name_t::ASCON_SEED,
                              {reinterpret_cast<const uint8_t*>(SEED.data()), SEED.size()});
            TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

            ret = file::write(file::name_t::PASSWORD,
                              {reinterpret_cast<const uint8_t*>(PASSWORD.data()), PASSWORD.size()});
            TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

            ret = file::write(file::name_t::PNUMBERS,
                              {reinterpret_cast<const uint8_t*>(PNUMBERS.data()), PNUMBERS.size()});
            TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        }
    }

    void test_file_sync() {
        auto ret = file::sync(file::name_t::ASCON_SEED);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = file::sync(file::name_t::PASSWORD);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = file::sync(file::name_t::PNUMBERS);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Should return an error since the COUNTER file is not accessible at runtime
        ret = file::sync(file::name_t::COUNTER);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Should return an error since COUNT is not a valid file
        ret = file::sync(file::name_t::COUNT);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);
    }

    void test_file_read() {
        std::array<uint8_t, SEED.size()>     seed_buf{};
        std::array<uint8_t, PASSWORD.size()> password_buf{};
        std::array<uint8_t, PNUMBERS.size()> pnumber_buf{};

        auto ret = file::read(file::name_t::ASCON_SEED, seed_buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = file::read(file::name_t::PASSWORD, password_buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = file::read(file::name_t::PNUMBERS, pnumber_buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Ensure the read data matches what was written
        auto seed =
            std::string_view{reinterpret_cast<const char*>(seed_buf.data()), seed_buf.size()};
        auto password = std::string_view{reinterpret_cast<const char*>(password_buf.data()),
                                         password_buf.size()};
        auto pnumber =
            std::string_view{reinterpret_cast<const char*>(pnumber_buf.data()), pnumber_buf.size()};

        TEST_ASSERT_TRUE(seed == SEED);
        TEST_ASSERT_TRUE(password == PASSWORD);
        TEST_ASSERT_TRUE(pnumber == PNUMBERS);

        // Should return an error since the COUNTER file is not accessible at runtime
        ret = file::read(file::name_t::COUNTER, {});
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Should return an error since COUNT is not a valid file
        ret = file::read(file::name_t::COUNT, {});
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);
    }

    void test_all() {
        RUN_TEST(test_uninit_guards);
        RUN_TEST(test_init);
        RUN_TEST(test_get_boot_count);
        RUN_TEST(test_file_write);
        RUN_TEST(test_file_sync);
        RUN_TEST(test_file_read);
        RUN_TEST(test_deinit);
        RUN_TEST(test_uninit_guards);
    }

} // namespace file_test

#include "unity.h"

#include "file.hpp"
#include "utils.hpp"

#include <array>
#include <cstring>
#include <string_view>

namespace file_test {

    namespace {
        // 16-byte payload — large enough to split into sub-regions for offset tests
        constexpr std::string_view PAYLOAD = "ABCDEFGHIJKLMNOP";

        static_assert(PAYLOAD.size() == 16);

        constexpr uint32_t HALF_OFFSET = PAYLOAD.size() / 2; // 8
        constexpr uint32_t LAST_OFFSET = PAYLOAD.size() - 1; // 15
    } // namespace

    // -------------------------------------------------------------------------
    // byte_offset: write at non-zero offset, read back partial region
    // -------------------------------------------------------------------------

    void test_write_at_offset() {
        // Write full payload first so the file has known content
        auto ret = file::write(file::name_t::ASCON_SEED, {reinterpret_cast<const uint8_t*>(PAYLOAD.data()), PAYLOAD.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Overwrite the second half only
        constexpr std::string_view PATCH = "QRSTUVWX";
        static_assert(PATCH.size() == HALF_OFFSET);

        ret = file::write(file::name_t::ASCON_SEED, {reinterpret_cast<const uint8_t*>(PATCH.data()), PATCH.size()}, HALF_OFFSET);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Read back full content and verify the first half is untouched,
        // second half reflects the patch
        std::array<uint8_t, PAYLOAD.size()> buf{};
        ret = file::read(file::name_t::ASCON_SEED, buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        const auto first_half  = std::string_view{reinterpret_cast<const char*>(buf.data()), HALF_OFFSET};
        const auto second_half = std::string_view{reinterpret_cast<const char*>(buf.data() + HALF_OFFSET), HALF_OFFSET};

        TEST_ASSERT_TRUE(first_half == PAYLOAD.substr(0, HALF_OFFSET));
        TEST_ASSERT_TRUE(second_half == PATCH);
    }

    void test_read_at_offset() {
        // Write known payload
        auto ret = file::write(file::name_t::PASSWORD, {reinterpret_cast<const uint8_t*>(PAYLOAD.data()), PAYLOAD.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Read only the second half via offset
        std::array<uint8_t, HALF_OFFSET> buf{};
        ret = file::read(file::name_t::PASSWORD, buf, HALF_OFFSET);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        const auto result = std::string_view{reinterpret_cast<const char*>(buf.data()), buf.size()};
        TEST_ASSERT_TRUE(result == PAYLOAD.substr(HALF_OFFSET));
    }

    void test_read_write_last_byte_offset() {
        // Write full payload
        auto ret = file::write(file::name_t::PNUMBERS, {reinterpret_cast<const uint8_t*>(PAYLOAD.data()), PAYLOAD.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Overwrite the single last byte
        const uint8_t last_byte = 0xFFU;
        ret                     = file::write(file::name_t::PNUMBERS, {&last_byte, 1}, LAST_OFFSET);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Read it back
        uint8_t read_byte{};
        ret = file::read(file::name_t::PNUMBERS, {&read_byte, 1}, LAST_OFFSET);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        TEST_ASSERT_EQUAL(0xFFU, read_byte);
    }

    // -------------------------------------------------------------------------
    // read with a buffer smaller than file content
    // -------------------------------------------------------------------------

    void test_read_undersized_buffer() {
        // Write a full payload
        auto ret = file::write(file::name_t::ASCON_SEED, {reinterpret_cast<const uint8_t*>(PAYLOAD.data()), PAYLOAD.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Read with a buffer smaller than what was written — LittleFS will
        // return exactly buf.size() bytes, which matches data.size(), so this
        // should succeed and return only the first N bytes
        std::array<uint8_t, HALF_OFFSET> small_buf{};
        ret = file::read(file::name_t::ASCON_SEED, small_buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        const auto result = std::string_view{reinterpret_cast<const char*>(small_buf.data()), small_buf.size()};
        TEST_ASSERT_TRUE(result == PAYLOAD.substr(0, HALF_OFFSET));
    }

    // -------------------------------------------------------------------------
    // write with empty span on valid files
    // -------------------------------------------------------------------------

    void test_write_empty_span() {
        // An empty write (zero bytes) on a valid file — LittleFS should
        // write 0 bytes and return 0, which matches data.size() == 0, so NONE
        auto ret = file::write(file::name_t::ASCON_SEED, {});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = file::write(file::name_t::PASSWORD, {});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = file::write(file::name_t::PNUMBERS, {});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    // -------------------------------------------------------------------------
    // get_boot_cycle_count after deinit (covers the gap in the re-run of
    // uninit_guards which didn't include this function explicitly)
    // -------------------------------------------------------------------------

    void test_get_boot_count_after_deinit() {
        // Assumes deinit has already been called by the caller/sequence
        auto rc = file::get_boot_cycle_count();
        TEST_ASSERT_FALSE(rc.has_value());
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, rc.error());
    }

    // -------------------------------------------------------------------------
    // Sequence — must run after test_init() and before test_deinit()
    //            in the parent test_all() runner, or standalone with its own
    //            init/deinit wrapping
    // -------------------------------------------------------------------------

    void all() {
        // Pre: init
        auto ret = file::init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        RUN_TEST(test_write_at_offset);
        RUN_TEST(test_read_at_offset);
        RUN_TEST(test_read_write_last_byte_offset);
        RUN_TEST(test_read_undersized_buffer);
        RUN_TEST(test_write_empty_span);

        // Post: deinit, then verify get_boot_cycle_count guard
        ret = file::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        RUN_TEST(test_get_boot_count_after_deinit);
    }

} // namespace file_test

#include "unity.h"

#include "file.hpp"
#include "utils.hpp"

#include <array>
#include <cstring>
#include <string_view>

namespace file::test {

    namespace {
        // 16 byte data, large enough to split into sub-regions for offset tests
        constexpr std::string_view PAYLOAD = "ABCDEFGHIJKLMNOP";

        static_assert(PAYLOAD.size() == 16);

        constexpr uint32_t HALF_OFFSET = PAYLOAD.size() / 2; // 8
        constexpr uint32_t LAST_OFFSET = PAYLOAD.size() - 1; // 15
    } // namespace

    void write_at_offset() {
        // Write full payload first so the file has known content
        auto ret = write(name_t::ASCON_SEED, {reinterpret_cast<const uint8_t*>(PAYLOAD.data()), PAYLOAD.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Overwrite the second half only
        constexpr std::string_view PATCH = "QRSTUVWX";
        static_assert(PATCH.size() == HALF_OFFSET);

        ret = write(name_t::ASCON_SEED, {reinterpret_cast<const uint8_t*>(PATCH.data()), PATCH.size()}, HALF_OFFSET);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Read back full content and verify the first half is untouched,
        // second half reflects the patch
        std::array<uint8_t, PAYLOAD.size()> buf{};
        ret = read(name_t::ASCON_SEED, buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        const auto first_half  = std::string_view{reinterpret_cast<const char*>(buf.data()), HALF_OFFSET};
        const auto second_half = std::string_view{reinterpret_cast<const char*>(buf.data() + HALF_OFFSET), HALF_OFFSET};

        TEST_ASSERT_TRUE(first_half == PAYLOAD.substr(0, HALF_OFFSET));
        TEST_ASSERT_TRUE(second_half == PATCH);
    }

    void read_at_offset() {
        // Write known payload
        auto ret = write(name_t::PASSWORD, {reinterpret_cast<const uint8_t*>(PAYLOAD.data()), PAYLOAD.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Read only the second half via offset
        std::array<uint8_t, HALF_OFFSET> buf{};
        ret = read(name_t::PASSWORD, buf, HALF_OFFSET);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        const auto result = std::string_view{reinterpret_cast<const char*>(buf.data()), buf.size()};
        TEST_ASSERT_TRUE(result == PAYLOAD.substr(HALF_OFFSET));
    }

    void read_write_last_byte_offset() {
        // Write full payload
        auto ret = write(name_t::PNUMBERS, {reinterpret_cast<const uint8_t*>(PAYLOAD.data()), PAYLOAD.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Overwrite the single last byte
        const uint8_t last_byte = 0xFFU;
        ret                     = write(name_t::PNUMBERS, {&last_byte, 1}, LAST_OFFSET);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Read it back
        uint8_t read_byte{};
        ret = read(name_t::PNUMBERS, {&read_byte, 1}, LAST_OFFSET);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        TEST_ASSERT_EQUAL(0xFFU, read_byte);
    }

    void read_undersized_buffer() {
        // Write a full payload
        auto ret = write(name_t::ASCON_SEED, {reinterpret_cast<const uint8_t*>(PAYLOAD.data()), PAYLOAD.size()});
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Read with a buffer smaller than what was written; LittleFS will
        // return exactly buf.size() bytes, which matches data.size(), so this
        // should succeed and return only the first N bytes
        std::array<uint8_t, HALF_OFFSET> small_buf{};
        ret = read(name_t::ASCON_SEED, small_buf);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        const auto result = std::string_view{reinterpret_cast<const char*>(small_buf.data()), small_buf.size()};
        TEST_ASSERT_TRUE(result == PAYLOAD.substr(0, HALF_OFFSET));
    }

    void write_empty_span() {
        auto ret = write(name_t::ASCON_SEED, {});
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        ret = write(name_t::PASSWORD, {});
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        ret = write(name_t::PNUMBERS, {});
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);
    }

    void get_boot_count_after_deinit() {
        // Assumes deinit has already been called by the caller/sequence
        auto rc = get_boot_cycle_count();
        TEST_ASSERT_FALSE(rc.has_value());
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, rc.error());
    }

    void all() {
        // Setup
        auto ret = init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init should fail
        ret = init();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        RUN_TEST(write_at_offset);
        RUN_TEST(read_at_offset);
        RUN_TEST(read_write_last_byte_offset);
        RUN_TEST(read_undersized_buffer);
        RUN_TEST(write_empty_span);

        // Teardown
        ret = deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit should fail
        ret = deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        RUN_TEST(get_boot_count_after_deinit);
    }

} // namespace file::test

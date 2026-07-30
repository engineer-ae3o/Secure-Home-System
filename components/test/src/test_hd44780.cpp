#include "unity.h"

#include "utils.hpp"
#include "hd44780.hpp"

#include <string_view>

namespace lcd::test {

    void put_char_edge_cases() {
        // Space: valid printable boundary
        auto ret = put_char(' ', 0, 0);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // 0x00 (NUL): non-printable. The implementation forwards it straight
        // to send_data without filtering, so it should still return NONE since
        // the hardware accepts any byte as a character code
        ret = put_char(0x00U, 0, 0);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // 0xFF: max byte value, valid HD44780 custom char index
        ret = put_char(0xFFU, 0, 0);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Bottom-right corner: column COLUMNS-1, row ROWS-1 (already in base
        // tests but repeated here for grouping clarity: last valid cell)
        ret = put_char('!', COLUMNS - 1, ROWS - 1);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // col == COLUMNS, row valid: off by one on column
        ret = put_char('X', COLUMNS, 0);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // col valid, row == ROWS: off by one on row
        ret = put_char('X', 0, ROWS);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Both out of bounds simultaneously
        ret = put_char('X', COLUMNS, ROWS);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);
    }

    void println_empty_string() {
        // Empty string: length 0, within COLUMNS, should succeed.
        // With pad_to_whitespace == true the whole line gets blanked.
        auto ret = println("", 0);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Same, without padding: should also succeed
        ret = println("", 0, false);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void println_full_string_no_pad() {
        // FULL_STR is exactly COLUMNS chars: valid with or without padding
        constexpr std::string_view FULL_STR = "Exactly16Chars!!";
        static_assert(FULL_STR.size() == COLUMNS);

        auto ret = println(FULL_STR, 0, false);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = println(FULL_STR, 1, false);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void println_all_lines() {
        constexpr std::string_view STR = "Test";

        for (uint8_t line{}; line < ROWS; line++) {
            auto ret = println(STR, line);
            TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        }
    }

    void clear_screen_twice() {
        auto ret = clear_screen();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = clear_screen();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void all() {
        auto ret = init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        RUN_TEST(put_char_edge_cases);
        RUN_TEST(println_empty_string);
        RUN_TEST(println_full_string_no_pad);
        RUN_TEST(println_all_lines);
        RUN_TEST(clear_screen_twice);

        ret = deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

} // namespace lcd::test

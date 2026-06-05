extern "C" {
#include "unity.h"
}

#include "utils.hpp"
#include "hd44780.hpp"

#include <string_view>

namespace {
    constexpr std::string_view SHORT_STR = "Hello";
    constexpr std::string_view FULL_STR  = "Exactly16Chars!!";
    constexpr std::string_view LONG_STR  = "This is too long!!";
} // namespace

namespace lcd_test {

    void uninit_guards() {
        auto ret = lcd::put_char('A', 0, 0);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = lcd::println(SHORT_STR, 0);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = lcd::clear_screen();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = lcd::backlight_on();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void init() {
        auto ret = lcd::init();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init should fail
        ret = lcd::init();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void deinit() {
        auto ret = lcd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit should fail
        ret = lcd::deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void backlight() {
        auto ret = lcd::backlight_on(true);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = lcd::backlight_on(false);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Default parameter. Backlight should be on now
        ret = lcd::backlight_on();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void clear_screen() {
        auto ret = lcd::clear_screen();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void put_char() {
        // Valid positions
        auto ret = lcd::put_char('A', 0, 0);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Last valid position
        ret = lcd::put_char('Z', lcd::COLUMNS - 1, lcd::ROWS - 1);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Column out of bounds
        ret = lcd::put_char('X', lcd::COLUMNS, 0);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Line out of bounds
        ret = lcd::put_char('X', 0, lcd::ROWS);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);
    }

    void println() {
        // Valid short string on each line
        for (uint8_t line{}; line < lcd::ROWS; line++) {
            auto ret = lcd::println(SHORT_STR, line);
            TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
        }

        // Exactly COLUMNS length should be valid
        auto ret = lcd::println(FULL_STR, 0);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Over COLUMNS should fail
        ret = lcd::println(LONG_STR, 0);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Line out of bounds
        ret = lcd::println(SHORT_STR, lcd::ROWS);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_ARG, ret);

        // Without padding
        ret = lcd::println(SHORT_STR, 0, false);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void all() {
        RUN_TEST(uninit_guards);
        RUN_TEST(init);
        RUN_TEST(backlight);
        RUN_TEST(clear_screen);
        RUN_TEST(put_char);
        RUN_TEST(println);
        RUN_TEST(deinit);
        RUN_TEST(uninit_guards);
    }

} // namespace lcd_test

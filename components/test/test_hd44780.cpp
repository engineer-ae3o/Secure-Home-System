#include "utils.hpp"
#include "hd44780.hpp"

#include <string_view>

namespace lcd {

    namespace {
        constexpr std::string_view SHORT_STR = "Hello";
        constexpr std::string_view FULL_STR  = "Exactly16Chars!!";
        constexpr std::string_view LONG_STR  = "This is too long!!";
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

    void test_backlight() {
        auto ret = backlight_on(true);
        utils::assert_check(ret == utils::error_t::NONE);

        ret = backlight_on(false);
        utils::assert_check(ret == utils::error_t::NONE);

        // Default parameter. Backlight should be on now
        ret = backlight_on();
        utils::assert_check(ret == utils::error_t::NONE);
    }

    void test_clear_screen() {
        auto ret = clear_screen();
        utils::assert_check(ret == utils::error_t::NONE);
    }

    void test_put_char() {
        // Valid positions
        auto ret = put_char('A', 0, 0);
        utils::assert_check(ret == utils::error_t::NONE);

        // Last valid position
        ret = put_char('Z', COLUMNS - 1, ROWS - 1);
        utils::assert_check(ret == utils::error_t::NONE);

        // Column out of bounds
        ret = put_char('X', COLUMNS, 0);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);

        // Line out of bounds
        ret = put_char('X', 0, ROWS);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);
    }

    void test_println() {
        // Valid short string on each line
        for (uint8_t line{}; line < ROWS; line++) {
            auto ret = println(SHORT_STR, line);
            utils::assert_check(ret == utils::error_t::NONE);
        }

        // Exactly COLUMNS length should be valid
        auto ret = println(FULL_STR, 0);
        utils::assert_check(ret == utils::error_t::NONE);

        // Over COLUMNS should fail
        ret = println(LONG_STR, 0);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);

        // Line out of bounds
        ret = println(SHORT_STR, ROWS);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_ARG);

        // Without padding
        ret = println(SHORT_STR, 0, false);
        utils::assert_check(ret == utils::error_t::NONE);
    }

    void test_uninit_guards() {
        // All public functions should reject calls before init
        auto ret = put_char('A', 0, 0);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);

        ret = println(SHORT_STR, 0);
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);

        ret = clear_screen();
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);

        ret = backlight_on();
        utils::assert_check(ret == utils::error_t::ERR_INVALID_STATE);
    }

    void test_all() {
        test_uninit_guards();
        test_init();
        test_backlight();
        test_clear_screen();
        test_put_char();
        test_println();
        test_deinit();
        test_uninit_guards();
    }

} // namespace lcd

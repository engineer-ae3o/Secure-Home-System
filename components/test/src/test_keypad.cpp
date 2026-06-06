extern "C" {
#include "unity.h"
}

#include "utils.hpp"
#include "config.hpp"
#include "keypad.hpp"
#include "test_keypad.hpp"

#include "FreeRTOS.h"
#include "task.h"

namespace keypad_test {

    namespace {

        pad::keypad_t<config::QUEUE_SIZE> s_keypad;

    } // namespace

    void uninit_guards() {
        auto ret = s_keypad.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        auto queue = s_keypad.get_event_queue();
        TEST_ASSERT_FALSE(queue.has_value());
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, queue.error());
    }

    void init() {
        const pad::config_t config = {
            .row_port = config::KEYPAD_ROW_PINS[0].port,
            .col_port = config::KEYPAD_COLUMN_PINS[0].port,
            .row_pins =
                {
                    config::KEYPAD_ROW_PINS[0].pin,
                    config::KEYPAD_ROW_PINS[1].pin,
                    config::KEYPAD_ROW_PINS[2].pin,
                    config::KEYPAD_ROW_PINS[3].pin,
                },
            .col_pins =
                {
                    config::KEYPAD_COLUMN_PINS[0].pin,
                    config::KEYPAD_COLUMN_PINS[1].pin,
                    config::KEYPAD_COLUMN_PINS[2].pin,
                    config::KEYPAD_COLUMN_PINS[3].pin,
                },
        };
        auto ret = s_keypad.init(config);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init should fail
        ret = s_keypad.init(config);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void get_event_queue() {
        auto queue = s_keypad.get_event_queue();
        TEST_ASSERT_TRUE(queue.has_value());
        TEST_ASSERT_NOT_NULL(queue.value());
    }

    void key_presses() {
        // Test logic for receiving key presses
    }

    void deinit() {
        auto ret = s_keypad.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit should fail
        ret = s_keypad.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        // Queue should no longer be accessible after deinit
        auto queue = s_keypad.get_event_queue();
        TEST_ASSERT_FALSE(queue.has_value());
    }

    void all() {
        RUN_TEST(uninit_guards);
        RUN_TEST(init);
        RUN_TEST(get_event_queue);
        RUN_TEST(key_presses);
        RUN_TEST(deinit);
        RUN_TEST(uninit_guards);
    }

} // namespace keypad_test

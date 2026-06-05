extern "C" {
#include "unity.h"
}

#include "utils.hpp"
#include "keypad.hpp"
#include "test_keypad.hpp"

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

namespace keypad_test {

    namespace {

        constexpr uint8_t QUEUE_LENGTH{8};

        constexpr pad::config_t CONFIG = {
            .row_port = GPIOA,
            .col_port = GPIOB,
            .row_pins = {GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3},
            .col_pins = {GPIO_PIN_4, GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7},
        };

    } // namespace

    static pad::keypad_t<QUEUE_LENGTH> s_keypad{};

    void uninit_guards() {
        auto ret = s_keypad.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        auto queue = s_keypad.get_event_queue();
        TEST_ASSERT_FALSE(queue.has_value());
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, queue.error());
    }

    void init() {
        auto ret = s_keypad.init(CONFIG);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init should fail
        ret = s_keypad.init(CONFIG);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void get_event_queue() {
        auto queue = s_keypad.get_event_queue();
        TEST_ASSERT_TRUE(queue.has_value());
        TEST_ASSERT_NOT_NULL(queue.value());
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

    void irq_handler() {
        // The full scan logic requires hardware — this only verifies the
        // IRQ handler doesn't crash and the timer is started without error.
        // Key detection is verified on-hardware by observing queue output.
        auto ret = s_keypad.init(CONFIG);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Simulate an IRQ firing — will start the debounce timer
        s_keypad.irq_handler();

        // Give the timer daemon time to process
        vTaskDelay(pdMS_TO_TICKS(pad::DEBOUNCE_TIME_MS + 10));

        ret = s_keypad.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void all() {
        RUN_TEST(uninit_guards);
        RUN_TEST(init);
        RUN_TEST(get_event_queue);
        RUN_TEST(deinit);
        RUN_TEST(uninit_guards);
        RUN_TEST(irq_handler);
    }

} // namespace keypad_test

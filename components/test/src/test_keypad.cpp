#include "unity.h"

#include "utils.hpp"
#include "keypad.hpp"
#include "config.hpp"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <array>
#include <cstdint>

// Test strategy:
//   1. Disable NVIC for all column IRQs (EXTI3_IRQn, EXTI4_IRQn, EXTI9_5_IRQn)
//      so real hardware doesn't race with the manual trigger
//   2. Reconfigure the target column pin as output push-pull and drive LOW;
//      this simulates the physical key press. The debounce timer callback reads
//      the actual IDR register, so the pin MUST be driven low for scanning to work.
//   3. Call irq_handler() directly (row pins are already LOW from init; that's
//      the default state set by the driver, which is what triggers falling edge
//      detection on a column when a key bridges row to column)
//   4. Block on xQueueReceive() with a timeout > DEBOUNCE_TIME_MS (50ms).
//      The debounce callback runs in the FreeRTOS timer task and pushes to the
//      queue when it identifies the pressed key.
//   5. Verify the received character matches KEYS[row][col]
//   6. Restore the column pin to input pull-up and re-enable NVIC

namespace keypad_test {

    namespace {
        // Create the keypad instance
        pad::keypad_t<config::QUEUE_SIZE> s_keypad;

        // Column IRQ lines; all three must be disabled during tests
        // PB3 → EXTI3,  PB4 → EXTI4,  PB5+PB8 → EXTI9_5
        constexpr std::array<IRQn_Type, 3> COL_IRQS = {
            EXTI3_IRQn,
            EXTI4_IRQn,
            EXTI9_5_IRQn,
        };

        // Timeout must exceed DEBOUNCE_TIME_MS by a comfortable margin
        constexpr uint32_t QUEUE_TIMEOUT_MS{150};

        void enable_col_irqs(bool ena = true) {
            if (ena) {
                for (const auto irq : COL_IRQS) {
                    HAL_NVIC_EnableIRQ(irq);
                }
            } else {
                for (const auto irq : COL_IRQS) {
                    HAL_NVIC_DisableIRQ(irq);
                }
            }
        }

        // Drive a specific column pin LOW to simulate a key press.
        // The debounce_timer_cb reads GPIO IDR, so this must be a real driven output.
        void press_column(uint8_t col) {
            GPIO_InitTypeDef cfg = {
                .Pin   = config::KEYPAD_COLUMN_PINS[col].pin,
                .Mode  = GPIO_MODE_OUTPUT_PP,
                .Pull  = GPIO_NOPULL,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(config::KEYPAD_COLUMN_PINS[col].port, &cfg);
            HAL_GPIO_WritePin(config::KEYPAD_COLUMN_PINS[col].port, config::KEYPAD_COLUMN_PINS[col].pin, GPIO_PIN_RESET);
        }

        // Release the column pin back to input pull-up.
        // Does not restore EXTI mode since NVIC stays disabled during tests.
        void release_column(uint8_t col) {
            GPIO_InitTypeDef cfg = {
                .Pin   = config::KEYPAD_COLUMN_PINS[col].pin,
                .Mode  = GPIO_MODE_INPUT,
                .Pull  = GPIO_PULLUP,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(config::KEYPAD_COLUMN_PINS[col].port, &cfg);
        }

        // Full key press simulation for a given [row][col].
        // Returns the character received from the queue, or '\0' on timeout.
        char simulate_key_and_receive(QueueHandle_t queue, uint8_t col) {
            press_column(col);
            s_keypad.irq_handler();

            char received{};
            xQueueReceive(queue, &received, pdMS_TO_TICKS(QUEUE_TIMEOUT_MS));

            release_column(col);
            return received;
        }

    } // namespace

    void uninit_guards() {
        auto result = s_keypad.get_event_queue();
        TEST_ASSERT_FALSE(result.has_value());
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, result.error());

        auto ret = s_keypad.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void init() {
        const pad::config_t cfg = {
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

        auto ret = s_keypad.init(cfg);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init must fail
        ret = s_keypad.init(cfg);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void get_event_queue_is_valid() {
        auto result = s_keypad.get_event_queue();
        TEST_ASSERT_TRUE(result.has_value());
        TEST_ASSERT_NOT_NULL(result.value());
    }

    void event_queue_is_empty_after_init() {
        auto result = s_keypad.get_event_queue();
        TEST_ASSERT_TRUE(result.has_value());

        TEST_ASSERT_EQUAL(0U, uxQueueMessagesWaiting(result.value()));
    }

    // Key press detection: one representative per row and one per column,
    // covering all four rows and all four columns without running all 16.
    //
    // Tested keys: '1'(r0,c0), 'A'(r0,c3), '5'(r1,c1), '9'(r2,c2),
    //              '*'(r3,c0), 'D'(r3,c3)

    void key_detection_runs(QueueHandle_t queue, uint8_t row, uint8_t col) {
        // All rows are already LOW (driver default), so pressing column[col] gives
        // the debounce callback a low signal on that column on every row scan pass.
        // The scan picks the first row that reads low on the target column, which
        // is row 0; but only for the row that was driven low by the scan loop.
        // Since rows are set HIGH before scanning then driven LOW one at a time,
        // the callback correctly isolates [row][col].
        enable_col_irqs(false);

        const char expected = pad::KEYS[row][col];
        const char received = simulate_key_and_receive(queue, col);

        enable_col_irqs();

        TEST_ASSERT_EQUAL(expected, received);
    }

    void key_1_r0_c0() {
        auto* queue = s_keypad.get_event_queue().value();
        key_detection_runs(queue, 0, 0); // '1'
    }

    void key_A_r0_c3() {
        auto* queue = s_keypad.get_event_queue().value();
        key_detection_runs(queue, 0, 3); // 'A'
    }

    void key_5_r1_c1() {
        auto* queue = s_keypad.get_event_queue().value();
        key_detection_runs(queue, 1, 1); // '5'
    }

    void key_9_r2_c2() {
        auto* queue = s_keypad.get_event_queue().value();
        key_detection_runs(queue, 2, 2); // '9'
    }

    void key_star_r3_c0() {
        auto* queue = s_keypad.get_event_queue().value();
        key_detection_runs(queue, 3, 0); // '*'
    }

    void key_D_r3_c3() {
        auto* queue = s_keypad.get_event_queue().value();
        key_detection_runs(queue, 3, 3); // 'D'
    }

    void queue_is_empty_between_presses() {
        auto* queue = s_keypad.get_event_queue().value();
        TEST_ASSERT_EQUAL(0U, uxQueueMessagesWaiting(queue));
    }

    void no_spurious_queue_event() {
        // irq_handler() is not called. Queue should be empty after timeout.
        enable_col_irqs(false);

        auto* queue = s_keypad.get_event_queue().value();

        char       received{};
        BaseType_t result = xQueueReceive(queue, &received, pdMS_TO_TICKS(QUEUE_TIMEOUT_MS));

        enable_col_irqs();

        TEST_ASSERT_EQUAL(pdFALSE, result);
        TEST_ASSERT_EQUAL('\0', received);
    }

    // Queue overflow. xQueueSend in the debounce callback uses timeout 0,
    // so excess events are silently dropped. Verify no crash and queue caps at
    // config::QUEUE_SIZE.

    void queue_overflow_drops_excess() {
        auto* queue = s_keypad.get_event_queue().value();

        enable_col_irqs(false);

        // Trigger more presses than queue_length. Each call goes through the
        // full debounce cycle so we wait for each one to complete before
        // triggering the next, keeping the queue filling steadily.
        // We use column 0 (key '1') for all presses for simplicity.
        const uint8_t OVERFLOW_COUNT = config::QUEUE_SIZE + 3;

        for (uint8_t i{}; i < OVERFLOW_COUNT; i++) {
            press_column(0);
            s_keypad.irq_handler();

            // Wait for debounce timer to process and push to queue
            vTaskDelay(pdMS_TO_TICKS(QUEUE_TIMEOUT_MS));

            release_column(0);

            // Brief gap between presses so EXTI->IMR is restored before next trigger
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        enable_col_irqs();

        // Queue must be capped at QUEUE_SIZE, not larger
        TEST_ASSERT_EQUAL(config::QUEUE_SIZE, uxQueueMessagesWaiting(queue));

        // All items in the queue must be '1' (KEYS[0][0])
        char item{};

        // Should run QUEUE_SIZE times
        while (xQueueReceive(queue, &item, 0) == pdTRUE) {
            TEST_ASSERT_EQUAL(pad::KEYS[0][0], item);
        }
    }

    void deinit() {
        auto ret = s_keypad.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit must fail
        ret = s_keypad.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void post_deinit_guards() {
        auto result = s_keypad.get_event_queue();
        TEST_ASSERT_FALSE(result.has_value());
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, result.error());

        auto ret = s_keypad.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void all() {
        RUN_TEST(uninit_guards);
        RUN_TEST(init);

        RUN_TEST(get_event_queue_is_valid);
        RUN_TEST(event_queue_is_empty_after_init);

        RUN_TEST(key_1_r0_c0);
        RUN_TEST(queue_is_empty_between_presses);

        RUN_TEST(key_A_r0_c3);
        RUN_TEST(queue_is_empty_between_presses);

        RUN_TEST(key_5_r1_c1);
        RUN_TEST(queue_is_empty_between_presses);

        RUN_TEST(key_9_r2_c2);
        RUN_TEST(queue_is_empty_between_presses);

        RUN_TEST(key_star_r3_c0);
        RUN_TEST(queue_is_empty_between_presses);

        RUN_TEST(key_D_r3_c3);
        RUN_TEST(queue_is_empty_between_presses);

        RUN_TEST(no_spurious_queue_event);
        RUN_TEST(queue_overflow_drops_excess);

        RUN_TEST(deinit);
        RUN_TEST(post_deinit_guards);
    }

} // namespace keypad_test

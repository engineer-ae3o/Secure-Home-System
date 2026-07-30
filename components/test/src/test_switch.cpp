#include "unity.h"

#include "utils.hpp"
#include "switch.hpp"
#include "config.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include <utility>
#include <cstdint>

// Test strategy:
//   1. Disable NVIC for the necessary irq line so no hardware interrupt fires
//   2. Reconfigure the pin as output and drive it HIGH (simulates NC switch
//      opening, the rising edge that would have triggered the real ISR)
//   3. Call irq_handler() directly to simulate the ISR executing
//   4. Block on xTaskNotifyWait() with a reasonable timeout
//   5. Verify the notification value carries the correct type bit

namespace nc::test {

    namespace {
        // NC switch instances under test
        switch_t<type_t::REED>  s_reed;
        switch_t<type_t::LIMIT> s_limit;

        constexpr uint32_t NOTIFY_TIMEOUT_MS{100};

        // Reconfigure a pin as output push-pull and drive it to the given level.
        // Used to simulate the physical switch state on the line before manually
        // invoking irq_handler().
        void drive_pin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state) {
            GPIO_InitTypeDef cfg = {
                .Pin   = pin,
                .Mode  = GPIO_MODE_OUTPUT_PP,
                .Pull  = GPIO_NOPULL,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(port, &cfg);
            HAL_GPIO_WritePin(port, pin, state);
        }

        // Restore a pin to input pull-up (its operational mode).
        // Called after each IRQ simulation so subsequent tests start clean.
        void restore_pin_as_input(GPIO_TypeDef* port, uint16_t pin) {
            GPIO_InitTypeDef cfg = {
                .Pin   = pin,
                .Mode  = GPIO_MODE_IT_RISING,
                .Pull  = GPIO_PULLUP,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(port, &cfg);
        }

    } // namespace

    void uninit_guards() {
        auto ret = s_reed.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = s_limit.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void reed_init() {
        const config_t cfg = {
            .port                = config::REED_SWITCH.port,
            .pin                 = config::REED_SWITCH.pin,
            .irq_type            = EXTI15_10_IRQn,
            .calling_task_handle = xTaskGetCurrentTaskHandle(),
        };

        auto ret = s_reed.init(cfg);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init must fail
        ret = s_reed.init(cfg);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void limit_init() {
        const config_t cfg = {
            .port                = config::TAMPER_SWITCH.port,
            .pin                 = config::TAMPER_SWITCH.pin,
            .irq_type            = EXTI15_10_IRQn,
            .calling_task_handle = xTaskGetCurrentTaskHandle(),
        };

        auto ret = s_limit.init(cfg);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init must fail
        ret = s_limit.init(cfg);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void reed_irq_notifies_task() {
        // Disable NVIC so real hardware doesn't interfere with our manual trigger
        HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);

        // Drive the pin HIGH: simulates the NC reed switch opening (rising edge)
        drive_pin(config::REED_SWITCH.port, config::REED_SWITCH.pin, GPIO_PIN_SET);

        // Manually invoke the ISR logic
        s_reed.irq_handler();

        // Block waiting for the task notification. Use xTaskNotifyWait since
        // irq_handler uses eSetBits; ulTaskNotifyTake would lose the value.
        uint32_t   notification_value{};
        BaseType_t result = xTaskNotifyWait(0, 0xFFFFFFFFUL, &notification_value, pdMS_TO_TICKS(NOTIFY_TIMEOUT_MS));

        // Restore pin and re-enable NVIC before asserting so state is clean
        // regardless of outcome
        restore_pin_as_input(config::REED_SWITCH.port, config::REED_SWITCH.pin);
        HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

        TEST_ASSERT_EQUAL(pdTRUE, result);
        TEST_ASSERT_EQUAL(std::to_underlying(type_t::REED), notification_value);
    }

    void reed_irq_multiple_triggers_accumulate_bits() {
        // Fire irq_handler twice without consuming the notification between calls.
        // eSetBits ORs into the value, so the bit should remain set both times.
        HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);

        drive_pin(config::REED_SWITCH.port, config::REED_SWITCH.pin, GPIO_PIN_SET);
        s_reed.irq_handler();
        s_reed.irq_handler();

        uint32_t   notification_value{};
        BaseType_t result = xTaskNotifyWait(0, 0xFFFFFFFFUL, &notification_value, pdMS_TO_TICKS(NOTIFY_TIMEOUT_MS));

        restore_pin_as_input(config::REED_SWITCH.port, config::REED_SWITCH.pin);
        HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

        TEST_ASSERT_EQUAL(pdTRUE, result);
        // REED bit must be set; value should still be exactly REED since REED|REED == REED
        TEST_ASSERT_BITS_HIGH(std::to_underlying(type_t::REED), notification_value);
    }

    void reed_no_spurious_notification_without_trigger() {
        // Do NOT call irq_handler(). Waiting for a notification should time out.
        HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);

        uint32_t   notification_value{};
        BaseType_t result = xTaskNotifyWait(0, 0xFFFFFFFFUL, &notification_value, pdMS_TO_TICKS(NOTIFY_TIMEOUT_MS));

        HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

        TEST_ASSERT_EQUAL(pdFALSE, result);
        TEST_ASSERT_EQUAL(0U, notification_value);
    }

    void limit_irq_notifies_task() {
        HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);

        drive_pin(config::TAMPER_SWITCH.port, config::TAMPER_SWITCH.pin, GPIO_PIN_SET);
        s_limit.irq_handler();

        uint32_t   notification_value{};
        BaseType_t result = xTaskNotifyWait(0, 0xFFFFFFFFUL, &notification_value, pdMS_TO_TICKS(NOTIFY_TIMEOUT_MS));

        restore_pin_as_input(config::TAMPER_SWITCH.port, config::TAMPER_SWITCH.pin);
        HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

        TEST_ASSERT_EQUAL(pdTRUE, result);
        TEST_ASSERT_EQUAL(std::to_underlying(type_t::LIMIT), notification_value);
    }

    void both_types_bits_are_distinct() {
        // REED and LIMIT must have different, non-overlapping type bits so a task
        // waiting on notifications can distinguish which switch triggered
        constexpr auto reed_bit  = std::to_underlying(type_t::REED);
        constexpr auto limit_bit = std::to_underlying(type_t::LIMIT);

        TEST_ASSERT_NOT_EQUAL(reed_bit, limit_bit);
        TEST_ASSERT_EQUAL(0U, reed_bit & limit_bit);
    }

    void both_switches_trigger_accumulates_both_bits() {
        // Fire both irq_handlers without consuming the notification.
        // The task's notification value should have both bits set.
        HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);

        drive_pin(config::REED_SWITCH.port, config::REED_SWITCH.pin, GPIO_PIN_SET);
        drive_pin(config::TAMPER_SWITCH.port, config::TAMPER_SWITCH.pin, GPIO_PIN_SET);

        s_reed.irq_handler();
        s_limit.irq_handler();

        uint32_t   notification_value{};
        BaseType_t result = xTaskNotifyWait(0, 0xFFFFFFFFUL, &notification_value, pdMS_TO_TICKS(NOTIFY_TIMEOUT_MS));

        restore_pin_as_input(config::REED_SWITCH.port, config::REED_SWITCH.pin);
        restore_pin_as_input(config::TAMPER_SWITCH.port, config::TAMPER_SWITCH.pin);
        HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

        TEST_ASSERT_EQUAL(pdTRUE, result);
        TEST_ASSERT_BITS_HIGH(std::to_underlying(type_t::REED), notification_value);
        TEST_ASSERT_BITS_HIGH(std::to_underlying(type_t::LIMIT), notification_value);
    }

    void reed_deinit() {
        auto ret = s_reed.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit must fail
        ret = s_reed.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void limit_deinit() {
        auto ret = s_limit.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit must fail
        ret = s_limit.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void all() {
        RUN_TEST(uninit_guards);

        RUN_TEST(reed_init);
        RUN_TEST(limit_init);

        RUN_TEST(both_types_bits_are_distinct);

        RUN_TEST(reed_irq_notifies_task);
        RUN_TEST(reed_irq_multiple_triggers_accumulate_bits);
        RUN_TEST(reed_no_spurious_notification_without_trigger);

        RUN_TEST(limit_irq_notifies_task);

        RUN_TEST(both_switches_trigger_accumulates_both_bits);

        RUN_TEST(reed_deinit);
        RUN_TEST(limit_deinit);

        RUN_TEST(uninit_guards);
    }

} // namespace nc::test

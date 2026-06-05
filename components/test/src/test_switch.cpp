extern "C" {
#include "unity.h"
}

#include "test_switch.hpp"
#include "config.hpp"
#include "switch.hpp"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"

namespace switch_test {

    namespace {

        const nc::config_t REED_CONFIG = {
            .port                = GPIOA,
            .pin                 = GPIO_PIN_0,
            .irq_type            = EXTI0_IRQn,
            .calling_task_handle = nullptr, // No task needed for init/deinit tests
        };

        const nc::config_t LIMIT_CONFIG = {
            .port                = GPIOB,
            .pin                 = GPIO_PIN_1,
            .irq_type            = EXTI1_IRQn,
            .calling_task_handle = nullptr,
        };

        // Reed and limit switch instances
        nc::switch_t<nc::type_t::REED>  s_reed{};
        nc::switch_t<nc::type_t::LIMIT> s_limit{};

    } // namespace

    void uninit_guards() {
        // deinit before init should fail
        auto ret = s_reed.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = s_limit.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void init() {
        auto ret = s_reed.init(REED_CONFIG);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double init should fail
        ret = s_reed.init(REED_CONFIG);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = s_limit.init(LIMIT_CONFIG);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = s_limit.init(LIMIT_CONFIG);
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void deinit() {
        auto ret = s_reed.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Double deinit should fail
        ret = s_reed.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);

        ret = s_limit.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        ret = s_limit.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::ERR_INVALID_STATE, ret);
    }

    void irq_handler() {
        // Reinitialize with a real task handle so the notification has somewhere to go
        nc::config_t config        = REED_CONFIG;
        config.calling_task_handle = xTaskGetCurrentTaskHandle();

        auto ret = s_reed.init(config);
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);

        // Fire the IRQ handler manually
        s_reed.irq_handler();

        // Expect a notification with the REED bit set
        uint32_t   bits{};
        BaseType_t notified = xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(100));
        TEST_ASSERT_TRUE(notified == pdTRUE);
        TEST_ASSERT_TRUE(bits & std::to_underlying(nc::type_t::REED));

        ret = s_reed.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void all() {
        RUN_TEST(uninit_guards);
        RUN_TEST(init);
        RUN_TEST(deinit);
        RUN_TEST(uninit_guards);
        RUN_TEST(irq_handler);
    }

} // namespace switch_test

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

        const nc::config_t REED_CONFIG = {
            .port                = config::REED_SWITCH.port,
            .pin                 = config::REED_SWITCH.pin,
            .irq_type            = EXTI15_10_IRQn,
            .calling_task_handle = xTaskGetCurrentTaskHandle(),
        };

        const nc::config_t LIMIT_CONFIG = {
            .port                = config::TAMPER_SWITCH.port,
            .pin                 = config::TAMPER_SWITCH.pin,
            .irq_type            = EXTI15_10_IRQn,
            .calling_task_handle = xTaskGetCurrentTaskHandle(),
        };

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

    void switch_broken() {
        // Fire the IRQ handler manually
        s_reed.irq_handler();

        // Expect a notification with the REED bit set
        uint32_t   bits{};
        BaseType_t notified = xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(100));
        TEST_ASSERT_TRUE(notified == pdTRUE);
        TEST_ASSERT_TRUE(bits & std::to_underlying(nc::type_t::REED));

        auto ret = s_reed.deinit();
        TEST_ASSERT_EQUAL(utils::error_t::NONE, ret);
    }

    void all() {
        RUN_TEST(uninit_guards);
        RUN_TEST(init);
        RUN_TEST(switch_broken);
        RUN_TEST(deinit);
        RUN_TEST(uninit_guards);
    }

} // namespace switch_test

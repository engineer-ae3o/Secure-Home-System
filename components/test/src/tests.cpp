#include "unity.h"

#include "utils.hpp"

#include "tests.hpp"
#include "test_file.hpp"
#include "test_csprng.hpp"
#include "test_keypad.hpp"
#include "test_switch.hpp"
#include "test_hd44780.hpp"
#include "test_sim800l.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include <array>
#include <cstdint>
#include <type_traits>

extern "C" {
    void setUp() {
        utils::log<utils::level_t::INFO>("Tests", "Setting up the unity test suite.");
    }

    void tearDown() {
        utils::log<utils::level_t::INFO>("Tests", "Tests complete. Tearing down unity.");
    }
}

namespace tests {

    namespace {

        StaticTask_t       task_tcb{};
        constexpr uint32_t TASK_PRIORITY    = configMAX_PRIORITIES - 1;
        constexpr uint32_t TASK_STACK_BYTES = 4096;
        constexpr uint32_t TASK_STACK_DEPTH = utils::bytes_to_words(TASK_STACK_BYTES);

        std::array<StackType_t, TASK_STACK_DEPTH> task_stack{};

        static_assert(sizeof(task_stack) == TASK_STACK_BYTES);
        static_assert(std::is_same_v<decltype(task_stack)::value_type, uint32_t>, "Word size must be 32 bits");

        /**
         * @brief The test runner is in the form of a FreeRTOS task. By default,
         *        it assumes it has the entire hardware to itself, so must not be
         *        run simultaneously with any other threads. It runs all the tests,
         *        deinitializes its internal state and then deletes itself.
         */
        void runner(void* arg) {
            UNUSED(arg);
            UNITY_BEGIN();

            nc::test::all();
            rnd::test::all();
            pad::test::all();
            file::test::all();
            // lcd::test::all();
            // gsm::test::all();

            UNITY_END();
            vTaskDelete(nullptr);
        }

    } // namespace

    void run() {
        xTaskCreateStatic(runner, "Test Runner", TASK_STACK_DEPTH, nullptr, TASK_PRIORITY, task_stack.data(), &task_tcb);
    }

} // namespace tests

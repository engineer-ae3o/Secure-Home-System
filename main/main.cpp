#include "stm32f1xx_hal.h"

#include "tests.hpp"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include <array>

namespace {

    // Tasks TCBs and Stacks
    std::array<StackType_t, TESTS_TASK_STACK_NYTES> test_task_stack{};
    StaticTask_t                                    test_task_tcb{};

} // namespace

extern "C" {

    [[noreturn]] int main() {
        HAL_Init();

        xTaskCreateStatic(tests,
                          "Tests Task",
                          utils::bytes_to_words(TESTS_TASK_STACK_NYTES),
                          nullptr,
                          10,
                          test_task_stack.data(),
                          &test_task_tcb);

        vTaskStartScheduler();

        while (true) {
        }
    }

} // extern "C"

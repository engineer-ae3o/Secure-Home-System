#include "stm32f1xx_hal.h"

#include "tests.hpp"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include <array>

namespace tests {
    [[maybe_unused]] void tests(void* arg) {
        UNUSED(arg);

        while (true) {
            __WFI();
        }
    }
} // namespace tests

extern "C" {

    [[noreturn]] int main() {
        HAL_Init();

        xTaskCreateStatic(tests::tests,
                          "Tests Task",
                          utils::bytes_to_words(tests::TESTS_TASK_STACK_BYTES),
                          nullptr,
                          10,
                          tests::test_task_stack.data(),
                          &tests::test_task_tcb);

        vTaskStartScheduler();

        while (true) {
        }
    }

} // extern "C"

#include "stm32f1xx_hal.h"

#include "tests.hpp"
#include "utils.hpp"
#include "tasks.hpp"

#include "FreeRTOS.h"
#include "task.h"

extern "C" {

    [[noreturn]] int main() {
        HAL_Init();

#if (BUILD_TESTS == 1)
        xTaskCreateStatic(tests::tests,
                          "Tests Task",
                          utils::bytes_to_words(tests::TASK_STACK_BYTES),
                          nullptr,
                          tests::TASK_PRIORITY,
                          tests::task_stack.data(),
                          &tests::task_tcb);
#else

#endif

        vTaskStartScheduler();

        while (true) {
        }
    }

} // extern "C"

#include "stm32f1xx_hal.h"

#include "tests.hpp"
#include "tasks.hpp"

#include "FreeRTOS.h"
#include "task.h"

#if defined(BUILD_TESTS)
#undef BUILD_TESTS
#define BUILD_TESTS 1
#else
#define BUILD_TESTS 0
#endif

extern "C" {

    [[noreturn]] int main() {
        HAL_Init();

#if BUILD_TESTS == 0
        tasks::run();
#else
        tests::run();
#endif

        vTaskStartScheduler();

        while (true) {
        }
    }

} // extern "C"

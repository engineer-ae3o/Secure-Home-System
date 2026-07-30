#include "stm32f1xx_hal.h"

#include "tests.hpp"
#include "tasks.hpp"

#include "FreeRTOS.h"
#include "task.h"

extern "C" {

    [[noreturn]] int main() {
        HAL_Init();

#if (BUILD_TESTS == 0)
        tasks::run();
#else
        tests::run();
#endif

        vTaskStartScheduler();

        while (true) {
        }
    }

} // extern "C"

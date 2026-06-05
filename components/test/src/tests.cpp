#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tests.hpp"
#include "test_file.hpp"
#include "test_hd44780.hpp"

extern "C" {

#include "unity.h"

    void setUp() {
    }

    void tearDown() {
    }
}

void tests(void* arg) {
    UNUSED(arg);

    HAL_Init();
    UNITY_BEGIN();

    file_test::all();
    lcd_test::all();

    UNITY_END();
    HAL_DeInit();

    vTaskDelete(nullptr);
}

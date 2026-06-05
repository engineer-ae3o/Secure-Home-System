#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tests.hpp"
#include "test_file.hpp"
#include "test_switch.hpp"
#include "test_keypad.hpp"
#include "test_hd44780.hpp"

extern "C" {

#include "unity.h"

    void setUp() {
    }

    void tearDown() {
    }
}

namespace tests {

    void tests(void* arg) {
        UNUSED(arg);

        UNITY_BEGIN();

        file_test::all();
        lcd_test::all();
        switch_test::all();
        keypad_test::all();

        UNITY_END();

        vTaskDelete(nullptr);
    }

} // namespace tests

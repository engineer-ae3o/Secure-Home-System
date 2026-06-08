#include "unity.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tests.hpp"
#include "test_file.hpp"
#include "test_random.hpp"
#include "test_keypad.hpp"
#include "test_switch.hpp"
#include "test_hd44780.hpp"
#include "test_sim800l.hpp"

extern "C" {

    void setUp() {
    }

    void tearDown() {
    }
}

namespace tests {

    void tests(void* arg) {
        UNUSED(arg);

        UNITY_BEGIN();

        //lcd_test::all();
        //gsm_test::all();
        //rnd_test::all();
        file_test::all();
        //switch_test::all();
        //keypad_test::all();

        UNITY_END();

        vTaskDelete(nullptr);
    }

} // namespace tests

#pragma once

#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include <array>
#include <cstdint>

namespace tests {

    inline StaticTask_t       test_task_tcb{};
    inline constexpr uint32_t TESTS_TASK_STACK_BYTES{2048};
    inline std::array<StackType_t, utils::bytes_to_words(TESTS_TASK_STACK_BYTES)> test_task_stack{};

    void tests(void* arg);

} // namespace tests

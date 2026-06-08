#pragma once

#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include <array>
#include <cstdint>

namespace tests {

    inline StaticTask_t                                                     task_tcb{};
    inline constexpr uint32_t                                               TASK_PRIORITY{configMAX_PRIORITIES - 1};
    inline constexpr uint32_t                                               TASK_STACK_BYTES{2048};
    inline std::array<StackType_t, utils::bytes_to_words(TASK_STACK_BYTES)> task_stack{};

    void tests(void* arg);

} // namespace tests

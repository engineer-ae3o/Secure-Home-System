#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "etl/array.h"


void led_task(void*) {
    
    GPIO_InitTypeDef init = {
        .Pin = GPIO_PIN_13,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_LOW
    };
    HAL_GPIO_Init(GPIOC, &init);
    
    while (1) {
        HAL_GPIO_WritePin(GPIOC, 13, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static etl::array<StackType_t, 20*4> led_task_buf{};
static StaticTask_t led_task_task{};

extern "C" {

    [[noreturn]] int main() {

        xTaskCreateStatic(led_task, "led_task", 20, nullptr, 6, led_task_buf.data(), &led_task_task);

        vTaskStartScheduler();

        while (1);
    }
}

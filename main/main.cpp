#include "stm32f1xx_hal.h"
#include "keypad.hpp"
#include "config.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include "etl/array.h"


void led_task(void*) {

    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    GPIO_InitTypeDef init = {
        .Pin = GPIO_PIN_13,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_LOW
    };
    HAL_GPIO_Init(GPIOC, &init);
    
    while (1) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        vTaskDelay(pdMS_TO_TICKS(5000));
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static etl::array<StackType_t, 1024> led_task_stack{};
static StaticTask_t led_task_tcb{};


extern "C" {
    
    [[noreturn]] int main() {

        HAL_Init();
        
        xTaskCreateStatic(led_task, "led_task", config::bytes_to_words(1024U), nullptr, 6, led_task_stack.data(), &led_task_tcb);
        
        vTaskStartScheduler();
        while (1);
    }
}

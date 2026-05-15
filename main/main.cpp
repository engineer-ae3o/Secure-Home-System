#include "stm32f1xx_hal.h"
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
        vTaskDelay(pdMS_TO_TICKS(500));
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static etl::array<StackType_t, 1024> led_task_buf{};
static StaticTask_t led_task_stack{};

//void delay_cycles(uint32_t cycles) {
//    volatile uint32_t mf = cycles;
//    while (mf--);
//}
//
//void delay_ms(uint32_t ms) {
//    volatile uint32_t mf = ms * 72'000;
//    delay_cycles(mf);
//}

extern "C" {

    [[noreturn]] int main() {
        
        //__HAL_RCC_GPIOC_CLK_ENABLE();
        //
        //GPIO_InitTypeDef init = {
        //    .Pin = GPIO_PIN_13,
        //    .Mode = GPIO_MODE_OUTPUT_PP,
        //    .Pull = GPIO_NOPULL,
        //    .Speed = GPIO_SPEED_LOW
        //};
        //HAL_GPIO_Init(GPIOC, &init);
//
        //while (1) {
        //    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        //    delay_ms(500);
        //    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        //    delay_ms(500);
        //}
        
        xTaskCreateStatic(led_task, "led_task", config::bytes_to_words(1024U), nullptr, 6, led_task_buf.data(), &led_task_stack);
        
        vTaskStartScheduler();
        while (1);
    }
}

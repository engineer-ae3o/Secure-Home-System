#include "stm32f1xx_hal.h"
#include "keypad.hpp"
#include "config.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include "etl/array.h"


static void led_task(void*) {

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
        vTaskDelay(pdMS_TO_TICKS(3000));
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void keypad_task(void*) {

    constexpr pad::config_t config = {
        .row_pins = { config::KEYPAD_ROW_PINS[0].pin, config::KEYPAD_ROW_PINS[1].pin,
                      config::KEYPAD_ROW_PINS[2].pin, config::KEYPAD_ROW_PINS[3].pin, },
        
        .col_pins = { config::KEYPAD_COLUMN_PINS[0].pin, config::KEYPAD_COLUMN_PINS[1].pin,
                      config::KEYPAD_COLUMN_PINS[2].pin, config::KEYPAD_COLUMN_PINS[3].pin, },
    };

    constexpr std::uintptr_t row_port = reinterpret_cast<uintptr_t>(config::KEYPAD_ROW_PINS[0].port);
    constexpr std::uintptr_t col_port = reinterpret_cast<uintptr_t>(config::KEYPAD_COLUMN_PINS[0].port);

    pad::keypad_t<row_port, col_port, 5> keypad(config);
    keypad.init();

    const auto& event_queue = keypad.get_event_queue();
    unsigned char key{};

    while (1) {
        xQueueReceive(event_queue, &key, portMAX_DELAY);
        (void)key;
    }
}

static etl::array<StackType_t, 512> led_task_stack{};
static StaticTask_t led_task_tcb{};

static etl::array<StackType_t, 2048> keypad_task_stack{};
static StaticTask_t keypad_task_tcb{};


extern "C" {
    
    [[noreturn]] int main() {

        HAL_Init();
        
        xTaskCreateStatic(led_task, "led_task", config::bytes_to_words(512), nullptr, 2, led_task_stack.data(), &led_task_tcb);
        xTaskCreateStatic(keypad_task, "keypad_task", config::bytes_to_words(2048), nullptr, 7, keypad_task_stack.data(), &keypad_task_tcb);
        
        vTaskStartScheduler();

        while (1);
    }
}

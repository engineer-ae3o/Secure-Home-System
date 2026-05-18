#include "stm32f1xx_hal.h"
#include "keypad.hpp"
#include "config.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include "etl/array.h"


static pad::keypad_t<config::QUEUE_SIZE> keypad;

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

    const pad::config_t config = {
        .row_port = config::KEYPAD_ROW_PINS[0].port,
        .row_pins = { config::KEYPAD_ROW_PINS[0].pin, config::KEYPAD_ROW_PINS[1].pin,
                      config::KEYPAD_ROW_PINS[2].pin, config::KEYPAD_ROW_PINS[3].pin, },
        
        .col_port = config::KEYPAD_COLUMN_PINS[0].port,
        .col_pins = { config::KEYPAD_COLUMN_PINS[0].pin, config::KEYPAD_COLUMN_PINS[1].pin,
                      config::KEYPAD_COLUMN_PINS[2].pin, config::KEYPAD_COLUMN_PINS[3].pin, },
    };

    keypad.init(config);

    const auto& event_queue = keypad.get_event_queue();
    unsigned char key{};

    // Enable the NVIC interrupts
    NVIC_EnableIRQ(EXTI3_IRQn);
    NVIC_EnableIRQ(EXTI4_IRQn);
    NVIC_EnableIRQ(EXTI9_5_IRQn);

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
        
        xTaskCreateStatic(led_task, "Led Task", config::bytes_to_words(512), nullptr, 2, led_task_stack.data(), &led_task_tcb);
        xTaskCreateStatic(keypad_task, "Keypad Task", config::bytes_to_words(2048), nullptr, 7, keypad_task_stack.data(), &keypad_task_tcb);
        
        vTaskStartScheduler();

        while (1);
    }

    void EXTI3_IRQHandler() {
        keypad.irq_handler();
    }

    void EXTI4_IRQHandler() {
        keypad.irq_handler();
    }

    void EXTI9_5_IRQHandler() {
        keypad.irq_handler();
    }
}

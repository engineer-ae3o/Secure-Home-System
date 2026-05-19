#include "stm32f1xx_hal.h"
#include "keypad.hpp"
#include "switch.hpp"
#include "config.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include "etl/array.h"


static pad::keypad_t<config::QUEUE_SIZE> keypad;
static nc::switch_t<nc::type_t::REED> reed;
static nc::switch_t<nc::type_t::LIMIT> tamper;


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
        vTaskDelay(pdMS_TO_TICKS(500));
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(500));
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

    NVIC_SetPriority(EXTI3_IRQn, 15);
    NVIC_SetPriority(EXTI4_IRQn, 15);
    NVIC_SetPriority(EXTI9_5_IRQn, 15);

    while (1) {
        xQueueReceive(event_queue, &key, portMAX_DELAY);
        (void)key;
    }
}

static void switch_task(void*) {

    const nc::config_t reed_config = {
        .port = config::REED_SWITCH.port,
        .pin = config::REED_SWITCH.pin,
        .irq_type = EXTI15_10_IRQn,
        .calling_task_handle = xTaskGetCurrentTaskHandle()
    };
    reed.init(reed_config);
    
    const nc::config_t tamper_config = {
        .port = config::TAMPER_SWITCH.port,
        .pin = config::TAMPER_SWITCH.pin,
        .irq_type = EXTI15_10_IRQn,
        .calling_task_handle = xTaskGetCurrentTaskHandle()
    };
    tamper.init(tamper_config);

    volatile nc::type_t type = nc::type_t::REED;

    while (1) {
        uint32_t flag{};
        xTaskNotifyWait(0, 0xFFFFFFFFU, &flag, portMAX_DELAY);
        
        if (flag & std::to_underlying(nc::type_t::REED)) {
            // Reed switch broken
            (void)flag;
            type = nc::type_t::REED;
            (void)type;
        }
        
        if (flag & std::to_underlying(nc::type_t::LIMIT)) {
            // Tamper switch broken
            (void)flag;
            type = nc::type_t::LIMIT;
            (void)type;
        }
    }
}

static etl::array<StackType_t, 512> led_task_stack{};
static StaticTask_t led_task_tcb{};

static etl::array<StackType_t, 512> keypad_task_stack{};
static StaticTask_t keypad_task_tcb{};

static etl::array<StackType_t, 512> switch_task_stack{};
static StaticTask_t switch_task_tcb{};


extern "C" {
    
    [[noreturn]] int main() {

        HAL_Init();
        
        xTaskCreateStatic(led_task, "Led Task", config::bytes_to_words(512), nullptr, 2, led_task_stack.data(), &led_task_tcb);
        xTaskCreateStatic(keypad_task, "Keypad Task", config::bytes_to_words(512), nullptr, 7, keypad_task_stack.data(), &keypad_task_tcb);
        xTaskCreateStatic(switch_task, "Switch Task", config::bytes_to_words(512), nullptr, 6, switch_task_stack.data(), &switch_task_tcb);
        
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

    void EXTI15_10_IRQHandler() {
        if (__HAL_GPIO_EXTI_GET_IT(config::REED_SWITCH.pin)) {
            reed.irq_handler();
        }
        if (__HAL_GPIO_EXTI_GET_IT(config::TAMPER_SWITCH.pin)) {
            tamper.irq_handler();
        }
    }
}

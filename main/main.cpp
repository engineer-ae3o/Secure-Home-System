#include "stm32f1xx_hal.h"

#include "hd44780.hpp"
#include "sim800l.hpp"
#include "keypad.hpp"
#include "switch.hpp"
#include "config.hpp"
#include "flash.hpp"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <array>

// Global because the ISR need to be able to see them
static pad::keypad_t<config::QUEUE_SIZE> keypad{};
static nc::switch_t<nc::type_t::REED>    reed{};
static nc::switch_t<nc::type_t::LIMIT>   tamper{};

// Neede to serialize use of the display
static SemaphoreHandle_t lcd_mutex{};
static StaticSemaphore_t lcd_mutex_buffer{};

// Thread safe LCD helper
void print(const std::string_view& str, uint8_t line) {
    xSemaphoreTake(lcd_mutex, portMAX_DELAY);
    lcd::println(str, line);
    vTaskDelay(pdMS_TO_TICKS(4000));
    xSemaphoreGive(lcd_mutex);
}

// Tasks
[[noreturn]] static void led_task(void* arg) {
    UNUSED(arg);

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef init = {
        .Pin   = GPIO_PIN_13,
        .Mode  = GPIO_MODE_OUTPUT_PP,
        .Pull  = GPIO_NOPULL,
        .Speed = GPIO_SPEED_LOW,
    };
    HAL_GPIO_Init(GPIOC, &init);

    //file::init();

    while (true) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        vTaskDelay(pdMS_TO_TICKS(500));
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

[[noreturn]] static void keypad_task(void* arg) {
    UNUSED(arg);

    const pad::config_t config = {
        // Ports
        .row_port = config::KEYPAD_ROW_PINS[0].port,
        .col_port = config::KEYPAD_COLUMN_PINS[0].port,
        // and pins
        .row_pins =
            {
                config::KEYPAD_ROW_PINS[0].pin,
                config::KEYPAD_ROW_PINS[1].pin,
                config::KEYPAD_ROW_PINS[2].pin,
                config::KEYPAD_ROW_PINS[3].pin,
            },
        .col_pins =
            {
                config::KEYPAD_COLUMN_PINS[0].pin,
                config::KEYPAD_COLUMN_PINS[1].pin,
                config::KEYPAD_COLUMN_PINS[2].pin,
                config::KEYPAD_COLUMN_PINS[3].pin,
            },
    };

    keypad.init(config);

    const auto&   event_queue = keypad.get_event_queue();
    unsigned char key{};

    // Enable the NVIC interrupts and set priority
    HAL_NVIC_SetPriority(EXTI3_IRQn, 15, 0);
    HAL_NVIC_SetPriority(EXTI4_IRQn, 15, 0);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 15, 0);

    HAL_NVIC_EnableIRQ(EXTI3_IRQn);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    while (true) {
        xQueueReceive(event_queue, &key, portMAX_DELAY);
        (void)key;
    }
}

[[noreturn]] static void switch_task(void* arg) {
    UNUSED(arg);

    const nc::config_t reed_config = {
        .port                = config::REED_SWITCH.port,
        .pin                 = config::REED_SWITCH.pin,
        .irq_type            = EXTI15_10_IRQn,
        .calling_task_handle = xTaskGetCurrentTaskHandle(),
    };
    reed.init(reed_config);

    const nc::config_t tamper_config = {
        .port                = config::TAMPER_SWITCH.port,
        .pin                 = config::TAMPER_SWITCH.pin,
        .irq_type            = EXTI15_10_IRQn,
        .calling_task_handle = xTaskGetCurrentTaskHandle(),
    };
    tamper.init(tamper_config);

    volatile nc::type_t type = nc::type_t::REED;

    while (true) {
        uint32_t flag{};
        xTaskNotifyWait(0, 0xFFFFFFFFU, &flag, portMAX_DELAY);

        if (static_cast<bool>(flag & std::to_underlying(nc::type_t::REED))) {
            // Reed switch broken
            (void)flag;
            type = nc::type_t::REED;
            (void)type;
        }

        if (static_cast<bool>(flag & std::to_underlying(nc::type_t::LIMIT))) {
            // Tamper switch broken
            (void)flag;
            type = nc::type_t::LIMIT;
            (void)type;
        }
    }
}

[[noreturn]] static void lcd_task(void* arg) {
    UNUSED(arg);

    lcd::init();
    lcd::clear_screen();
    lcd::backlight_on();

    // Text to be displayed
    constexpr std::array<std::array<std::string_view, 2>, 5> lcd_text = {{
        {"I", "hate"},
        {"my", "life."},
        {"This", "is"},
        {"so", "so"},
        {"damn", "boring"},
    }};

    while (true) {
        for (const auto& line : lcd_text) {
            // Print text. Bet you didn't know that before
            print(line[0], 0);
            print(line[1], 1);

            // Block 2.5s. Helpful? Share and drop a comment (hehe) if it did
            vTaskDelay(pdMS_TO_TICKS(2500));
        }
    }
}

[[noreturn]] static void gsm_task(void* arg) {
    UNUSED(arg);

    // The SIM800L requires apporx. 3s after bootup before any command can be sent
    vTaskDelay(pdMS_TO_TICKS(3000));

    auto ret = gsm::init();
    switch (ret) {
        case gsm::error_t::FAIL:
            print("An unknown", 0);
            print("error occured", 1);
            // Crash system for now
            utils::assert_check(false);
            break;
        case gsm::error_t::SIM_NOT_REGISTERED:
            print("SIM registration", 0);
            print("failed", 1);
            // Crash system for now
            utils::assert_check(false);
            break;
        case gsm::error_t::SIM_NOT_FOUND:
            print("SIM card", 0);
            print("not found", 1);
            // Crash system for now
            utils::assert_check(false);
            break;
        case gsm::error_t::BAD_NETWORK_CONN:
            print("Failed to get a", 0);
            print("good connection", 1);
            // Crash system for now
            utils::assert_check(false);
            break;
        case gsm::error_t::MODULE_NOT_ALIVE:
            print("GSM module", 0);
            print("not found", 1);
            // Crash system for now
            utils::assert_check(false);
            break;
        case gsm::error_t::NONE:
            print("SIM card found", 0);
            print("Reading IMSI", 1);
            break;
        case gsm::error_t::SMS_SEND_FAIL:
            print("Failed to", 0);
            print("send the SMS", 1);
            // Crash system for now
            utils::assert_check(false);
            break;
        case gsm::error_t::MUTEX_TIMEOUT:
            print("Internal error", 0);
            print("due to timeout", 1);
            // Crash system for now
            utils::assert_check(false);
            break;
        default:
            break;
    }

    const auto& imsi = gsm::get_imsi();
    if (!imsi) {
        print("Failed to read", 0);
        print("the SIM's IMSI", 1);
        // Crash system for now
        utils::assert_check(false);
    }

    print("SIM's IMSI: ", 0);
    print(std::string_view(imsi->data(), imsi->size() - 1), 1);

    // Do nothing for now
    while (true) {
        __WFI();
    }
}

// Tasks TCBs and Stacks
static std::array<StackType_t, 1024> led_task_stack{};
static StaticTask_t                  led_task_tcb{};

static std::array<StackType_t, 512> lcd_task_stack{};
static StaticTask_t                 lcd_task_tcb{};

static std::array<StackType_t, 512> gsm_task_stack{};
static StaticTask_t                 gsm_task_tcb{};

static std::array<StackType_t, 512> keypad_task_stack{};
static StaticTask_t                 keypad_task_tcb{};

static std::array<StackType_t, 512> switch_task_stack{};
static StaticTask_t                 switch_task_tcb{};

extern "C" {

    [[noreturn]] int main() {
        HAL_Init();

        lcd_mutex = xSemaphoreCreateMutexStatic(&lcd_mutex_buffer);

        xTaskCreateStatic(led_task, "Led Task", config::bytes_to_words(1024), nullptr, 2, led_task_stack.data(), &led_task_tcb);
        xTaskCreateStatic(lcd_task, "LCD Task", config::bytes_to_words(512), nullptr, 5, lcd_task_stack.data(), &lcd_task_tcb);
        xTaskCreateStatic(gsm_task, "GSM Task", config::bytes_to_words(512), nullptr, 6, gsm_task_stack.data(), &gsm_task_tcb);
        xTaskCreateStatic(keypad_task, "Keypad Task", config::bytes_to_words(512), nullptr, 3, keypad_task_stack.data(), &keypad_task_tcb);
        xTaskCreateStatic(switch_task, "Switch Task", config::bytes_to_words(512), nullptr, 4, switch_task_stack.data(), &switch_task_tcb);

        vTaskStartScheduler();

        while (true) {
        }
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

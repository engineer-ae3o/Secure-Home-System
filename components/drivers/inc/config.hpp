#pragma once


#include "stm32f103xb.h"
#include <cstdint>


namespace config {

    enum class log_level_t : uint8_t {
        NONE = 0,
        ERROR,
        WARN,
        INFO
    };

    constexpr inline log_level_t LOG_LEVEL = log_level_t::INFO;
    constexpr inline bool ASSERTS_ENABLED = true;

    // Core clock speed being used
    constexpr inline uint32_t CLOCK_SPEED_HZ = 72'000'000UL;

    struct gpio_pin_t {
        GPIO_TypeDef* port{};
        uint32_t pin;
    };

    // ADC pins to be used for entropy gathering for random number generation
    constexpr inline gpio_pin_t ADC_PINS[] = {
        { .port = GPIOA, .pin = GPIO_PIN_0 }, // ADC External Channel 0
        { .port = GPIOA, .pin = GPIO_PIN_1 }, // ADC External Channel 1
        { .port = GPIOA, .pin = GPIO_PIN_2 }, // ADC External Channel 2
        { .port = GPIOA, .pin = GPIO_PIN_3 }, // ADC External Channel 3
        { .port = GPIOA, .pin = GPIO_PIN_4 }, // ADC External Channel 4
        { .port = GPIOA, .pin = GPIO_PIN_5 }, // ADC External Channel 5
        { .port = GPIOA, .pin = GPIO_PIN_6 }, // ADC External Channel 6
        { .port = GPIOA, .pin = GPIO_PIN_7 }, // ADC External Channel 7
        { .port = GPIOB, .pin = GPIO_PIN_0 }, // ADC External Channel 8
        { .port = GPIOB, .pin = GPIO_PIN_1 }  // ADC External Channel 9
    };

    // LCD I2C pins
    constexpr inline auto LCD_I2C_PORT = I2C1;
    constexpr inline gpio_pin_t LCD_SCL = { .port = GPIOB, .pin = GPIO_PIN_6 };
    constexpr inline gpio_pin_t LCD_SDA = { .port = GPIOB, .pin = GPIO_PIN_7 };

    // GSM module UART pins
    constexpr inline auto GSM_UART_PORT = USART1;
    constexpr inline gpio_pin_t GSM_TX = { .port = GPIOA, .pin = GPIO_PIN_9 };
    constexpr inline gpio_pin_t GSM_RX = { .port = GPIOA, .pin = GPIO_PIN_10 };

    // Keypad matrix pins
    constexpr inline gpio_pin_t KEYPAD_PINS[] = {
        { .port = GPIOA, .pin = GPIO_PIN_8 },  // Row 0
        { .port = GPIOC, .pin = GPIO_PIN_13 }, // Row 1
        { .port = GPIOC, .pin = GPIO_PIN_14 }, // Row 2
        { .port = GPIOC, .pin = GPIO_PIN_15 }, // Row 3
        { .port = GPIOB, .pin = GPIO_PIN_3 },  // Column 0
        { .port = GPIOB, .pin = GPIO_PIN_4 },  // Column 1
        { .port = GPIOB, .pin = GPIO_PIN_5 },  // Column 2
        { .port = GPIOB, .pin = GPIO_PIN_8 },  // Column 3
    };

    // Reed and Tamper switches' pins
    constexpr inline gpio_pin_t REED_SWITCH   = { .port = GPIOB, .pin = GPIO_PIN_10 };
    constexpr inline gpio_pin_t TAMPER_SWITCH = { .port = GPIOB, .pin = GPIO_PIN_11 };

    // Needed for conversion since FreeRTOS uses words
    consteval inline size_t bytes_to_words(size_t bytes) { return bytes / 4; }

} // namespace config

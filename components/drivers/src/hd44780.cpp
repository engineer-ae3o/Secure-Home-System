#pragma once


#include "stm32f1xx_hal.h"
#include "hd44780.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "etl/string.h"
#include "etl/span.h"


namespace lcd {

    // Global state. Yes. I hate ST's HALs too
    static I2C_HandleTypeDef s_handle{};
    static bool s_is_initialized{};

    // Forward declarations
    static inline void send_byte(uint8_t byte);
    static inline void send_cmd(uint8_t cmd);
    static inline void send_data(etl::span<const uint8_t> data);
    

    // Public API
    void init() {
        ASSERT(!s_is_initialized);

        // Initialize the I2C bus
        s_handle.Instance = config::LCD_I2C_PORT;
        s_handle.Init.ClockSpeed = 100'000U;
        s_handle.Init.DutyCycle = I2C_DUTYCYCLE_2;
        s_handle.Init.OwnAddress1 = 0;
        s_handle.Init.OwnAddress2 = 0;
        s_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
        s_handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
        s_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
        s_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

        ASSERT(HAL_I2C_Init(&s_handle) == HAL_OK);

        // Initialize the GPIO pins
        __HAL_RCC_GPIOB_CLK_ENABLE();

        GPIO_InitTypeDef pin_init = {
            .Pin = (config::LCD_SDA.pin | config::LCD_SCL.pin),
            .Mode = GPIO_MODE_AF_OD,
            .Pull = GPIO_PULLUP,
            .Speed = GPIO_SPEED_FREQ_HIGH
        };
        HAL_GPIO_Init(config::LCD_SDA.port, &pin_init);

        // Send the initialization sequence to the HD44780 controller


        s_is_initialized = true;
    }

    void deinit() {
        ASSERT(s_is_initialized);

        // Deinitialize the I2C peripheral. Real helpful comment, I know
        ASSERT(HAL_I2C_DeInit(&s_handle) == HAL_OK);
        s_handle = {};

        // Set the pins to analog
        GPIO_InitTypeDef pin_deinit = {
            .Pin = (config::LCD_SDA.pin | config::LCD_SCL.pin),
            .Mode = GPIO_MODE_ANALOG,
            .Pull = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW
        };
        HAL_GPIO_Init(config::LCD_SDA.port, &pin_deinit);
        
        s_is_initialized = false;
    }

    void println(const etl::istring& str, uint8_t line) {
        ASSERT(s_is_initialized);
        ASSERT(line < ROWS);
        ASSERT(str.length() <= COLUMNS);

        for (size_t column{0}; const auto& c : str) {
            put_char(c, column, line);
            column++;
        }
    }

    void put_char(unsigned char c, uint8_t col, uint8_t line) {
        ASSERT(s_is_initialized);
        ASSERT(col < COLUMNS);
        ASSERT(line < ROWS);

    }

    void clear_screen() {
        ASSERT(s_is_initialized);

        // Create string with all whitespaces
        const etl::string<COLUMNS> empty_str(COLUMNS, ' ');

        for (uint8_t i = 0; i < ROWS; i++) {
            println(empty_str, i);
        }
    }
    
    // Helpers
    static inline void send_byte(uint8_t byte) {
        
    }

    static inline void send_cmd(uint8_t cmd) {
        
    }

    static inline void send_data(etl::span<const uint8_t> data) {

    }
    
} // namespace lcd

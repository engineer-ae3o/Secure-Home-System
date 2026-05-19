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
    static void send_byte(uint8_t byte);
    static void send_cmd(uint8_t cmd);
    static void send_data(etl::span<const uint8_t> data);
    

    // Public API
    void init() {
        ASSERT(!s_is_initialized);

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
        

        s_is_initialized = true;
    }

    void deinit() {
        ASSERT(s_is_initialized);
        

        s_is_initialized = false;
    }

    void println(const etl::istring& str, uint8_t line) {
        ASSERT(s_is_initialized);
        ASSERT(line < ROWS);

    }

    void put_char(unsigned char c, uint8_t col, uint8_t line) {
        ASSERT(s_is_initialized);
        ASSERT(col < COLUMNS);
        ASSERT(line < ROWS);

    }
    
    // Helpers
    static void send_byte(uint8_t byte) {
        
    }

    static void send_cmd(uint8_t cmd) {
        
    }

    static void send_data(etl::span<const uint8_t> data) {

    }
    
} // namespace lcd

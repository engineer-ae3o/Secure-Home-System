#include "stm32f1xx_hal.h"

#include "hd44780.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include <array>
#include <string_view>

namespace lcd {

    namespace {

        I2C_HandleTypeDef g_handle{};

        bool g_is_initialized{};

        // Offsets for calculating offset position
        constexpr std::array<uint8_t, ROWS> OFFSETS = {
            0x00U,
            0x40U,
            // Uncomment if ROWS == 4
            // 0x14U, 0x54U
        };

        constexpr uint32_t TIMEOUT_MS = 50;

        // I2C address of the backpack on the HD44780 controller
        constexpr uint8_t ADDRESS = 0x27;

        // Helpers
        utils::error_t send_nibble(uint8_t nibble, uint8_t rs) {
            // BL is Backlight. En is Enable
            // RW is read-write. RS selects data or cmds
            // `(D7 D6 D5 D4)`   `(BL)` `(EN & RW are 0)`  `(RS)`
            uint8_t data = static_cast<uint8_t>(nibble << 4) | (1 << 3) | (rs & 0b1U);

            // Fuck me sideways. ST requires you to shift the address to the left by 1 place
            // For some f***ing reason, it can't be done internally. Not like it's const or some shit
            auto ret = HAL_I2C_Master_Transmit(&g_handle, (ADDRESS << 1), &data, 1, TIMEOUT_MS);
            if (ret != HAL_OK) {
                return utils::error_t::ERR_TIMEOUT;
            }

            // Pulse the EN bit
            data |= static_cast<uint8_t>(1U << 2); // EN high
            ret = HAL_I2C_Master_Transmit(&g_handle, (ADDRESS << 1), &data, 1, TIMEOUT_MS);
            if (ret != HAL_OK) {
                return utils::error_t::ERR_TIMEOUT;
            }

            data &= static_cast<uint8_t>(~(1U << 2)); // EN low
            ret = HAL_I2C_Master_Transmit(&g_handle, (ADDRESS << 1), &data, 1, TIMEOUT_MS);
            if (ret != HAL_OK) {
                return utils::error_t::ERR_TIMEOUT;
            }

            return utils::error_t::NONE;
        }

        utils::error_t send_byte(uint8_t byte, uint8_t rs) {
            TRY(send_nibble((byte >> 4), rs));     // High nibble first
            return send_nibble((byte & 0xFU), rs); // Low nibble next
        }

        utils::error_t send_cmd(uint8_t cmd) {
            // RS = 0 for commands
            return send_byte(cmd, 0);
        }

        utils::error_t send_data(uint8_t data) {
            // RS = 1 for data
            return send_byte(data, 1);
        }

    } // namespace

    // Public API
    utils::error_t init() {
        if (g_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        // Initialize the I2C bus
        __HAL_RCC_I2C1_CLK_ENABLE();

        g_handle.Instance             = config::LCD_I2C_PORT;
        g_handle.Init.ClockSpeed      = 100'000U;
        g_handle.Init.DutyCycle       = I2C_DUTYCYCLE_2;
        g_handle.Init.OwnAddress1     = 0;
        g_handle.Init.OwnAddress2     = 0;
        g_handle.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
        g_handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
        g_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
        g_handle.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

        TRY_HAL(HAL_I2C_Init(&g_handle));

        // Initialize the I2C GPIO pins
        __HAL_RCC_GPIOB_CLK_ENABLE();

        GPIO_InitTypeDef pin_init = {
            .Pin   = static_cast<uint32_t>(config::LCD_SDA.pin | config::LCD_SCL.pin),
            .Mode  = GPIO_MODE_AF_OD,
            .Pull  = GPIO_PULLUP,
            .Speed = GPIO_SPEED_FREQ_HIGH,
        };
        HAL_GPIO_Init(config::LCD_SDA.port, &pin_init);

        GPIO_InitTypeDef led_init = {
            .Pin   = config::LCD_LED.pin,
            .Mode  = GPIO_MODE_OUTPUT_PP,
            .Pull  = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW,
        };
        HAL_GPIO_Init(config::LCD_LED.port, &led_init);

        // Wait 40ms after power on so VCC gets stable
        vTaskDelay(pdMS_TO_TICKS(40));

        // Send the initialization sequence to the HD44780 controller
        // Bloody cheap displays and their stupid timings
        TRY(send_nibble(0x3U, 0U)); // Function set 1
        vTaskDelay(pdMS_TO_TICKS(5));

        TRY(send_nibble(0x3U, 0U)); // Function set 2
        vTaskDelay(pdMS_TO_TICKS(1));

        TRY(send_nibble(0x3U, 0U)); // Function set 3
        vTaskDelay(pdMS_TO_TICKS(1));

        TRY(send_nibble(0x2U, 0U)); // 4 bit mode
        vTaskDelay(pdMS_TO_TICKS(1));

        // Full function set
        TRY(send_cmd(0x28U)); // 4 bit, 2 lines, 5x8 dots
        vTaskDelay(pdMS_TO_TICKS(2));

        TRY(send_cmd(0x08U)); // Display off
        vTaskDelay(pdMS_TO_TICKS(2));

        TRY(send_cmd(0x01U)); // Display clear
        vTaskDelay(pdMS_TO_TICKS(2));

        TRY(send_cmd(0x06U)); // Entry mode
        vTaskDelay(pdMS_TO_TICKS(2));

        TRY(send_cmd(0x0CU)); // Display on
        vTaskDelay(pdMS_TO_TICKS(2));

        g_is_initialized = true;

        return utils::error_t::NONE;
    }

    utils::error_t deinit() {
        if (!g_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        TRY_HAL(HAL_I2C_DeInit(&g_handle));
        g_handle = {};

        // Set the pins to analog
        GPIO_InitTypeDef pin_deinit = {
            .Pin   = static_cast<uint32_t>(config::LCD_SDA.pin | config::LCD_SCL.pin | config::LCD_LED.pin),
            .Mode  = GPIO_MODE_ANALOG,
            .Pull  = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW,
        };
        HAL_GPIO_Init(config::LCD_SDA.port, &pin_deinit);

        g_is_initialized = false;

        return utils::error_t::NONE;
    }

    utils::error_t put_char(unsigned char c, uint8_t col, uint8_t line) {
        if (!g_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        if (col >= COLUMNS || line >= ROWS) {
            return utils::error_t::ERR_INVALID_ARG;
        }

        // Set cursor and send the character
        const uint8_t addr = OFFSETS[line] + col;
        TRY(send_cmd(0x80U | (addr & 0x7FU)));
        TRY(send_data(c));

        return utils::error_t::NONE;
    }

    utils::error_t println(std::string_view str, uint8_t line, bool pad_to_whitespace) {
        if (!g_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        if (str.length() > COLUMNS || line >= ROWS) {
            return utils::error_t::ERR_INVALID_ARG;
        }

        if (str.size() == 0 || str.data() == nullptr) {
            return utils::error_t::ERR_INVALID_ARG;
        }

        // Set cursor to the first column of the row
        const uint8_t addr = OFFSETS[line];
        TRY(send_cmd(0x80U | (addr & 0x7FU)));

        // Now send the string
        for (const auto& c : str) {
            TRY(send_data(c));
        }

        if (pad_to_whitespace) {
            // Pad the remaining columns with whitespaces
            for (auto remaining{str.length()}; remaining < COLUMNS; remaining++) {
                TRY(send_data(' '));
            }
        }

        return utils::error_t::NONE;
    }

    utils::error_t clear_screen() {
        if (!g_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        TRY(send_cmd(0x01U)); // Display clear
        vTaskDelay(pdMS_TO_TICKS(2));

        return utils::error_t::NONE;
    }

    utils::error_t backlight_on(bool on) {
        if (!g_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        HAL_GPIO_WritePin(config::LCD_LED.port, config::LCD_LED.pin, (on ? GPIO_PIN_SET : GPIO_PIN_RESET));

        return utils::error_t::NONE;
    }

} // namespace lcd

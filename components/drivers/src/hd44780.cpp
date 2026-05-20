#include "stm32f1xx_hal.h"
#include "hd44780.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include "etl/string.h"
#include "etl/array.h"


namespace lcd {

    // Global state. Yes. I hate ST's HALs too
    static I2C_HandleTypeDef s_handle{};
    static bool s_is_initialized{};

    // Offsets for calculating offset position
    static constexpr etl::array<uint8_t, ROWS> OFFSETS = {
           0x00U, 0x40U,
        // Uncomment if ROWS == 4
        // 0x14U, 0x54U
    };

    // I2C address of the backpack on the HD44780 controller
    static constexpr uint8_t ADDRESS{0x27U};

    // Forward declarations
    static inline void send_nibble(uint8_t nibble, uint8_t rs);
    static inline void send_byte(uint8_t byte, uint8_t rs);
    static inline void send_cmd(uint8_t cmd);
    static inline void send_data(uint8_t data);
    

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

        // Initialize the I2C GPIO pins
        __HAL_RCC_GPIOB_CLK_ENABLE();

        GPIO_InitTypeDef pin_init = {
            .Pin = (config::LCD_SDA.pin | config::LCD_SCL.pin),
            .Mode = GPIO_MODE_AF_OD,
            .Pull = GPIO_PULLUP,
            .Speed = GPIO_SPEED_FREQ_HIGH
        };
        HAL_GPIO_Init(config::LCD_SDA.port, &pin_init);

        GPIO_InitTypeDef led_init = {
            .Pin = config::LCD_LED.pin,
            .Mode = GPIO_MODE_OUTPUT_PP,
            .Pull = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW
        };
        HAL_GPIO_Init(config::LCD_LED.port, &led_init);
        
        // Wait 40ms after power on so VCC gets stable
        vTaskDelay(pdMS_TO_TICKS(40));

        // Send the initialization sequence to the HD44780 controller
        // Bloody cheap displays and their stupid timings
        send_nibble(0x3U, 0U); vTaskDelay(pdMS_TO_TICKS(5)); // Function set 1
        send_nibble(0x3U, 0U); vTaskDelay(pdMS_TO_TICKS(1)); // Function set 2
        send_nibble(0x3U, 0U); vTaskDelay(pdMS_TO_TICKS(1)); // Function set 3
        send_nibble(0x2U, 0U); vTaskDelay(pdMS_TO_TICKS(1)); // 4 bit mode
        
        // Full function set
        send_cmd(0x28U); vTaskDelay(pdMS_TO_TICKS(2)); // 4 bit, 2 lines, 5x8 dots
        send_cmd(0x08U); vTaskDelay(pdMS_TO_TICKS(2)); // Display off
        send_cmd(0x01U); vTaskDelay(pdMS_TO_TICKS(2)); // Display clear
        send_cmd(0x06U); vTaskDelay(pdMS_TO_TICKS(2)); // Entry mode
        send_cmd(0x0CU); vTaskDelay(pdMS_TO_TICKS(2)); // Display on
        
        s_is_initialized = true;
    }

    void deinit() {
        ASSERT(s_is_initialized);

        // Deinitialize the I2C peripheral. Real helpful comment, I know
        ASSERT(HAL_I2C_DeInit(&s_handle) == HAL_OK);
        s_handle = {};

        // Set the pins to analog
        GPIO_InitTypeDef pin_deinit = {
            .Pin = (config::LCD_SDA.pin | config::LCD_SCL.pin | config::LCD_LED.pin),
            .Mode = GPIO_MODE_ANALOG,
            .Pull = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW
        };
        HAL_GPIO_Init(config::LCD_SDA.port, &pin_deinit);
        
        s_is_initialized = false;
    }

    void put_char(unsigned char c, uint8_t col, uint8_t line) {
        ASSERT(s_is_initialized);
        ASSERT(col < COLUMNS);
        ASSERT(line < ROWS);

        // Set cursor and send the character
        const uint8_t addr = OFFSETS[line] + col;
        send_cmd(0x80U | (addr & 0x7FU));
        send_data(c);
    }

    void println(const etl::string_view& str, uint8_t line, bool pad_to_whitespace) {
        ASSERT(s_is_initialized);
        ASSERT(line < ROWS);
        ASSERT(str.length() <= COLUMNS);

        // Set cursor to the first column of the row
        const uint8_t addr = OFFSETS[line];
        send_cmd(0x80U | (addr & 0x7FU));
        
        // Now send the string
        for (const auto& c : str) {
            send_data(c);
        }
        
        if (pad_to_whitespace) {
            // Pad the remaining columns with whitespaces
            for (auto remaining{str.length()}; remaining < COLUMNS; remaining++) {
                send_data(' ');
            }
        }
    }

    void clear_screen() {
        ASSERT(s_is_initialized);
        send_cmd(0x01U); // Display clear
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    
    void backlight_on(bool on) {
        // Of course I could use `on` directly, but if I wanted no type system, I'd use C
        on ? HAL_GPIO_WritePin(config::LCD_LED.port, config::LCD_LED.pin, GPIO_PIN_SET) :
             HAL_GPIO_WritePin(config::LCD_LED.port, config::LCD_LED.pin, GPIO_PIN_RESET);
    }

    // Helpers
    static inline void send_nibble(uint8_t nibble, uint8_t rs) {
        // BL is Backlight. En is Enable
        // RW is read-write. RS selects data or cmds
                   // `(D7 D6 D5 D4)`   `(BL)` `(EN & RW are 0)`  `(RS)`
        uint8_t data = (nibble << 4) | (1 << 3)                 | (rs & 0b1U);
        // Fuck me sideways. ST requires you to shift the address to the left by 1 place
        // For some f***ing reason, it can't be done internally. Not like it's const or some shit
        HAL_I2C_Master_Transmit(&s_handle, (ADDRESS << 1), &data, 1, HAL_MAX_DELAY);

        // Pulse the EN bit
        data |= static_cast<uint8_t>(1U << 2); // EN high
        HAL_I2C_Master_Transmit(&s_handle, (ADDRESS << 1), &data, 1, HAL_MAX_DELAY);

        data &= static_cast<uint8_t>(~(1U << 2)); // EN low
        HAL_I2C_Master_Transmit(&s_handle, (ADDRESS << 1), &data, 1, HAL_MAX_DELAY);
    }

    static inline void send_byte(uint8_t byte, uint8_t rs) {
        send_nibble((byte >> 4), rs);   // High nibble first
        send_nibble((byte & 0xFU), rs); // Low nibble next
    }

    static inline void send_cmd(uint8_t cmd) {
        // RS = 0 for commands
        send_byte(cmd, 0);
    }

    static inline void send_data(uint8_t data) {
        // RS = 1 for data
        send_byte(data, 1);
    }

} // namespace lcd

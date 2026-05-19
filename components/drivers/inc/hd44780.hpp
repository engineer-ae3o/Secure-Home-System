#pragma once


#include "stm32f1xx_hal.h"
#include "utils.hpp"

#include "etl/string.h"
#include "etl/span.h"


namespace lcd {

    struct config_t {
        I2C_TypeDef* i2c_port{};
        GPIO_TypeDef* gpio_port{};
        uint16_t sda{}, scl{};
    };

    enum class interface_t : uint8_t {
        PARALLEL,
        I2C
    };
    
    template <interface_t interface>
    class hd44780 {
        private:
            bool m_is_initialized{};
            config_t m_config{};

        public:
            hd44780() = default;

            ~hd44780() {
                if (m_is_initialized) deinit();
            }

            hd44780(const hd44780&) = delete;
            hd44780& operator=(const hd44780&) = delete;

            hd44780(hd44780&&) = delete;
            hd44780& operator=(hd44780&&) = delete;

            void init() {
                ASSERT(!m_is_initialized);


                m_is_initialized = true;
            }

            void deinit() {
                ASSERT(m_is_initialized);


                m_is_initialized = false;
            }

            void println() {

            }

            void put_char(unsigned char c, uint8_t row, uint8_t column) {

            }

        private:
            void send_byte(uint8_t byte) {

            }

            void send_cmd(uint8_t cmd) {

            }

            void send_data(etl::span<const uint8_t> data) {

            }
    };
    
} // namespace lcd

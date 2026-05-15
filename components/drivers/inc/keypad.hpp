#pragma once


#include "stm32f101xb.h"
#include "stm32f1xx_hal_gpio.h"

#include "utils.hpp"

#include "FreeRTOS.h"
#include "queue.h"

#include "etl/array.h"
#include "etl/atomic.h"

#include <cstdint>


namespace pad {
    
    struct config_t {
        GPIO_TypeDef* row_port{};
        uint32_t row_a{}, row_b{}, row_c{}, row_d{};

        GPIO_TypeDef* col_port{};
        uint32_t col_a{}, col_b{}, col_c{}, col_d{};
    };

    enum class keys_t : unsigned char {
        KEY_A     = 'A',
        KEY_B     = 'B',
        KEY_C     = 'C',
        KEY_D     = 'D',
        KEY_1     = '1',
        KEY_2     = '2',
        KEY_3     = '3',
        KEY_4     = '4',
        KEY_5     = '5',
        KEY_6     = '6',
        KEY_7     = '7',
        KEY_8     = '8',
        KEY_9     = '9',
        KEY_0     = '0',
        KEY_SHARP = '#',
        KEY_STAR  = '*'
    };

    template <uint8_t queue_size>
    class key_pad_t {
        private:
            etl::atomic_bool m_is_initialized{};
            config_t m_config{};

            QueueHandle_t m_event_queue{};
            etl::array<uint8_t, (queue_size * sizeof(keys_t))> m_queue_buffer{};
            StaticQueue_t m_queue_structure{};

        public:
            key_pad_t(const config_t& config) : m_config(config) {}

            ~key_pad_t() noexcept {
                if (m_is_initialized) deinit();
            };

            key_pad_t(const key_pad_t&) = delete;
            key_pad_t& operator=(const key_pad_t&) = delete;

            key_pad_t(key_pad_t&&) = delete;
            key_pad_t& operator=(key_pad_t&&) = delete;

            void init() {
                ASSERT(!m_is_initialized);

                utils::log<config::log_level_t::INFO>("Initializing the keypad");

                // TODO: Configure the gpios and interrupt handler

                // Can't fail since stack allocated
                m_event_queue = xQueueCreateStatic(queue_size, sizeof(keys_t), m_queue_buffer.data(), m_queue_structure);

                m_is_initialized = true;
                utils::log<config::log_level_t::INFO>("Done initializing the keypad");
            }

            void deinit() {
                if (!m_is_initialized) return;

                m_config = {};
                if (m_event_queue) {
                    // Can't delete since stack allocated
                    m_queue_structure = {};
                    m_queue_buffer = {};
                    m_event_queue = nullptr;
                }

                m_is_initialized = false;
            }

            QueueHandle_t key_pad_t::get_event_queue() const {
                return m_event_queue;
            };
    };
    
} // namespace pad

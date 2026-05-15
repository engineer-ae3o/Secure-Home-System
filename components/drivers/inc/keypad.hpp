#pragma once


#include "stm32f103xb.h"
#include "stm32f1xx_hal.h"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "timers.h"
#include "queue.h"

#include "etl/array.h"

#include <cstdint>


namespace pad {
    
    struct config_t {
        uint32_t row_1{}, row_2{}, row_3{}, row_4{};
        uint32_t col_1{}, col_2{}, col_3{}, col_4{};
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

    template <GPIO_TypeDef* row_port, GPIO_TypeDef* col_port, uint8_t queue_length>
    class key_pad_t {
        private:
            bool m_is_initialized{};
            config_t m_config{};

            QueueHandle_t m_event_queue{};
            etl::array<uint8_t, (queue_length * sizeof(keys_t))> m_queue_buffer{};
            StaticQueue_t m_queue_structure{};

            TimerHandle_t m_debounce_timer{};
            StaticTimer_t m_debounce_timer_structure{};

        public:
            static constexpr uint8_t DEBOUNCE_TIME_MS = 50;

            key_pad_t(const config_t& config) : m_config(config) {}

            ~key_pad_t() noexcept {
                if (m_is_initialized) deinit();
            };

            key_pad_t(const key_pad_t&) = delete;
            key_pad_t& operator=(const key_pad_t&) = delete;

            key_pad_t(key_pad_t&&) = delete;
            key_pad_t& operator=(key_pad_t&&) = delete;

            /**
             * @brief Initializes and configures the gpio pins according to how
             *        they will be used for row scanning.
             * 
             * @note User still has to enable and set the priority of the interrupt
             *       handler being used with NVIC, as well as clear the interrupt pending
             *       flag in `EXTI->PR`
             */
            void init() {
                ASSERT(!m_is_initialized);

                // Configure the clocks
                if constexpr      (col_port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
                else if constexpr (col_port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
                else if constexpr (col_port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
                else if constexpr (col_port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
                else static_assert(0);

                if constexpr      (row_port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
                else if constexpr (row_port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
                else if constexpr (row_port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
                else if constexpr (row_port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
                else static_assert(0);

                // Set all columns as input pullups with interrupts
                GPIO_InitTypeDef col_init = {
                    .Pin = (m_config.col_a | m_config.col_b | m_config.col_c | m_config.col_d),
                    .Mode = GPIO_MODE_INPUT,
                    .Pull = GPIO_PULLUP,
                    .Speed = GPIO_SPEED_FREQ_LOW
                };
                HAL_GPIO_Init(col_port, &col_init);

                EXTI

                // Set all rows as output push pull
                GPIO_InitTypeDef row_init = {
                    .Pin = (m_config.row_a | m_config.row_b | m_config.row_c | m_config.row_d),
                    .Mode = GPIO_MODE_OUTPUT_PP,
                    .Pull = GPIO_NOPULL,
                    .Speed = GPIO_SPEED_FREQ_LOW
                };
                HAL_GPIO_Init(row_port, &row_init);

                // Set all pins high by default
                HAL_GPIO_WritePin(row_port, m_config.row_a, GPIO_PIN_SET);
                HAL_GPIO_WritePin(row_port, m_config.row_b, GPIO_PIN_SET);
                HAL_GPIO_WritePin(row_port, m_config.row_c, GPIO_PIN_SET);
                HAL_GPIO_WritePin(row_port, m_config.row_d, GPIO_PIN_SET);

                // Can't fail since stack allocated
                m_event_queue = xQueueCreateStatic(queue_length, sizeof(keys_t), m_queue_buffer.data(), m_queue_structure);
                m_debounce_timer = xTimerCreateStatic("Debounce_timer", pdMS_TO_TICKS(DEBOUNCE_TIME_MS), pdFALSE,
                                                       this, debounce_timer_cb, &m_debounce_timer_structure);

                m_is_initialized = true;
            }

            /**
             * @brief Deinitializes the gpio pins used and sets the pins to analog
             *        mode so as to reduce power consumption
             */
            void deinit() {
                ASSERT(m_is_initialized);

                // Sets all pins as analog to reduce power draw
                GPIO_InitTypeDef col_init = {
                    .Pin = (m_config.col_a | m_config.col_b | m_config.col_c | m_config.col_d),
                    .Mode = GPIO_MODE_ANALOG,
                    .Pull = GPIO_NOPULL,
                    .Speed = GPIO_SPEED_FREQ_LOW
                };
                HAL_GPIO_Init(col_port, &col_init);
                
                GPIO_InitTypeDef row_init = {
                    .Pin = (m_config.row_a | m_config.row_b | m_config.row_c | m_config.row_d),
                    .Mode = GPIO_MODE_ANALOG,
                    .Pull = GPIO_NOPULL,
                    .Speed = GPIO_SPEED_FREQ_LOW
                };
                HAL_GPIO_Init(row_port, &row_init);

                m_config = {};

                // Can't delete since stack allocated
                // So, just zero the memory and mark as unused
                if (m_event_queue) {
                    m_queue_structure = {};
                    m_queue_buffer = {};
                    m_event_queue = nullptr;
                }

                if (m_debounce_timer) {
                    m_debounce_timer_structure = {};
                    m_debounce_timer = nullptr;
                }

                m_is_initialized = false;
            }

            /**
             * 
             */
            QueueHandle_t get_event_queue() const {
                return m_event_queue;
            };

            /**
             * 
             */
            void irq_handler() {
                // Clear the interrupt pending flags on all pins
                __HAL_GPIO_EXTI_CLEAR_FLAG(m_config.col_a | m_config.col_b | m_config.col_c | m_config.col_d);
                
                // And disable interrupts globally on the port
                // to be reenabled by the debounce timer
                __HAL_GPIO_EXTI_CLEAR_IT(m_config.col_a | m_config.col_b | m_config.col_c | m_config.col_d);

                // Start the debounce timer
                BaseType_t higher_priority_task_woken = pdFALSE;
                xTimerStartFromISR(m_debounce_timer, &higher_priority_task_woken);
            }

            static void debounce_timer_cb(TimerHandle_t xTimer) {

            }
    };
    
} // namespace pad

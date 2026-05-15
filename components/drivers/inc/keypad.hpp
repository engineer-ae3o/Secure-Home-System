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
        uint32_t row_a_pin{}, row_b_pin{}, row_c_pin{}, row_d_pin{};
        uint32_t col_a_pin{}, col_b_pin{}, col_c_pin{}, col_d_pin{};
    };
    
    template <GPIO_TypeDef* row_port, GPIO_TypeDef* col_port, uint8_t queue_length>
    class key_pad_t {
        private:
            bool m_is_initialized{};
            config_t m_config{};

            QueueHandle_t m_event_queue{};
            etl::array<uint8_t, (queue_length * sizeof(KEYS[0][0]))> m_queue_buffer{};
            StaticQueue_t m_queue_structure{};

            TimerHandle_t m_debounce_timer{};
            StaticTimer_t m_debounce_timer_structure{};
            
            static constexpr uint8_t DEBOUNCE_TIME_MS = 50;

            // Indexed as `KEYS[row][column]`
            static constexpr unsigned char KEYS[4][4] = {
                { '1', '2', '3', 'A' },
                { '4', '5', '6', 'B' },
                { '7', '8', '9', 'C' },
                { '*', '0', '#', 'D' },
            };

        public:
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
             *        they will be used for the keypad scanning.
             * 
             * @note The interrupts still have to be enabled with NVIC
             */
            void init() {
                ASSERT(!m_is_initialized);

                // Configure the clocks
                if constexpr      (col_port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
                else if constexpr (col_port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
                else if constexpr (col_port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
                else if constexpr (col_port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
                else static_assert(false);

                if constexpr      (row_port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
                else if constexpr (row_port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
                else if constexpr (row_port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
                else if constexpr (row_port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
                else static_assert(false);
                
                // Set all columns as input pullups with interrupt on falling edge
                GPIO_InitTypeDef col_init = {
                    .Pin = (m_config.col_a_pin | m_config.col_b_pin | m_config.col_c_pin | m_config.col_d_pin),
                    .Mode = GPIO_MODE_IT_FALLING,
                    .Pull = GPIO_PULLUP,
                    .Speed = GPIO_SPEED_FREQ_LOW
                };
                HAL_GPIO_Init(col_port, &col_init);
                
                // Set all rows as output push pull
                GPIO_InitTypeDef row_init = {
                    .Pin = (m_config.row_a_pin | m_config.row_b_pin | m_config.row_c_pin | m_config.row_d_pin),
                    .Mode = GPIO_MODE_OUTPUT_PP,
                    .Pull = GPIO_NOPULL,
                    .Speed = GPIO_SPEED_FREQ_LOW
                };
                HAL_GPIO_Init(row_port, &row_init);

                // Set all row pins high by default
                HAL_GPIO_WritePin(row_port, m_config.row_a_pin, GPIO_PIN_SET);
                HAL_GPIO_WritePin(row_port, m_config.row_b_pin, GPIO_PIN_SET);
                HAL_GPIO_WritePin(row_port, m_config.row_c_pin, GPIO_PIN_SET);
                HAL_GPIO_WritePin(row_port, m_config.row_d_pin, GPIO_PIN_SET);

                // Can't fail since stack allocated
                m_event_queue = xQueueCreateStatic(queue_length, sizeof(KEYS[0][0]), m_queue_buffer.data(), m_queue_structure);
                m_debounce_timer = xTimerCreateStatic("Debounce_timer", pdMS_TO_TICKS(DEBOUNCE_TIME_MS), pdFALSE,
                                                       this, debounce_timer_cb, &m_debounce_timer_structure);

                m_is_initialized = true;
            }

            /**
             * @brief Deinitializes the gpio pins used and sets the pins to
             *        analog mode so as to reduce power consumption
             */
            void deinit() {
                ASSERT(m_is_initialized);

                // Sets all pins as analog to reduce power draw
                GPIO_InitTypeDef col_init = {
                    .Pin = (m_config.col_a_pin | m_config.col_b_pin | m_config.col_c_pin | m_config.col_d_pin),
                    .Mode = GPIO_MODE_ANALOG,
                    .Pull = GPIO_NOPULL,
                    .Speed = GPIO_SPEED_FREQ_LOW
                };
                HAL_GPIO_Init(col_port, &col_init);
                
                GPIO_InitTypeDef row_init = {
                    .Pin = (m_config.row_a_pin | m_config.row_b_pin | m_config.row_c_pin | m_config.row_d_pin),
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
                    xTimerStop(m_debounce_timer, portMAX_DELAY);
                    m_debounce_timer_structure = {};
                    m_debounce_timer = nullptr;
                }

                m_is_initialized = false;
            }

            /**
             * @brief Returns the queue in which keypad events are
             *        pushed into. Pretty straightforward.
             * 
             * @return The event queue
             */
            QueueHandle_t get_event_queue() const {
                ASSERT(m_is_initialized);
                return m_event_queue;
            };

            /**
             * @brief This function should be called from all the NVIC
             *        interrupt handlers for the used gpio pins
             */
            void irq_handler() {
                // Clear the interrupt pending flags on all pins
                __HAL_GPIO_EXTI_CLEAR_IT(m_config.col_a_pin | m_config.col_b_pin | m_config.col_c_pin | m_config.col_d_pin);
                
                // And disable interrupts globally on the port by masking it.
                // Will be reenabled by the debounce timer after it's done scanning
                EXTI->IMR &= ~(m_config.col_a_pin | m_config.col_b_pin | m_config.col_c_pin | m_config.col_d_pin);

                // Start the debounce timer
                BaseType_t higher_priority_task_woken = pdFALSE;
                xTimerStartFromISR(m_debounce_timer, &higher_priority_task_woken);
                portYIELD_FROM_ISR(higher_priority_task_woken);
            }

        private:
            static void debounce_timer_cb(TimerHandle_t xTimer) {
                // Get timer ID
                const key_pad_t* keypad = static_cast<key_pad_t*>(pvTimerGetTimerID(xTimer));
                
                // Keypad scanning
                uint8_t row{}, column{};
                
                // Send only first detected keypad press to queue
                xQueueSend(keypad->m_event_queue, &KEYS[row][column], portMAX_DELAY);

                // Clear the interrupt pending flags on all pins before unmasking the EXTI interrupts
                __HAL_GPIO_EXTI_CLEAR_IT(keypad->m_config.col_a_pin | keypad->m_config.col_b_pin |
                                         keypad->m_config.col_c_pin | keypad->m_config.col_d_pin);
                
                // Enable interrupts by unmasking EXTI interrupts
                EXTI->IMR |= (keypad->m_config.col_a_pin | keypad->m_config.col_b_pin |
                              keypad->m_config.col_c_pin | keypad->m_config.col_d_pin);
            }
    };
    
} // namespace pad

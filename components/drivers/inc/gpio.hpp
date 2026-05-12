#pragma once


#include "stm32f103xb.h"
#include "common.hpp"

#include <cstdint>


namespace gpio {

    enum class speed_t : uint8_t {
        LOW_SPEED    = 0x00U,
        MEDIUM_SPEED = 0x01U,
        FAST_SPEED   = 0x02U,
        HIGH_SPEED   = 0x03U
    };

    enum class edge_t : uint8_t {
        NO_EDGE                 = 0x00U,
        RISING_EDGE_ONLY        = 0x01U,
        FALLING_EDGE_ONLY       = 0x02U,
        RISING_AND_FALLING_EDGE = 0x03U
    };

    enum class output_t : uint8_t {
        PUSH_PULL  = 0x00U,
        OPEN_DRAIN = 0x01U,
    };

    enum class port_t : uint8_t { A, B, C, D };

    template <port_t port, uint8_t pin>
    struct gpio_pin_t {

        static constexpr GPIO_Typedef* port() {
            if constexpr (port == port_t::A) return GPIOA;
            else if constexpr (port == port_t::B) return GPIOB;
            else if constexpr (port == port_t::C) return GPIOC;
            else if constexpr (port == port_t::D) return GPIOD;
            else return nullptr;
        }
        
        template <bool enable>
        hal_err_t clk_enable(GPIO_TypeDef* handle) {
            return hal_err_t::HAL_OK;
        }
    
        void set_output(GPIO_TypeDef* port, uint8_t pin);
        void set_input(GPIO_TypeDef* port, uint8_t pin);
        void set_analog(GPIO_TypeDef* port, uint8_t pin);
        hal_err_t set_alternate_function(GPIO_TypeDef* port, uint8_t pin, uint8_t alt_val);
    
        template <bool enable>
        void enable_pullup(GPIO_TypeDef* port, uint8_t pin) {
        
        }
        
        template <bool enable>
        void enable_pulldown(GPIO_TypeDef* port, uint8_t pin) {
        
        }
    
        void set_speed_mode(GPIO_TypeDef* port, uint8_t pin, speed_t mode);
        void set_output_type(GPIO_TypeDef* port, uint8_t pin, output_t type);
    
        void level_set(GPIO_TypeDef* port, uint8_t pin, bool level);
        void level_toggle(GPIO_TypeDef* port, uint8_t pin);
        bool get_level(GPIO_TypeDef* port, uint8_t pin);
    
        hal_err_t set_interrupt(GPIO_TypeDef* port, uint8_t pin, edge_t edge);
        void clear_interrupt(GPIO_TypeDef* port, uint8_t pin);

    };

} // namespace gpio

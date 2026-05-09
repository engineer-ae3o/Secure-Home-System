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
        
    };

} // namespace gpio

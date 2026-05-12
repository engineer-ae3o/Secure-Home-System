#include "gpio.hpp"


namespace gpio {
    
    void set_output(GPIO_TypeDef* port, uint8_t pin) {

    }

    void set_input(GPIO_TypeDef* port, uint8_t pin) {

    }

    void set_analog(GPIO_TypeDef* port, uint8_t pin) {

    }

    hal_err_t set_alternate_function(GPIO_TypeDef* port, uint8_t pin, uint8_t alt_val) {
        return hal_err_t::HAL_OK;
    }
    
    void set_speed_mode(GPIO_TypeDef* port, uint8_t pin, speed_t mode) {

    }

    void set_output_type(GPIO_TypeDef* port, uint8_t pin, output_t type) {

    }
    
    void level_set(GPIO_TypeDef* port, uint8_t pin, bool level) {

    }

    void level_toggle(GPIO_TypeDef* port, uint8_t pin) {

    }

    bool get_level(GPIO_TypeDef* port, uint8_t pin) {

    }
    
    hal_err_t set_interrupt(GPIO_TypeDef* port, uint8_t pin, edge_t edge) {
        return hal_err_t::HAL_OK;
    }

    void clear_interrupt(GPIO_TypeDef* port, uint8_t pin) {

    }
    
} // namespace gpio

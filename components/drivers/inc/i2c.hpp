#pragma once


#include "stm32f103xb.h"
#include "common.hpp"

#include <cstddef>
#include <cstdint>

#include "etl/span.h"


namespace i2c {

    enum class freq_t : uint8_t {
        FREQ_100KHz = 0,
        FREQ_400KHz
    };

    struct master_config_t {
        bool use_pullup;
        freq_t freq_type;
        
        uint8_t sda;
        uint8_t scl;
        GPIO_TypeDef* gpio_port;
    };

    hal_err_t clk_enable(I2C_TypeDef* handle, bool enable);
    hal_err_t master_init(I2C_TypeDef* handle, const master_config_t& config);

    hal_err_t master_transmit(I2C_TypeDef* handle, uint8_t address, etl::span<const uint8_t> data);
    hal_err_t master_receive(I2C_TypeDef* handle, uint8_t address, etl::span<uint8_t> data);
    hal_err_t master_transceive(I2C_TypeDef* handle, uint8_t address, etl::span<const uint8_t> tx_data, etl::span<uint8_t> rx_data);
    
} // namespace i2c

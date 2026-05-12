#include "i2c.hpp"


namespace i2c {
    
    hal_err_t clk_enable(I2C_TypeDef* handle, bool enable) {
        return hal_err_t::HAL_OK;
    }

    hal_err_t master_init(I2C_TypeDef* handle, const master_config_t& config) {
        return hal_err_t::HAL_OK;
    }
    
    hal_err_t master_transmit(I2C_TypeDef* handle, uint8_t address, etl::span<const uint8_t> data) {
        return hal_err_t::HAL_OK;
    }

    hal_err_t master_receive(I2C_TypeDef* handle, uint8_t address, etl::span<uint8_t> data) {
        return hal_err_t::HAL_OK;
    }

    hal_err_t master_transceive(I2C_TypeDef* handle, uint8_t address, etl::span<const uint8_t> tx_data, etl::span<uint8_t> rx_data) {
        return hal_err_t::HAL_OK;
    }

} // namespace i2c

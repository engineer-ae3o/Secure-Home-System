#pragma once


#include "etl/string.h"
#include "etl/expected.h"


namespace gsm {

    enum class status_t : uint8_t {
        OK,
        SIM_NOT_FOUND,
    };
    
    status_t init();

    void deinit();

    status_t send_sms();

    status_t get_sim_status();

    etl::expected<uint32_t, status_t> get_imsi();

} // namespace gsm

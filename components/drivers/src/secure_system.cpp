#include "stm32f1xx_hal.h"

#include "secure_system.hpp"
#include "config.hpp"
#include "flash.hpp"
#include "utils.hpp"

#include "ascon/random.h"

#include <cstdint>

extern "C" {
    //int ascon_trng_get_bytes(const unsigned char* out, std::size_t outlen) {
    //    (void)out;
    //    (void)outlen;
    //    return 0;
    //}
} // extern "C"

namespace ss {

    void init() {
    }

    void deinit() {
    }

    int get_random_number() {
        return 0;
    }

} // namespace ss

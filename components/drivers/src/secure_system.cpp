#include <cstdint>


extern "C" {
    int ascon_trng_get_bytes(unsigned char *out, std::size_t outlen) {
        (void)out;
        (void)outlen;
        return 0;
    }
}

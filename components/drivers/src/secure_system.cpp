#include "stm32f1xx_hal.h"

#include "file.hpp"
#include "csprng.hpp"
#include "config.hpp"
#include "sim800l.hpp"
#include "secure_system.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace ss {

    namespace {

        constexpr uint32_t ASCON_DIGEST_LEN_BYTES = 32;
        constexpr uint32_t SALT_LEN_BYTES         = 32;
        constexpr uint32_t AUTH_TAG_LEN_BYTES     = 16;
        constexpr uint32_t NONCE_LEN_BYTES        = 16;

        // The salt will be randomly generated at first boot
        // since only one password is being used at a time.
        struct password_file_data_t {
            std::array<uint8_t, SALT_LEN_BYTES>         salt{};
            std::array<uint8_t, ASCON_DIGEST_LEN_BYTES> password_digest{};
        };

        // All the phone numbers are encrypted as a single blob. The nonce is generated, used for
        // encryption, written to flash, on next boot, is fetched, used to decrypt the data, and
        // is discarded. A new nonce will be generated to prevent nonce reuse.
        struct pnumbers_file_data_t {
            std::array<uint8_t, NONCE_LEN_BYTES>    nonce{};
            std::array<uint8_t, AUTH_TAG_LEN_BYTES> auth_tag{};

            std::array<std::array<uint8_t, gsm::MAX_PHONE_NUMBER_LEN>, MAX_PNUMBERS> pnumbers{};
        };

        // Data storage for the files being used
        [[maybe_unused]] password_file_data_t g_pswd_file_storage{};
        [[maybe_unused]] pnumbers_file_data_t g_pnumbers_file_storage{};

    } // namespace

    utils::error_t init() {
        TRY(rnd::init());

        return utils::error_t::NONE;
    }

    utils::error_t deinit() {
        TRY(rnd::deinit());

        return utils::error_t::NONE;
    }

    void add_pnumber() {
    }

    void remove_pnumber() {
    }

    bool cmp_password() {
        return true;
    }

    void update_password() {
    }

} // namespace ss

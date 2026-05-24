#pragma once

#include "etl/array.h"
#include "etl/expected.h"
#include "etl/string_view.h"

namespace gsm {

    enum class error_t : uint8_t {
        NONE,
        FAIL,
        SIM_NOT_FOUND,
        SIM_NOT_REGISTERED,
        MODULE_NOT_ALIVE,
        BAD_NETWORK_CONN,
    };

    /**
     * @brief It initializes the GPIO, UART and GPIO peripherals as needed for
     *        communication with the SIM800L. It then performs a bunch of status
     *        checks on the GSM module and the SIM card to ensure they are suitable
     *        to use for operation. It polls the module every 5s till a good network
     *        connnection has been established with a network provider.
     * 
     * @return `NONE`: The GSM module found the SIM card, has connected to a network
     *                 provider and has a good connection with it.
     *         `FAIL`: Generic error from the GSM module.
     *         `MODULE_NOT_ALIVE`: GSM module not responding.
     *         `SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     *         `SIM_NOT_REGISTERED`: SIM card not registered to a network service.
     *         `BAD_NETWORK_CONN`: The SIM card has a poor network connection.
     * 
     * @note It blocks the calling task for up to 30s on the worst case path
     *       while waiting for the `NONE` status message from the GSM module.
     */
    [[nodiscard]] error_t init();

    /**
     * @brief Deinitializes the UART and DMA peripherals, as well as
     *        sets the GPIOs used to analog mode to reduce power draw.
     */
    void deinit();

    /**
     * @brief Checks if the SIM card is still present in the GSM module and can
     *        be read; and if there's still a connection between the module and
     *        the network tower. It returns immediately after checking all the
     *        available statuses.
     * 
     * @return `NONE`: The GSM module sees and can read the SIM card and still has
     *                 a stable connection to the network tower.
     *         `MODULE_NOT_ALIVE`: GSM module not responding.
     *         `SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     *         `SIM_NOT_REGISTERED`: SIM card not registered to a network service.
     *         `BAD_NETWORK_CONN`: The SIM card has a poor network connection.
     */
    [[nodiscard]] error_t get_sim_status();

    /**
     * @brief Sends an SMS to the given phone number.
     * 
     * @param[in] sms    SMS to be sent.
     * @param[in] number Phone number to send the SMS to.
     * 
     * @return `NONE`: The SMS was sent successfully.
     *         `FAIL`: Generic failure from the GSM module.
     *         `MODULE_NOT_ALIVE`: GSM module not responding.
     *         `SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     *         `SIM_NOT_REGISTERED`: SIM card not registered to a network service.
     *         `BAD_NETWORK_CONN`: The SIM card has a poor network connection.
     */
    [[nodiscard]] error_t send_sms(const etl::string_view& sms, const etl::string_view& number);

    /**
     * @brief Reads the IMSI (International Mobile Subscriber Identity) of the
     *        SIM card and returns it. Pretty straightforward. The IMSI is a 15
     *        digit UID used by mobile network providers for identifying and
     *        authenticating SIM cards and subscribers.
     * 
     * @return The IMSI as an `etl::array<char>` on sucess. On error,
     *         it returns any of the following:
     *         `FAIL`: IMSI could not be read, or was read with errors.
     *         `MODULE_NOT_ALIVE`: GSM module not responding.
     *         `SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     * 
     * @note The digits are stored as ASCII, not numeric digits.
     */
    [[nodiscard]] etl::expected<etl::array<char, 16>, error_t> get_imsi();

} // namespace gsm

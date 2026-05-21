#pragma once


#include "etl/array.h"
#include "etl/string.h"
#include "etl/expected.h"


namespace gsm {

    [[nodiscard]] enum class status_t : uint8_t {
        OK,
        ERR_GENERIC,
        ERR_SIM_NOT_FOUND,
        ERR_COULD_NOT_CONNECT
    };
    
    /**
     * @brief Initializes the UART, DMA and GPIO peripherals, as well as
     *        polls the GSM module every 5s till it responds with an `OK`
     *        message. It times out after 30s and returns the timeout.
     *        Otherwise, it returns the state of GSM module, the SIM card
     *        or network registration status on an error.
     * 
     * @return `OK`: The GSM module found the SIM card and has connected to
     *                a network tower.
     *         `ERR_GENERIC`: Generic error from the GSM module.
     *         `ERR_SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     *         `ERR_COULD_NOT_CONNECT`: The GSM module could not connnect to a
     *                                   network tower.
     * 
     * @note It blocks the calling task for up to 30s on the worst case path
     *       while waiting for the `OK` status message from the GSM module.
     */
    status_t init();

    /**
     * @brief Deinitializes the UART and DMA peripherals, as well as
     *        sets the GPIOs used to analog mode to reduce power draw.
     */
    void deinit();

    /**
     * @brief Sends an SMS to the given phone number.
     * 
     * @param[in] sms    SMS to be sent.
     * @param[in] number Phone number to send the SMS to.
     * 
     * @return `OK`: The SMS was sent successfully.
     *         `ERR_GENERIC`: Generic error from the GSM module.
     *         `ERR_SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     *         `ERR_COULD_NOT_CONNECT`: The GSM module lost connection to the
     *                                    network tower.
     */
    status_t send_sms(const etl::string_view& sms, const etl::string_view& number);

    /**
     * @brief Checks if the SIM card is still present in the GSM module and can
     *        be read; and if there's still a connection between the module and
     *        the network tower. It returns immediately after checking all the
     *        available statuses.
     * 
     * @return `OK`: The GSM module sees and can read the SIM card and still has
     *                a stable connection to the network tower.
     *         `ERR_GENERIC`: Generic error from the GSM module.
     *         `ERR_SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     *         `ERR_COULD_NOT_CONNECT`: The GSM module lost connection to the
     *                                   network tower.
     */
    status_t get_sim_status();

    /**
     * @brief Reads the IMSI (International Mobile Subscriber Identity) of the
     *        SIM card and returns it. Pretty straightforward. The IMSI is a 15
     *        digit UID used by mobile network providers for identifying and
     *        authenticating SIM cards and subscribers.
     * 
     * @return The IMSI on sucess. On error, returns any of the following:
     *         `ERR_GENERIC`: Generic error from the GSM module.
     *         `ERR_SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     * 
     * @note The digits are stored as ASCII, not numeric digits.
     */
    [[nodiscard]] etl::expected<etl::array<char, 16>, status_t> get_imsi();

} // namespace gsm

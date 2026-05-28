#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string_view>

namespace gsm {

    enum class error_t : uint8_t {
        NONE,
        FAIL,
        SIM_NOT_FOUND,
        SIM_NOT_REGISTERED,
        MODULE_NOT_ALIVE,
        BAD_NETWORK_CONN,
        MUTEX_TIMEOUT,
        SMS_SEND_FAIL,
    };

    constexpr inline uint8_t MAX_SMS_LEN{64};
    constexpr inline uint8_t IMSI_BUF_SIZE{16};
    constexpr inline uint8_t MAX_PHONE_NUMBER_LEN{11};

    /**
     * @brief It initializes the GPIO, UART and DMA peripherals as needed for
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
     * @note It blocks the calling task for up to (30 + 2.5)s on the worst case path
     *       while waiting for the first `"OK"` from the module and waiting for a good
     *       and useable network connection. This function is not thread safe.
     */
    [[nodiscard]] error_t init();

    /**
     * @brief Deinitializes the UART and DMA peripherals, as well as
     *        sets the GPIOs used to analog mode to reduce power draw.
     * 
     * @note This function is not thread safe.
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
     *         `MUTEX_TIMEOUT`: Timeout waiting for the mutex.
     */
    [[nodiscard]] error_t get_sim_status();

    /**
     * @brief Sends an SMS to the given phone number.
     * 
     * @param[in] sms              SMS to be sent.
     * @param[in] number           Phone number to send the SMS to.
     * @param[in] check_sim_status Determines whether the function checks the SIM's
     *                             card's status before attempting to send the SMS.
     * 
     * @return `NONE`: The SMS was sent successfully.
     *         `FAIL`: Generic failure from the GSM module.
     *         `MODULE_NOT_ALIVE`: GSM module not responding.
     *         `SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     *         `SIM_NOT_REGISTERED`: SIM card not registered to a network service.
     *         `BAD_NETWORK_CONN`: The SIM card has a poor network connection.
     *         `MUTEX_TIMEOUT`: Timeout waiting for the mutex.
     *         `SMS_SEND_FAIL`: Failed to send the SMS due to some specific error.
     */
    [[nodiscard]] error_t send_sms(const std::string_view& sms, const std::string_view& number, bool check_sim_status = true);

    /**
     * @brief Reads the IMSI (International Mobile Subscriber Identity) of the
     *        SIM card and returns it. Pretty straightforward. The IMSI is a 15
     *        digit UID used by mobile network providers for identifying and
     *        authenticating SIM cards and subscribers.
     * 
     * @return The IMSI as an `std::array<char>` on sucess. On error,
     *         it returns any of the following:
     *         `FAIL`: IMSI could not be read, or was read with errors.
     *         `MODULE_NOT_ALIVE`: GSM module not responding.
     *         `SIM_NOT_FOUND`: The GSM module couldn't find the SIM card.
     *         `MUTEX_TIMEOUT`: Timeout waiting for the mutex.
     * 
     * @note The digits are stored as ASCII, not numeric digits.
     */
    [[nodiscard]] std::expected<std::array<char, IMSI_BUF_SIZE>, error_t> get_imsi();

} // namespace gsm

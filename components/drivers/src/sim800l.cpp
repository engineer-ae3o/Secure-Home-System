#include "stm32f1xx_hal.h"

#include "sim800l.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "etl/utility.h"
#include "etl/string.h"

#include <string_view>
#include <cstring>

namespace gsm {

    // Global state. It is what it is
    static UART_HandleTypeDef s_huart{};
    static DMA_HandleTypeDef  s_hdma_tx{};
    static DMA_HandleTypeDef  s_hdma_rx{};
    static TaskHandle_t       s_calling_task_handle{};
    static SemaphoreHandle_t  s_task_mutex{};
    static StaticSemaphore_t  s_task_mutex_buffer{};

    static bool s_is_initialized{};

    // This is needed because we are receiving UART data, we don't
    // know the length of the data we will get. The UART idle line
    // ISR puts the actual length of the data received here.
    static volatile uint16_t s_rx_idle_line_size{};

    static constexpr uint32_t UART_IDLE_LINE_BUF_BYTE{32};

    static constexpr uint32_t NUM_OF_TIMES_TO_POLL_SIGNAL_CHECK{6};
    static constexpr uint32_t DELAY_BETWEEN_SIGNAL_CHECK_POLL_MS{5000};

    static constexpr uint32_t NUM_OF_TIMES_TO_SEND_AT{10};
    static constexpr uint32_t DELAY_BETWEEN_TX_AT_CMDS_MS{250};

    static constexpr uint32_t TIMEOUT_MS{2000};

    // RAII helper for cleaning up stale used state
    struct cleanup_t {
        cleanup_t() = default;

        ~cleanup_t() {
            s_rx_idle_line_size   = {};
            s_calling_task_handle = {};
        }

        cleanup_t(const cleanup_t&)            = delete;
        cleanup_t& operator=(const cleanup_t&) = delete;
        cleanup_t(cleanup_t&&)                 = delete;
        cleanup_t& operator=(cleanup_t&&)      = delete;
    };

    // RAII helper for taking and freeing the mutex
    struct mutex_t {
    public:
        mutex_t(bool& mutex_taken) : m_mutex_taken(xSemaphoreTake(s_task_mutex, pdMS_TO_TICKS(TIMEOUT_MS)) == pdTRUE) {
            mutex_taken = m_mutex_taken;
        }

        ~mutex_t() {
            if (m_mutex_taken) {
                xSemaphoreGive(s_task_mutex);
            }
        }

        mutex_t(const mutex_t&)            = delete;
        mutex_t& operator=(const mutex_t&) = delete;
        mutex_t(mutex_t&&)                 = delete;
        mutex_t& operator=(mutex_t&&)      = delete;

    private:
        bool m_mutex_taken{};
    };

    enum class cmd_t : uint8_t {
        // Initialization
        AT,        // Module alive check
        ECHO_OFF,  // Disable echo mode
        TEXT_MODE, // SMS text mode
        SET_SMSC,  // GLO SMSC

        // Status checks
        CHECK_SIM,    // Check if SIM card is present and ready
        CHECK_REG,    // Network registration
        CHECK_SIGNAL, // Check signal strength

        // IMSI
        GET_IMSI, // Get the SIM card's IMSI

        // Cleanup
        DEINIT, // Tell the SIM800L to deinitialize itself

        // Total
        COUNT // Used to get total number for array declaration
    };

    // `etl::string_view` doesn't have the right constructor
    // to take in a `const char*` on the fly unless a length is specified
    struct cmd_entry_t {
        std::string_view tx;
        std::string_view rx_expected;
    };

    // AT commands LUT
    static constexpr etl::array<cmd_entry_t, std::to_underlying(cmd_t::COUNT)> AT_CMD_LUT = {{
        [std::to_underlying(cmd_t::AT)]           = {"AT\r", "OK"},
        [std::to_underlying(cmd_t::ECHO_OFF)]     = {"ATE0\r", "OK"},
        [std::to_underlying(cmd_t::TEXT_MODE)]    = {"AT+CMGF=1\r", "OK"},
        [std::to_underlying(cmd_t::SET_SMSC)]     = {"AT+CSCA=\"+2348050020020\"\r", "OK"},
        [std::to_underlying(cmd_t::CHECK_SIM)]    = {"AT+CPIN?\r", "+CPIN: READY"},
        [std::to_underlying(cmd_t::CHECK_REG)]    = {"AT+CREG?\r", "+CREG"},
        [std::to_underlying(cmd_t::CHECK_SIGNAL)] = {"AT+CSQ\r", "+CSQ"},
        [std::to_underlying(cmd_t::GET_IMSI)]     = {"AT+CIMI\r", "OK"},
        [std::to_underlying(cmd_t::DEINIT)]       = {"AT+CPOWD=1\r", "NORMAL POWER DOWN"},
    }};

    // Forward declarations
    [[nodiscard]] static inline error_t send_cmd_and_compare_result(cmd_t cmd);
    [[nodiscard]] static inline error_t send_init_sequence();
    [[nodiscard]] static inline etl::expected<etl::string_view, error_t>
    transact(const etl::string_view& tx_cmd, etl::array<char, UART_IDLE_LINE_BUF_BYTE>& rx_cmd, uint32_t timeout_ms = TIMEOUT_MS);

    // Public API
    error_t init() {
        utils::assert_check(!s_is_initialized);

        // Configure the GPIOs
        __HAL_RCC_GPIOA_CLK_ENABLE();

        // TX
        GPIO_InitTypeDef tx_init = {
            .Pin   = config::GSM_GPIO_TX.pin,
            .Mode  = GPIO_MODE_AF_PP,
            .Pull  = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW,
        };
        HAL_GPIO_Init(config::GSM_GPIO_TX.port, &tx_init);

        // RX
        GPIO_InitTypeDef rx_init = {
            .Pin   = config::GSM_GPIO_RX.pin,
            .Mode  = GPIO_MODE_INPUT,
            .Pull  = GPIO_PULLUP,
            .Speed = GPIO_SPEED_FREQ_LOW,
        };
        HAL_GPIO_Init(config::GSM_GPIO_RX.port, &rx_init);

        // Configure the UART channel
        __HAL_RCC_USART1_CLK_ENABLE();

        s_huart.Instance          = config::GSM_UART_PORT;
        s_huart.Init.BaudRate     = 57600U;
        s_huart.Init.WordLength   = UART_WORDLENGTH_8B;
        s_huart.Init.StopBits     = UART_STOPBITS_1;
        s_huart.Init.Parity       = UART_PARITY_NONE;
        s_huart.Init.Mode         = UART_MODE_TX_RX;
        s_huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
        s_huart.Init.OverSampling = UART_OVERSAMPLING_16;
        utils::assert_check(HAL_UART_Init(&s_huart) == HAL_OK);

        // Configure the DMA channels
        __HAL_RCC_DMA1_CLK_ENABLE();

        // TX
        s_hdma_tx.Instance                 = config::GSM_UART_DMA_TX;
        s_hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
        s_hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
        s_hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;
        s_hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        s_hdma_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
        s_hdma_tx.Init.Mode                = DMA_NORMAL;
        s_hdma_tx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;

        utils::assert_check(HAL_DMA_Init(&s_hdma_tx) == HAL_OK);
        __HAL_LINKDMA(&s_huart, hdmatx, s_hdma_tx);

        // RX
        s_hdma_rx.Instance                 = config::GSM_UART_DMA_RX;
        s_hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
        s_hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
        s_hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
        s_hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        s_hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
        s_hdma_rx.Init.Mode                = DMA_NORMAL;
        s_hdma_rx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;

        utils::assert_check(HAL_DMA_Init(&s_hdma_rx) == HAL_OK);
        __HAL_LINKDMA(&s_huart, hdmarx, s_hdma_rx);

        // Enable the NVIC irqs and set priorities to lowest
        HAL_NVIC_SetPriority(USART1_IRQn, 15, 0);
        HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 15, 0);
        HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 15, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
        HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

        // Create the mutex
        s_task_mutex = xSemaphoreCreateMutexStatic(&s_task_mutex_buffer);

        // Send init sequence to GSM module and confirm everything is in order
        auto ret = send_init_sequence();

        if (ret == error_t::NONE) {
            s_is_initialized = true;
        }

        return ret;
    }

    void deinit() {
        utils::assert_check(s_is_initialized);

        // Tell the SIM800L to deinitialize itself.
        // We don't care if there's an error so we can ignore the return value.
        (void)send_cmd_and_compare_result(cmd_t::DEINIT);

        // Deinitialize the USART and DMA channels
        utils::assert_check(HAL_DMA_DeInit(&s_hdma_tx) == HAL_OK);
        utils::assert_check(HAL_DMA_DeInit(&s_hdma_rx) == HAL_OK);
        utils::assert_check(HAL_UART_DeInit(&s_huart) == HAL_OK);

        s_huart               = {};
        s_hdma_tx             = {};
        s_hdma_rx             = {};
        s_rx_idle_line_size   = {};
        s_task_mutex          = {};
        s_task_mutex_buffer   = {};
        s_calling_task_handle = {};

        // Disable the corresponding NVIC UART, DMA tx and rx irqs
        NVIC_DisableIRQ(USART1_IRQn);
        NVIC_DisableIRQ(DMA1_Channel4_IRQn);
        NVIC_DisableIRQ(DMA1_Channel5_IRQn);

        // Set TX and RX pins as analog
        GPIO_InitTypeDef gpio_deinit = {
            .Pin   = static_cast<uint32_t>(config::GSM_GPIO_TX.pin | config::GSM_GPIO_RX.pin),
            .Mode  = GPIO_MODE_ANALOG,
            .Pull  = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW,
        };
        HAL_GPIO_Init(config::GSM_GPIO_TX.port, &gpio_deinit);

        s_is_initialized = false;
    }

    error_t get_sim_status() {
        // RAII handling for mutex acquisition and releasing
        bool                     mutex_taken{};
        [[maybe_unused]] mutex_t mutex(mutex_taken);

        if (!mutex_taken) {
            return error_t::MUTEX_TIMEOUT;
        }

        utils::assert_check(s_is_initialized);

        // Confirm the module is still responding before doing anything
        auto ret = send_cmd_and_compare_result(cmd_t::AT);
        if (ret != error_t::NONE) {
            return error_t::MODULE_NOT_ALIVE;
        }

        // Check if the SIM card is present
        ret = send_cmd_and_compare_result(cmd_t::CHECK_SIM);
        if (ret != error_t::NONE) {
            return error_t::SIM_NOT_FOUND;
        }

        // Check if the SIM card is registered to a network service
        ret = send_cmd_and_compare_result(cmd_t::CHECK_REG);
        if (ret != error_t::NONE) {
            return error_t::SIM_NOT_REGISTERED;
        }

        // Check signal strength
        // No need to poll here. This is simply a status check
        ret = send_cmd_and_compare_result(cmd_t::CHECK_SIGNAL);
        if (ret != error_t::NONE) {
            return error_t::BAD_NETWORK_CONN;
        }

        return error_t::NONE;
    }

    error_t send_sms(const etl::string_view& sms, const etl::string_view& number, bool check_sim_status) {
        // RAII handling for mutex acquisition and releasing
        bool                     mutex_taken{};
        [[maybe_unused]] mutex_t mutex(mutex_taken);

        if (!mutex_taken) {
            return error_t::MUTEX_TIMEOUT;
        }

        utils::assert_check(s_is_initialized);
        utils::assert_check(sms.size() <= MAX_SMS_LEN);
        utils::assert_check(number.size() <= MAX_PHONE_NUMBER_LEN);

        // Will clear the rx idle line and calling task handle variables
        [[maybe_unused]] cleanup_t auto_cleanup;

        if (check_sim_status) {
            // Check the SIM card's status before sending the SMS
            auto ret = get_sim_status();
            if (ret != error_t::NONE) {
                return ret;
            }
        }

        // Capture calling task since the irq handler sends a notification to it
        s_calling_task_handle = xTaskGetCurrentTaskHandle();

        // Build phone number AT command
        // Sadly, no DMA descriptors so scatter gather is not available
        // so we have to build the string into a buffer before transmitting
        static constexpr etl::array<const char, 10> num_begin = {"AT+CMGS=\""};
        static constexpr etl::array<const char, 3>  num_end   = {"\"\r"};

        // Phone number AT string
        // NOTE: `num_begin` and `num_begin` are guaranteed to be null terminated
        // whereas `number` is not, hence why we pass size for only it.
        etl::string<num_begin.size() + MAX_PHONE_NUMBER_LEN + num_end.size()> number_command = num_begin.data();
        number_command.append(number.data(), number.size());
        number_command.append(num_end.data());

        // We have to start reception on the UART RX line since the SIM800L
        // may start its own transmission immediately after ours is done.
        etl::array<char, UART_IDLE_LINE_BUF_BYTE> rx_num_buf{};
        utils::assert_check(HAL_UARTEx_ReceiveToIdle_DMA(&s_huart, reinterpret_cast<uint8_t*>(rx_num_buf.data()), rx_num_buf.max_size()) ==
                            HAL_OK);

        // Transmit the AT command
        utils::assert_check(
            HAL_UART_Transmit_DMA(&s_huart, reinterpret_cast<const uint8_t*>(number_command.data()), number_command.size()) == HAL_OK);

        // Block till the task notification is received from the ISR
        // If no notification is received within the timeout, return an error.
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMEOUT_MS)) == 0) {
            return error_t::FAIL;
        }

        // Construct a string view from the returned data and see if it matches what we expect
        auto rx_number_actual_ret_val = std::string_view{rx_num_buf.data(), s_rx_idle_line_size};

        // Check if the expected response can be found in the actual response
        if (!rx_number_actual_ret_val.contains('>')) {
            return error_t::FAIL;
        }

        // Clear the RX dma size variable
        s_rx_idle_line_size = {};

        // If we get here, the SIM800L has given us clearance to send the SMS
        // So we send the SMS followed by `0x1A (CTRL + Z)`.
        etl::string<MAX_SMS_LEN + 1> sms_command = {sms.data(), sms.size()};
        sms_command += '\x1A';

        // We have to start reception on the UART RX line since the SIM800L
        // may start its own transmission immediately after ours is done.
        etl::array<char, UART_IDLE_LINE_BUF_BYTE> rx_sms_buf{};
        utils::assert_check(HAL_UARTEx_ReceiveToIdle_DMA(&s_huart, reinterpret_cast<uint8_t*>(rx_sms_buf.data()), rx_sms_buf.max_size()) ==
                            HAL_OK);

        // Transmit the AT command
        utils::assert_check(HAL_UART_Transmit_DMA(&s_huart, reinterpret_cast<const uint8_t*>(sms_command.data()), sms_command.size()) ==
                            HAL_OK);

        // Block till the task notification is received from the ISR
        // If no notification is received within the timeout, return
        // an error. Reception in this case can take a long time.
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMEOUT_MS * 5)) == 0) {
            return error_t::FAIL;
        }

        // Construct a string view from the returned data and see if it matches what we expect
        auto rx_sms_actual_ret_val = std::string_view{rx_sms_buf.data(), s_rx_idle_line_size};

        // Parse output to see if there was an error.
        // The returned string should contain "+CMGS:" on success
        return rx_sms_actual_ret_val.contains("+CMGS:") ? error_t::NONE : error_t::SMS_SEND_FAIL;
    }

    etl::expected<etl::array<char, IMSI_BUF_SIZE>, error_t> get_imsi() {
        // RAII handling for mutex acquisition and releasing
        bool                     mutex_taken{};
        [[maybe_unused]] mutex_t mutex(mutex_taken);

        if (!mutex_taken) {
            return etl::unexpected(error_t::MUTEX_TIMEOUT);
        }

        utils::assert_check(s_is_initialized);

        // Confirm the module is still responding before doing anything
        auto ret = send_cmd_and_compare_result(cmd_t::AT);
        if (ret != error_t::NONE) {
            return etl::unexpected(error_t::MODULE_NOT_ALIVE);
        }

        // We only need to check if the SIM card is available
        // since the IMSI is a static property of the SIM card
        ret = send_cmd_and_compare_result(cmd_t::CHECK_SIM);
        if (ret != error_t::NONE) {
            return etl::unexpected(error_t::SIM_NOT_FOUND);
        }

        auto rx_str = transact();

        // Makes sure `s_rx_idle_line_size` has enough data before copying any data
        // NOTE: The module is supposed to send a carriage return, a newline, the 15
        // digit IMSI, another carriage and newline, yet another carriage and newline
        // a "OK" and a final carriage and newline, giving us 25 characters total.
        if (rx_str.size() < 25) {
            return etl::unexpected(error_t::FAIL);
        }

        // Extract IMSI: Since the responses have `'\r\n'`, we find the
        // find the first occurrence of them and copy the next 15 characters.
        etl::array<char, IMSI_BUF_SIZE> imsi{};
        size_t                          pos{};

        // Search for first occurrence
        for (size_t i{}; i < s_rx_idle_line_size; i++) {
            if (rx_str[i] == '\r' && rx_str[i + 1] == '\n') {
                pos = i;
                break;
            }
        }

        // Return an error if `'\r\n'` wasnt found
        if (pos == rx_str.size()) {
            return etl::unexpected(error_t::FAIL);
        }

        // Copy the next 15 characters as our IMSI
        memcpy(imsi.data(), (rx_str.data() + pos), 15);

        return imsi;
    }

    // Helpers
    static inline error_t send_cmd_and_compare_result(cmd_t cmd) {

        // Will clear the rx idle line and calling task handle variables
        [[maybe_unused]] cleanup_t auto_cleanup;

        error_t ret{error_t::NONE};

        // Capture calling task since the irq handler sends a notification to it
        s_calling_task_handle = xTaskGetCurrentTaskHandle();

        // We have to start reception on the UART RX line since the SIM800L
        // may start its own transmission immediately after ours is done.
        etl::array<char, UART_IDLE_LINE_BUF_BYTE> rx_buf{};
        utils::assert_check(HAL_UARTEx_ReceiveToIdle_DMA(&s_huart, reinterpret_cast<uint8_t*>(rx_buf.data()), rx_buf.max_size()) == HAL_OK);

        // Get the corresponding AT command using the command as the index
        const auto& data = AT_CMD_LUT[std::to_underlying(cmd)];

        // Transmit the AT command
        utils::assert_check(HAL_UART_Transmit_DMA(&s_huart, reinterpret_cast<const uint8_t*>(data.tx.data()), data.tx.size()) == HAL_OK);

        // Block till the task notification is received from the ISR
        // If no notification is received within the timeout, return an error.
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMEOUT_MS)) == 0) {
            return error_t::FAIL;
        }

        // Construct a `std::string_view` from the data received. The UART
        // idle line ISR puts the actual length received in `s_rx_idle_line_size`.
        auto rx_actual = std::string_view{rx_buf.data(), s_rx_idle_line_size};

        // Interpret the data that was received based on the type of AT command
        // that was transferred because some require parsing and others do not.
        [&]() {
            // The `CHECK_SIGNAL` command receives a command that requires parsing
            if (cmd == cmd_t::CHECK_SIGNAL) {
                // Get index to the beginning of the first instance of `"+CSQ: "`
                auto pos = rx_actual.find("+CSQ: ");
                if (pos == std::string_view::npos) {
                    ret = error_t::FAIL;
                    return;
                }

                // strlen of `"+CSQ: "` is 6. Create a sub string view of everything after it.
                auto rssi_str = rx_actual.substr(pos + 6);

                // Get the RSSI. It is the first number after `"+CSQ: "`
                auto rssi = std::strtoul(rssi_str.data(), {}, 10);

                // If RSSI is 99, the module couldn't detect a signal or
                // if RSSI is less than 5, the signal is too weak to use.
                if (rssi == 99 || rssi < 5) {
                    ret = error_t::FAIL;
                    return;
                }
            }
            // The `CHECK_REG` command receives a command that also requires parsing
            else if (cmd == cmd_t::CHECK_REG) {
                // Find position of first three numbers that appears after `','`
                auto pos = rx_actual.find(',');
                if (pos == std::string_view::npos) {
                    ret = error_t::FAIL;
                    return;
                }

                // Create a string view of the remaining characters that appear after the `',`
                auto stat_str = rx_actual.substr(pos + 1);

                // Get the network stat
                auto stat = std::strtoul(stat_str.data(), {}, 10);

                // The stat is what tells us the state of the SIM card's network registration.
                // A stat of 1 means homing and 5 means roaming. Nothing else is good.
                if (!(stat == 1) && !(stat == 5)) {
                    ret = error_t::FAIL;
                    return;
                }
            }
            // If the sent command wasn't `CHECK_SIGNAL` or `CHECK_REG`, that
            // means the command does not need parsing and the actual result
            // can be checked to see if it contains the expected result.
            else {
                // Check if the AT command we are expecting for the transmitted AT command can be
                // found in the actual data we received back. If it's not, then an error occurred.
                if (!rx_actual.contains(data.rx_expected)) {
                    ret = error_t::FAIL;
                    return;
                }
            }
        }();

        // Don't blame me. AT commands are a mess.

        return ret;
    }

    static inline error_t send_init_sequence() {

        // Will clear the rx idle line and calling task handle variables
        [[maybe_unused]] cleanup_t auto_cleanup;

        error_t ret{error_t::NONE};

        // We cannot use `send_cmd_and_compare_result(...)` here
        // since we need to send the `AT` command multiple times
        // untill we receive the `OK` string from the SIM800L. So we
        // need to handle this special case manually.

        // Capture calling task since the irq handler sends a notification to it
        s_calling_task_handle = xTaskGetCurrentTaskHandle();

        // We have to start reception on the UART RX line since the SIM800L
        // may start its own transmission immediately after ours is done.
        etl::array<char, UART_IDLE_LINE_BUF_BYTE> rx_buf{};
        utils::assert_check(HAL_UARTEx_ReceiveToIdle_DMA(&s_huart, reinterpret_cast<uint8_t*>(rx_buf.data()), rx_buf.max_size()) == HAL_OK);

        // Get the tx data from the LUT
        const auto& data = AT_CMD_LUT[std::to_underlying(cmd_t::AT)];
        uint8_t     count{NUM_OF_TIMES_TO_SEND_AT};
        bool        module_responded{};

        // Continuously transmit the `AT` command
        while (static_cast<bool>(count--)) {
            utils::assert_check(
                HAL_UART_Transmit(&s_huart, reinterpret_cast<const uint8_t*>(data.tx.data()), data.tx.size(), HAL_MAX_DELAY) == HAL_OK);

            // If a notification was received, that means the module has responded.
            // Wait for some ms before attempting to transmit the `AT` command again
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DELAY_BETWEEN_TX_AT_CMDS_MS)) > 0) {
                module_responded = true;
                break;
            }
        }

        if (module_responded) {
            // Construct a `std::string_view` from the data received. The UART
            // idle line ISR puts the actual length received in `s_rx_idle_line_size`.
            auto rx_actual = std::string_view{rx_buf.data(), s_rx_idle_line_size};

            // Check if the expected return was in the actual data received
            if (!rx_actual.contains(data.rx_expected)) {
                ret = error_t::MODULE_NOT_ALIVE;
            }
        } else {
            ret = error_t::MODULE_NOT_ALIVE;
        }

        // Return immediately on an error
        if (ret != error_t::NONE) {
            return error_t::MODULE_NOT_ALIVE;
        }

        // Echo mode off
        ret = send_cmd_and_compare_result(cmd_t::ECHO_OFF);
        if (ret != error_t::NONE) {
            return error_t::FAIL;
        }

        // Text mode on
        ret = send_cmd_and_compare_result(cmd_t::TEXT_MODE);
        if (ret != error_t::NONE) {
            return error_t::FAIL;
        }

        // Set network provider's SMSC: GLO's in this case
        ret = send_cmd_and_compare_result(cmd_t::SET_SMSC);
        if (ret != error_t::NONE) {
            return error_t::FAIL;
        }

        // Check if the SIM card is present
        ret = send_cmd_and_compare_result(cmd_t::CHECK_SIM);
        if (ret != error_t::NONE) {
            return error_t::SIM_NOT_FOUND;
        }

        // Check if the SIM card is registered to a network service
        ret = send_cmd_and_compare_result(cmd_t::CHECK_REG);
        if (ret != error_t::NONE) {
            return error_t::SIM_NOT_REGISTERED;
        }

        // Check signal strength
        for (uint32_t i{0}; i < NUM_OF_TIMES_TO_POLL_SIGNAL_CHECK; i++) {
            ret = send_cmd_and_compare_result(cmd_t::CHECK_SIGNAL);
            if (ret == error_t::NONE) {
                return error_t::NONE;
            }

            // We poll here since network connection failure is a recoverable error from
            // the module, so we can poll it until we get a stable network connection
            vTaskDelay(pdMS_TO_TICKS(DELAY_BETWEEN_SIGNAL_CHECK_POLL_MS));
        }

        // If we get here, we were unable to establish a good connection
        return error_t::BAD_NETWORK_CONN;
    }

    static inline etl::expected<etl::string_view, error_t>
    transact(const etl::string_view& tx_cmd, etl::array<char, UART_IDLE_LINE_BUF_BYTE>& rx_cmd, uint32_t timeout_ms) {

        // Will clear the rx idle line and calling task handle variables
        [[maybe_unused]] cleanup_t auto_cleanup{};

        // Capture calling task since the irq handler sends a notification to it
        s_calling_task_handle = xTaskGetCurrentTaskHandle();

        // We have to start reception on the UART RX line since the SIM800L
        // may start its own transmission immediately after ours is done.
        utils::assert_check(HAL_UARTEx_ReceiveToIdle_DMA(&s_huart, reinterpret_cast<uint8_t*>(rx_cmd.data()), rx_cmd.max_size()) == HAL_OK);

        // Transmit the AT command
        utils::assert_check(HAL_UART_Transmit_DMA(&s_huart, reinterpret_cast<const uint8_t*>(tx_cmd.data()), tx_cmd.size()) == HAL_OK);

        // Block till the task notification is received from the ISR
        // If no notification is received within the timeout, return an error.
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) == 0) {
            return etl::unexpected(error_t::FAIL);
        }

        return etl::string_view{rx_cmd.data(), s_rx_idle_line_size};
    }

} // namespace gsm

extern "C" {
    // UART RX done callback
    void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size) {
        if (huart->Instance == gsm::s_huart.Instance) {
            // Save the length that was received
            gsm::s_rx_idle_line_size = Size;
            BaseType_t higher_priority_task_woken{};
            vTaskNotifyGiveFromISR(gsm::s_calling_task_handle, &higher_priority_task_woken);
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
    }

    // UART1 irq handler
    void USART1_IRQHandler() {
        HAL_UART_IRQHandler(&gsm::s_huart);
    }

    // DMA TX irq handler
    void DMA1_Channel4_IRQHandler() {
        HAL_DMA_IRQHandler(&gsm::s_hdma_tx);
    }

    // DMA RX irq handler
    void DMA1_Channel5_IRQHandler() {
        HAL_DMA_IRQHandler(&gsm::s_hdma_rx);
    }
}

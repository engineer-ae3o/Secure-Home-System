#include "stm32f1xx_hal.h"

#include "sim800l.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <cstring>
#include <charconv>
#include <string_view>

#include "etl/string.h" // Needed for `etl::string`

namespace gsm {

    namespace {
        // Global state. It is what it is
        UART_HandleTypeDef s_huart{};
        DMA_HandleTypeDef  s_hdma_tx{};
        DMA_HandleTypeDef  s_hdma_rx{};
        TaskHandle_t       s_calling_task_handle{};
        SemaphoreHandle_t  s_task_mutex{};
        StaticSemaphore_t  s_task_mutex_buffer{};

        bool s_is_initialized{};

        // This is needed because we are receiving UART data, we don't
        // know the length of the data we will get. The UART idle line
        // ISR puts the actual length of the data received here.
        volatile uint16_t s_rx_idle_line_size{};

        constexpr uint32_t UART_IDLE_LINE_BUF_BYTE{64};

        constexpr uint32_t NUM_OF_TIMES_TO_POLL_SIGNAL_CHECK{6};
        constexpr uint32_t DELAY_BETWEEN_SIGNAL_CHECK_POLL_MS{5000};

        constexpr uint32_t NUM_OF_TIMES_TO_SEND_AT{10};
        constexpr uint32_t DELAY_BETWEEN_TX_AT_CMDS_MS{250};

        constexpr uint32_t TIMEOUT_MS{50};
        constexpr uint32_t DEINIT_TIMEOUT_MS{5000};

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
            mutex_t(uint32_t timeout_ms = TIMEOUT_MS)
                : m_mutex_taken(xSemaphoreTakeRecursive(s_task_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
            }

            ~mutex_t() {
                if (m_mutex_taken) {
                    xSemaphoreGiveRecursive(s_task_mutex);
                }
            }

            operator bool() const {
                return m_mutex_taken;
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
            SET_SMSC,  // SMSC for network provider

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

        struct cmd_entry_t {
            std::string_view tx;
            std::string_view rx_expected;
        };

        // AT commands LUT
        constexpr std::array<cmd_entry_t, std::to_underlying(cmd_t::COUNT)> AT_CMD_LUT = {{
            [std::to_underlying(cmd_t::AT)]           = {"AT\r", "OK"},
            [std::to_underlying(cmd_t::ECHO_OFF)]     = {"ATE0\r", "OK"},
            [std::to_underlying(cmd_t::TEXT_MODE)]    = {"AT+CMGF=1\r", "OK"},
            [std::to_underlying(cmd_t::SET_SMSC)]     = {config::SIM_CARD_SMSC, "OK"},
            [std::to_underlying(cmd_t::CHECK_SIM)]    = {"AT+CPIN?\r", "+CPIN: READY"},
            [std::to_underlying(cmd_t::CHECK_REG)]    = {"AT+CREG?\r", "+CREG"},
            [std::to_underlying(cmd_t::CHECK_SIGNAL)] = {"AT+CSQ\r", "+CSQ"},
            [std::to_underlying(cmd_t::GET_IMSI)]     = {"AT+CIMI\r", "OK"},
            [std::to_underlying(cmd_t::DEINIT)]       = {"AT+CPOWD=1\r", "NORMAL POWER DOWN"},
        }};

        // Helper for this driver specific error checking
#define TRY_GSM(func, err)                                                                                                                 \
    do {                                                                                                                                   \
        if (auto ret_ = (func); ret_ != utils::error_t::NONE) {                                                                            \
            return err;                                                                                                                    \
        }                                                                                                                                  \
    } while (0)

        // Helpers
        [[nodiscard]] std::expected<std::string_view, utils::error_t>
        transact(std::string_view tx_cmd, std::array<char, UART_IDLE_LINE_BUF_BYTE>& rx_cmd, uint32_t timeout_ms = TIMEOUT_MS) {

            // Will clear the rx idle line and calling task handle variables
            [[maybe_unused]] cleanup_t auto_cleanup{};

            // Capture calling task since the irq handler sends a notification to it
            s_calling_task_handle = xTaskGetCurrentTaskHandle();

            // We have to start reception on the UART RX line since the SIM800L
            // may start its own transmission immediately after ours is done.
            if (HAL_UARTEx_ReceiveToIdle_DMA(&s_huart, reinterpret_cast<uint8_t*>(rx_cmd.data()), rx_cmd.max_size()) != HAL_OK) {
                return std::unexpected(utils::error_t::ERR_HAL_FAIL);
            }

            // Transmit the AT command
            if (HAL_UART_Transmit_DMA(&s_huart, reinterpret_cast<const uint8_t*>(tx_cmd.data()), tx_cmd.size()) != HAL_OK) {
                return std::unexpected(utils::error_t::ERR_HAL_FAIL);
            }

            // Block till the task notification is received from the ISR
            // If no notification is received within the timeout, return an error.
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) == 0) {
                HAL_UART_Abort(&s_huart);
                return std::unexpected(utils::error_t::ERR_FAIL);
            }

            return std::string_view{rx_cmd.data(), s_rx_idle_line_size};
        }

        utils::error_t send_cmd_and_compare_result(cmd_t cmd, uint32_t timeout_ms = TIMEOUT_MS) {

            utils::error_t ret{utils::error_t::NONE};

            // Get AT command and the expected response from the LUT
            const auto& [tx_str, rx_expected] = AT_CMD_LUT[std::to_underlying(cmd)];

            // The resulting string view gets stored here
            std::array<char, UART_IDLE_LINE_BUF_BYTE> rx_buf{};
            auto                                      rx_str = transact(tx_str, rx_buf, timeout_ms);

            if (!rx_str) {
                return utils::error_t::ERR_FAIL;
            }

            // Interpret the data that was received based on the type of AT command
            // that was transferred because some require parsing and others do not.
            [&] {
                // The `CHECK_SIGNAL` command receives a command that requires parsing
                if (cmd == cmd_t::CHECK_SIGNAL) {
                    // Get index to the beginning of the first instance of `"+CSQ: "`
                    auto pos = rx_str->find("+CSQ: ");
                    if (pos == std::string_view::npos) {
                        ret = utils::error_t::ERR_FAIL;
                        return;
                    }

                    // strlen of `"+CSQ: "` is 6. Create a sub string view of everything after it.
                    auto rssi_str = rx_str->substr(pos + 6);

                    // Get the RSSI. It is the first number after `"+CSQ: "`
                    size_t rssi{};
                    auto [ptr, err] = std::from_chars(rssi_str.data(), rssi_str.data() + rssi_str.size(), rssi);
                    if (err != std::errc{}) {
                        ret = utils::error_t::ERR_FAIL;
                        return;
                    }

                    // If RSSI is 99, the module couldn't detect a signal or
                    // if RSSI is less than 5, the signal is too weak to use.
                    if (rssi == 99 || rssi < 5) {
                        ret = utils::error_t::ERR_FAIL;
                        return;
                    }
                }
                // The `CHECK_REG` command receives a command that also requires parsing
                else if (cmd == cmd_t::CHECK_REG) {
                    // Find position of first three numbers that appears after `','`
                    auto pos = rx_str->find(',');
                    if (pos == std::string_view::npos) {
                        ret = utils::error_t::ERR_FAIL;
                        return;
                    }

                    // Create a string view of the remaining characters that appear after the `',`
                    auto stat_str = rx_str->substr(pos + 1);

                    // Get the network stat
                    size_t stat{};
                    auto [ptr, err] = std::from_chars(stat_str.data(), stat_str.data() + stat_str.size(), stat);
                    if (err != std::errc{}) {
                        ret = utils::error_t::ERR_FAIL;
                        return;
                    }

                    // The stat is what tells us the state of the SIM card's network registration.
                    // A stat of 1 means homing and 5 means roaming. Nothing else is good.
                    if (!(stat == 1) && !(stat == 5)) {
                        ret = utils::error_t::ERR_FAIL;
                        return;
                    }
                }
                // If the sent command wasn't `CHECK_SIGNAL` or `CHECK_REG`, that
                // means the command does not need parsing and the actual result
                // can be checked to see if it contains the expected result.
                else {
                    // Check if the AT command we are expecting for the transmitted AT command can be
                    // found in the actual data we received back. If it's not, then an error occurred.
                    if (!rx_str->contains(rx_expected)) {
                        ret = utils::error_t::ERR_FAIL;
                        return;
                    }
                }
            }();

            // Don't blame me. AT commands are a mess.

            return ret;
        }

        utils::error_t send_init_sequence() {

            utils::error_t ret{utils::error_t::NONE};

            // We cannot use `send_cmd_and_compare_result(...)` here
            // since we need to send the `AT` command multiple times
            // until we receive the `OK` string from the SIM800L. So
            // we need to handle this special case manually. We also
            // limit its scope so as not to impact the transmission
            // of other AT commands.

            {
                // Will clear the rx idle line and calling task handle variables
                [[maybe_unused]] cleanup_t auto_cleanup;

                // Capture calling task since the irq handler sends a notification to it
                s_calling_task_handle = xTaskGetCurrentTaskHandle();

                // We have to start reception on the UART RX line since the SIM800L
                // may start its own transmission immediately after ours is done.
                std::array<char, UART_IDLE_LINE_BUF_BYTE> rx_buf{};
                TRY_HAL(HAL_UARTEx_ReceiveToIdle_DMA(&s_huart, reinterpret_cast<uint8_t*>(rx_buf.data()), rx_buf.max_size()));

                // Get the tx data from the LUT
                const auto& [tx_str, rx_expected] = AT_CMD_LUT[std::to_underlying(cmd_t::AT)];
                uint8_t count{NUM_OF_TIMES_TO_SEND_AT};
                bool    module_responded{};

                // Continuously transmit the `AT` command
                while (static_cast<bool>(count--)) {
                    TRY_HAL(HAL_UART_Transmit_DMA(&s_huart, reinterpret_cast<const uint8_t*>(tx_str.data()), tx_str.size()));

                    // The UART idle line irq handler sends a notification on completion, implying the module has responded.
                    // We block for some ms while waiting for the notification before sending the 'AT' command again.
                    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DELAY_BETWEEN_TX_AT_CMDS_MS)) > 0) {
                        module_responded = true;
                        break;
                    }
                }

                if (module_responded) {
                    // Construct a `std::string_view` from the data received. The UART
                    // idle line ISR puts the actual length received in `s_rx_idle_line_size`.
                    auto rx_actual = std::string_view{rx_buf.data(), s_rx_idle_line_size};

                    // Check if the expected response was in the actual data received
                    if (!rx_actual.contains(rx_expected)) {
                        ret = utils::error_t::GSM_MODULE_NOT_ALIVE;
                    }
                    // Report an error since the SIM800L didn't respond
                } else {
                    HAL_UART_Abort(&s_huart);
                    ret = utils::error_t::GSM_MODULE_NOT_ALIVE;
                }

                // Return immediately on an error
                if (ret != utils::error_t::NONE) {
                    return utils::error_t::GSM_MODULE_NOT_ALIVE;
                }
            }

            // Echo mode off
            TRY_GSM(send_cmd_and_compare_result(cmd_t::ECHO_OFF), utils::error_t::ERR_FAIL);

            // Text mode on
            TRY_GSM(send_cmd_and_compare_result(cmd_t::TEXT_MODE), utils::error_t::ERR_FAIL);

            // Set network provider's SMSC: GLO's in this case
            TRY_GSM(send_cmd_and_compare_result(cmd_t::SET_SMSC), utils::error_t::ERR_FAIL);

            // Check if the SIM card is present
            TRY_GSM(send_cmd_and_compare_result(cmd_t::CHECK_SIM), utils::error_t::GSM_SIM_NOT_FOUND);

            // Check if the SIM card is registered to a network service
            TRY_GSM(send_cmd_and_compare_result(cmd_t::CHECK_REG), utils::error_t::GSM_SIM_NOT_REGISTERED);

            // Check signal strength
            // We poll here since network connection failure is a recoverable error from
            // the module, so we can poll it until we get a stable network connection
            for (uint32_t i{0}; i < NUM_OF_TIMES_TO_POLL_SIGNAL_CHECK; i++) {
                ret = send_cmd_and_compare_result(cmd_t::CHECK_SIGNAL);
                if (ret == utils::error_t::NONE) {
                    return utils::error_t::NONE;
                }

                vTaskDelay(pdMS_TO_TICKS(DELAY_BETWEEN_SIGNAL_CHECK_POLL_MS));
            }

            // If we get here, we were unable to establish a good connection
            return utils::error_t::GSM_BAD_NETWORK_CONN;
        }

    } // namespace

    // Public API
    utils::error_t init() {
        if (s_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

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
        s_huart.Init.BaudRate     = 57600U; // The SIM800L has auto-bauding so it detects our baud rate
        s_huart.Init.WordLength   = UART_WORDLENGTH_8B;
        s_huart.Init.StopBits     = UART_STOPBITS_1;
        s_huart.Init.Parity       = UART_PARITY_NONE;
        s_huart.Init.Mode         = UART_MODE_TX_RX;
        s_huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
        s_huart.Init.OverSampling = UART_OVERSAMPLING_16;
        TRY_HAL(HAL_UART_Init(&s_huart));

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

        TRY_HAL(HAL_DMA_Init(&s_hdma_tx));
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

        TRY_HAL(HAL_DMA_Init(&s_hdma_rx));
        __HAL_LINKDMA(&s_huart, hdmarx, s_hdma_rx);

        // Enable the NVIC irqs and set priorities to lowest
        HAL_NVIC_SetPriority(USART1_IRQn, 15, 0);
        HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 15, 0);
        HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 15, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
        HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

        // Send init sequence to GSM module and confirm everything is in order
        TRY(send_init_sequence());

        // Create the mutex as recursive
        s_task_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_task_mutex_buffer);

        s_is_initialized = true;

        return utils::error_t::NONE;
    }

    utils::error_t deinit() {
        if (!s_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        {
            // Take the mutex to make sure no other thread is using the SIM800L while we are
            // deinitializing it. We have to wait for all other tasks to finish use of the mutex
            [[maybe_unused]] mutex_t mutex(portMAX_DELAY);

            // Tell the SIM800L to deinitialize itself. We don't care
            // if there's an error so we can ignore the return value.
            (void)send_cmd_and_compare_result(cmd_t::DEINIT, DEINIT_TIMEOUT_MS);

            // Disable the corresponding NVIC UART, DMA tx and rx irqs
            HAL_NVIC_DisableIRQ(USART1_IRQn);
            HAL_NVIC_DisableIRQ(DMA1_Channel4_IRQn);
            HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);

            // Deinitialize the USART and DMA channels
            TRY_HAL(HAL_DMA_DeInit(&s_hdma_tx));
            TRY_HAL(HAL_DMA_DeInit(&s_hdma_rx));
            TRY_HAL(HAL_UART_DeInit(&s_huart));

            s_huart               = {};
            s_hdma_tx             = {};
            s_hdma_rx             = {};
            s_rx_idle_line_size   = {};
            s_calling_task_handle = {};

            // Set TX and RX pins as analog
            GPIO_InitTypeDef gpio_deinit = {
                .Pin   = static_cast<uint32_t>(config::GSM_GPIO_TX.pin | config::GSM_GPIO_RX.pin),
                .Mode  = GPIO_MODE_ANALOG,
                .Pull  = GPIO_NOPULL,
                .Speed = GPIO_SPEED_FREQ_LOW,
            };
            HAL_GPIO_Init(config::GSM_GPIO_TX.port, &gpio_deinit);
        }

        // Unintialize the mutex
        vSemaphoreDelete(s_task_mutex);
        s_task_mutex        = {};
        s_task_mutex_buffer = {};

        s_is_initialized = false;

        return utils::error_t::NONE;
    }

    utils::error_t get_sim_status() {
        if (!s_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        // RAII handling for mutex acquisition and releasing
        [[maybe_unused]] mutex_t mutex;
        if (!mutex) {
            return utils::error_t::ERR_TIMEOUT;
        }

        // Confirm the module is still responding before doing anything
        TRY_GSM(send_cmd_and_compare_result(cmd_t::AT), utils::error_t::GSM_MODULE_NOT_ALIVE);

        // Check if the SIM card is present
        TRY_GSM(send_cmd_and_compare_result(cmd_t::CHECK_SIM), utils::error_t::GSM_SIM_NOT_FOUND);

        // Check if the SIM card is registered to a network service
        TRY_GSM(send_cmd_and_compare_result(cmd_t::CHECK_REG), utils::error_t::GSM_SIM_NOT_REGISTERED);

        // Check signal strength
        // No need to poll here. This is simply a status check
        TRY_GSM(send_cmd_and_compare_result(cmd_t::CHECK_SIGNAL), utils::error_t::GSM_BAD_NETWORK_CONN);

        return utils::error_t::NONE;
    }

    utils::error_t send_sms(std::string_view sms, std::string_view number, bool check_sim_status) {
        if (!s_is_initialized) {
            return utils::error_t::ERR_INVALID_STATE;
        }

        if (sms.size() == 0 || sms.data() == nullptr || number.size() == 0 || number.data() == nullptr) {
            return utils::error_t::ERR_INVALID_ARG;
        }

        // RAII handling for mutex acquisition and releasing
        [[maybe_unused]] mutex_t mutex;
        if (!mutex) {
            return utils::error_t::ERR_TIMEOUT;
        }

        if (sms.size() > MAX_SMS_LEN || number.size() > MAX_PHONE_NUMBER_LEN) {
            return utils::error_t::ERR_INVALID_ARG;
        }

        if (check_sim_status) {
            // Check the SIM card's status before sending the SMS
            TRY(get_sim_status());
        }

        // Build phone number AT command
        // Sadly, no DMA descriptors so scatter gather is not available,
        // so we have to build the string into a buffer before transmitting.
        static constexpr std::string_view num_begin = {"AT+CMGS=\""};
        static constexpr std::string_view num_end   = {"\"\r"};

        // Phone number AT string
        // NOTE: `num_begin` and `num_begin` are guaranteed to be null terminated whereas
        // `number` is not, hence why we pass size for only it. I use an `etl::string`
        // because the data has to be modifiable and owning and I am not going through
        // the stress of implementing it myself when it already exists.
        etl::string<num_begin.size() + MAX_PHONE_NUMBER_LEN + num_end.size()> number_command{num_begin.data()};
        number_command.append(number.data(), number.size());
        number_command.append(num_end.data());

        // The resulting string view gets stored here
        std::array<char, UART_IDLE_LINE_BUF_BYTE> rx_num_buf{};
        auto                                      rx_num_str = transact({number_command.data(), number_command.size()}, rx_num_buf);

        if (!rx_num_str) {
            return utils::error_t::ERR_FAIL;
        }

        // Check if the expected response can be found in the actual response
        if (!rx_num_str->contains('>')) {
            return utils::error_t::ERR_FAIL;
        }

        // If we get here, the SIM800L has given us clearance to send
        // the SMS. So we send the SMS suffixed with `0x1A (CTRL + Z)`.
        etl::string<MAX_SMS_LEN + 1> sms_command{sms.data(), sms.size()};
        sms_command += '\x1A';

        // The resulting string view gets stored here
        std::array<char, UART_IDLE_LINE_BUF_BYTE> rx_sms_buf{};
        auto                                      rx_sms_str = transact({sms_command.data(), sms_command.size()}, rx_sms_buf);

        if (!rx_sms_str) {
            return utils::error_t::ERR_FAIL;
        }

        // The response should contain "+CMGS:" on success
        return rx_sms_str->contains("+CMGS:") ? utils::error_t::NONE : utils::error_t::GSM_SMS_SEND_FAIL;
    }

    std::expected<std::array<char, IMSI_BUF_SIZE>, utils::error_t> get_imsi() {
        if (!s_is_initialized) {
            return std::unexpected(utils::error_t::ERR_INVALID_STATE);
        }

        // RAII handling for mutex acquisition and releasing
        [[maybe_unused]] mutex_t mutex;
        if (!mutex) {
            return std::unexpected(utils::error_t::ERR_TIMEOUT);
        }

        // Confirm the module is still responding before doing anything
        auto ret = send_cmd_and_compare_result(cmd_t::AT);
        if (ret != utils::error_t::NONE) {
            return std::unexpected(utils::error_t::GSM_MODULE_NOT_ALIVE);
        }

        // We only need to check if the SIM card is available
        // since the IMSI is a static property of the SIM card
        ret = send_cmd_and_compare_result(cmd_t::CHECK_SIM);
        if (ret != utils::error_t::NONE) {
            return std::unexpected(utils::error_t::GSM_SIM_NOT_FOUND);
        }

        // The resulting string view gets stored here
        std::array<char, UART_IDLE_LINE_BUF_BYTE> rx_buf{};
        auto                                      rx_str = transact(AT_CMD_LUT[std::to_underlying(cmd_t::GET_IMSI)].tx, rx_buf);

        if (!rx_str) {
            return std::unexpected(utils::error_t::ERR_FAIL);
        }

        // Makes sure `s_rx_idle_line_size` has enough data before copying any data
        // NOTE: The module is supposed to send a carriage return, a newline, the 15
        // digit IMSI, another carriage and newline, yet another carriage and newline
        // a "OK" and a final carriage and newline, giving us 25 characters total.
        if (rx_str->size() < 25) {
            return std::unexpected(utils::error_t::ERR_FAIL);
        }

        // Extract IMSI: Since the responses have `'\r\n'`, we find the
        // find the first occurrence of them and copy the next 15 characters.
        std::array<char, IMSI_BUF_SIZE> imsi{};

        auto pos = rx_str->find("\r\n");

        // Make sure that the response has enough data elements to account for the length
        // of the expected data and the index we found the "\r\n" at.
        if (pos == std::string_view::npos || rx_str->size() < (pos + 25)) {
            return std::unexpected(utils::error_t::ERR_FAIL);
        }

        // Copy the next 15 characters as our IMSI
        memcpy(imsi.data(), (rx_str->data() + pos + 2), 15);

        return imsi;
    }

} // namespace gsm

extern "C" {
    // UART RX done callback
    void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size) {
        if (huart->Instance == gsm::s_huart.Instance) {
            if (gsm::s_calling_task_handle == nullptr) {
                return;
            }
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

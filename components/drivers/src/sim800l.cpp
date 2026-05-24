#include "stm32f1xx_hal.h"

#include "sim800l.hpp"
#include "config.hpp"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include "etl/atomic.h"

#include <string_view>
#include <cstring>

namespace gsm {

    // Global state. It is what it is
    static UART_HandleTypeDef s_huart{};
    static DMA_HandleTypeDef  s_hdma_tx{};
    static DMA_HandleTypeDef  s_hdma_rx{};
    static TaskHandle_t       s_calling_task_handle{};

    static bool s_is_initialized{};

    // This is needed because we are receiving UART data, we don't
    // know the length of the data we will get. The UART idle line
    // ISR puts the actual length of the data received here.
    static volatile uint32_t s_rx_idle_line_size{};

    static constexpr uint32_t RETRIES{6};
    static constexpr uint32_t DELAY_BETWEEN_RETRIES_MS{5000};

    enum class cmd_t : uint8_t {
        // Initialization
        AT,        // Module alive check
        ECHO_OFF,  // Disable echo
        TEXT_MODE, // SMS text mode
        SET_SMSC,  // GLO SMSC

        // Status checks
        CHECK_SIM,    // Check if SIM card is present and ready
        CHECK_REG,    // Network registration
        CHECK_SIGNAL, // Check signal strength

        // IMSI
        GET_IMSI, // Get the SIM card's IMSI

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
        [std::to_underlying(cmd_t::AT)]           = {"AT\r\n", "OK"},
        [std::to_underlying(cmd_t::ECHO_OFF)]     = {"ATE0\r\n", "OK"},
        [std::to_underlying(cmd_t::TEXT_MODE)]    = {"AT+CMGF=1\r\n", "OK"},
        [std::to_underlying(cmd_t::SET_SMSC)]     = {"AT+CSCA=\"+2348050020020\"\r\n", "OK"},
        [std::to_underlying(cmd_t::CHECK_SIM)]    = {"AT+CPIN?\r\n", "+CPIN: READY"},
        [std::to_underlying(cmd_t::CHECK_REG)]    = {"AT+CREG?\r\n", "+CREG"},
        [std::to_underlying(cmd_t::CHECK_SIGNAL)] = {"AT+CSQ\r\n", "+CSQ"},
        [std::to_underlying(cmd_t::GET_IMSI)]     = {"AT+CIMI\r\n", "OK"},
    }};

    // Forward declarations
    [[nodiscard]] static inline status_t send_cmd_and_compare_result(cmd_t cmd);
    [[nodiscard]] static inline status_t send_init_sequence();

    // Public API
    status_t init() {
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
        s_huart.Init.BaudRate     = 9600U;
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
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
        HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
        HAL_NVIC_SetPriority(USART1_IRQn, 15, 15);
        HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 15, 15);
        HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 15, 15);

        // Send init sequence to GSM mdodule and confirm everything is in order
        auto ret = send_init_sequence();

        if (ret == status_t::OK) {
            s_is_initialized = true;
        }

        return ret;
    }

    void deinit() {
        utils::assert_check(s_is_initialized);

        // Deinitialize the USART and DMA channels
        utils::assert_check(HAL_DMA_DeInit(&s_hdma_tx) == HAL_OK);
        utils::assert_check(HAL_DMA_DeInit(&s_hdma_rx) == HAL_OK);
        utils::assert_check(HAL_UART_DeInit(&s_huart) == HAL_OK);

        s_huart               = {};
        s_hdma_tx             = {};
        s_hdma_rx             = {};
        s_calling_task_handle = nullptr;

        // Disable the corresponding NVIC DMA tx and rx irqs
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

    status_t send_sms(const etl::string_view& sms, const etl::string_view& number) {
        utils::assert_check(s_is_initialized);

        // Confirm the module is still responding before doing anything
        auto ret = send_cmd_and_compare_result(cmd_t::AT);
        if (ret != status_t::OK) {
            return status_t::ERR_MODULE_NOT_ALIVE;
        }

        (void)sms;
        (void)number;

        return status_t::OK;
    }

    status_t get_sim_status() {
        utils::assert_check(s_is_initialized);

        // Confirm the module is still responding before doing anything
        auto ret = send_cmd_and_compare_result(cmd_t::AT);
        if (ret != status_t::OK) {
            return status_t::ERR_MODULE_NOT_ALIVE;
        }

        // Check if the SIM card is present
        ret = send_cmd_and_compare_result(cmd_t::CHECK_SIM);
        if (ret != status_t::OK) {
            return status_t::ERR_SIM_NOT_FOUND;
        }

        // Check signal strength
        // No need to poll here. This is simply a status check
        ret = send_cmd_and_compare_result(cmd_t::CHECK_SIGNAL);
        if (ret != status_t::OK) {
            return status_t::ERR_COULD_NOT_CONNECT;
        }

        return status_t::OK;
    }

    etl::expected<etl::array<char, 16>, status_t> get_imsi() {
        utils::assert_check(s_is_initialized);

        // Confirm the module is still responding before doing anything
        auto ret = send_cmd_and_compare_result(cmd_t::AT);
        if (ret != status_t::OK) {
            return etl::unexpected(status_t::ERR_MODULE_NOT_ALIVE);
        }

        return {};
    }

    // Helpers
    static inline status_t send_cmd_and_compare_result(cmd_t cmd) {

        status_t ret{status_t::OK};

        // Capture calling task since the ISRs send a notification to it
        s_calling_task_handle = xTaskGetCurrentTaskHandle();

        // Send command
        const auto& data = AT_CMD_LUT[std::to_underlying(cmd)];
        utils::assert_check(HAL_UART_Transmit_DMA(&s_huart, reinterpret_cast<const uint8_t*>(data.tx.data()), data.tx.size()) == HAL_OK);

        // Block till the task notification is received from the ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Receive the response from the SIM800L
        etl::array<char, 32> rx_buf{};
        utils::assert_check(HAL_UARTEx_ReceiveToIdle_DMA(&s_huart, reinterpret_cast<uint8_t*>(rx_buf.data()), rx_buf.size()) == HAL_OK);

        // Block till notification received from ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Construct a `std::string_view` from the data received. The UART
        // idle line ISR puts the actual length received in `s_rx_idle_line_size`.
        auto rx_actual = std::string_view{rx_buf.data(), s_rx_idle_line_size};

        // Check the response
        if (!rx_actual.contains(data.rx_expected)) {
            ret = status_t::ERR_GENERIC;
        }

        // Clear the RX dma size variable and calling task handle
        s_rx_idle_line_size   = {};
        s_calling_task_handle = nullptr;

        return ret;
    }

    static inline status_t send_init_sequence() {
        // Confirm the module is responding
        auto ret = send_cmd_and_compare_result(cmd_t::AT);
        if (ret != status_t::OK) {
            return status_t::ERR_MODULE_NOT_ALIVE;
        }

        // Echo mode off
        ret = send_cmd_and_compare_result(cmd_t::ECHO_OFF);
        if (ret != status_t::OK) {
            return ret;
        }

        // Text mode on
        ret = send_cmd_and_compare_result(cmd_t::TEXT_MODE);
        if (ret != status_t::OK) {
            return ret;
        }

        // Set network provider's SMSC: GLO's in this case
        ret = send_cmd_and_compare_result(cmd_t::SET_SMSC);
        if (ret != status_t::OK) {
            return ret;
        }

        // Check if the SIM card is present
        ret = send_cmd_and_compare_result(cmd_t::CHECK_SIM);
        if (ret != status_t::OK) {
            return status_t::ERR_SIM_NOT_FOUND;
        }

        // Check if the SIM card is registered
        // If it's not, treat as it though it was not present
        ret = send_cmd_and_compare_result(cmd_t::CHECK_REG);
        if (ret != status_t::OK) {
            return status_t::ERR_SIM_NOT_FOUND;
        }

        // Check signal strength
        for (uint32_t i{0}; i < RETRIES; i++) {
            ret = send_cmd_and_compare_result(cmd_t::CHECK_SIGNAL);
            if (ret == status_t::OK) {
                return status_t::OK;
            }

            // We poll here since network connection failure is a recoverable error from
            // the module, so we can poll it until we get a stable network connection
            vTaskDelay(pdMS_TO_TICKS(DELAY_BETWEEN_RETRIES_MS));
        }

        // If we get here, we were unable to establish a connection
        return status_t::ERR_COULD_NOT_CONNECT;
    }

} // namespace gsm

extern "C" {
    // UART TX done callback
    void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) {
        if (huart->Instance == gsm::s_huart.Instance) {
            BaseType_t higher_priority_task_woken{};
            vTaskNotifyGiveFromISR(gsm::s_calling_task_handle, &higher_priority_task_woken);
            portYIELD_FROM_ISR(higher_priority_task_woken);
        }
    }

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

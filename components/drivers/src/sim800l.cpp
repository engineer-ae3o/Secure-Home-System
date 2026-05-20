#include "stm32f1xx_hal.h"
#include "sim800l.hpp"
#include "config.hpp"
#include "utils.hpp"


namespace gsm {

    // GLobal state. It is what it is
    static UART_HandleTypeDef s_uart_handle{};
    static DMA_HandleTypeDef s_dma_tx_handle{};
    static DMA_HandleTypeDef s_dma_rx_handle{};

    static bool s_is_initialized{};

    static constexpr uint32_t POLLING_DELAY_MS{2000};

    enum class cmd_t : uint8_t {
        // Initialization
        AT,           // Module alive check
        ECHO_OFF,     // Disable echo
        TEXT_MODE,    // SMS text mode
        SET_SMSC,     // GLO SMSC

        // Status checks
        CHECK_SIM,    // Check if SIM card is present and ready
        CHECK_REG,    // Network registration
        CHECK_SIGNAL, // Check signal strength

        // IMSI
        GET_IMSI,     // Get the SIM card's IMSI

        // Total
        COUNT         // Used to get total number for array declaration
    };

    struct cmd_entry_t {
        const char* tx{};
        const char* rx{};
    };

    // AT commands LUT
    static constexpr etl::array<cmd_entry_t, std::to_underlying(cmd_t::COUNT)> AT_CMD_LUT = {{
        [std::to_underlying(cmd_t::AT)]           = { "AT\r\n",                         "OK" },
        [std::to_underlying(cmd_t::ECHO_OFF)]     = { "ATE0\r\n",                       "OK" },
        [std::to_underlying(cmd_t::TEXT_MODE)]    = { "AT+CMGF=1\r\n",                  "OK" },
        [std::to_underlying(cmd_t::SET_SMSC)]     = { "AT+CSCA=\"+2348050020020\"\r\n", "OK" },
        [std::to_underlying(cmd_t::CHECK_SIM)]    = { "AT+CPIN?\r\n",                   "+CPIN: READY" },
        [std::to_underlying(cmd_t::CHECK_REG)]    = { "AT+CREG?\r\n",                   "+CREG" },
        [std::to_underlying(cmd_t::CHECK_SIGNAL)] = { "AT+CSQ\r\n",                     "+CSQ" },
        [std::to_underlying(cmd_t::GET_IMSI)]     = { "AT+CIMI\r\n",                    "OK" },
    }};

    
    // Public API
    status_t init() {
        utils::assert_check(!s_is_initialized);

        // Configure the UART channel
        __HAL_RCC_USART1_CLK_ENABLE();

        s_uart_handle.Instance = config::GSM_UART_PORT;
        s_uart_handle.Init.BaudRate = 9600U;
        s_uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
        s_uart_handle.Init.StopBits = UART_STOPBITS_1;
        s_uart_handle.Init.Parity = UART_PARITY_NONE;
        s_uart_handle.Init.Mode = UART_MODE_TX_RX;
        s_uart_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
        s_uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;
        utils::assert_check(HAL_UART_Init(&s_uart_handle) == HAL_OK);

        // Configure the GPIOs
        __HAL_RCC_GPIOA_CLK_ENABLE();

        // TX
        GPIO_InitTypeDef tx_init = {
            .Pin = config::GSM_GPIO_TX.pin,
            .Mode = GPIO_MODE_AF_PP,
            .Pull = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW
        };
        HAL_GPIO_Init(config::GSM_GPIO_TX.port, &tx_init);

        // RX
        GPIO_InitTypeDef rx_init = {
            .Pin = config::GSM_GPIO_RX.pin,
            .Mode = GPIO_MODE_INPUT,
            .Pull = GPIO_PULLUP,
            .Speed = GPIO_SPEED_FREQ_LOW
        };
        HAL_GPIO_Init(config::GSM_GPIO_RX.port, &rx_init);

        // Configure the DMA channels
        __HAL_RCC_DMA1_CLK_ENABLE();

        // TX
        s_dma_tx_handle.Instance = config::GSM_UART_DMA_TX;
        s_dma_tx_handle.Init.Direction = DMA_MEMORY_TO_PERIPH;
        s_dma_tx_handle.Init.PeriphInc = DMA_PINC_DISABLE;
        s_dma_tx_handle.Init.MemInc = DMA_MINC_ENABLE;
        s_dma_tx_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        s_dma_tx_handle.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        s_dma_tx_handle.Init.Mode = DMA_NORMAL;
        s_dma_tx_handle.Init.Priority = DMA_PRIORITY_VERY_HIGH;
        utils::assert_check(HAL_DMA_Init(&s_dma_tx_handle) == HAL_OK);

        // RX
        s_dma_rx_handle.Instance = config::GSM_UART_DMA_RX;
        s_dma_rx_handle.Init.Direction = DMA_PERIPH_TO_MEMORY;
        s_dma_rx_handle.Init.PeriphInc = DMA_PINC_DISABLE;
        s_dma_rx_handle.Init.MemInc = DMA_MINC_ENABLE;
        s_dma_rx_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        s_dma_rx_handle.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        s_dma_rx_handle.Init.Mode = DMA_NORMAL;
        s_dma_rx_handle.Init.Priority = DMA_PRIORITY_VERY_HIGH;
        utils::assert_check(HAL_DMA_Init(&s_dma_rx_handle) == HAL_OK);

        // Enable the NVIC irqs and set priorities to lowest
        NVIC_EnableIRQ(DMA1_Channel4_IRQn);
        NVIC_EnableIRQ(DMA1_Channel5_IRQn);
        NVIC_SetPriority(DMA1_Channel4_IRQn, 15);
        NVIC_SetPriority(DMA1_Channel5_IRQn, 15);
        
        // Send AT commands to the module to confirm everything is ok
        

        s_is_initialized = true;

        return status_t::OK;
    }

    void deinit() {
        utils::assert_check(s_is_initialized);

        // Deinitialize the USART and DMA channels
        utils::assert_check(HAL_UART_DeInit(&s_uart_handle) == HAL_OK);
        utils::assert_check(HAL_DMA_DeInit(&s_dma_tx_handle) == HAL_OK);
        utils::assert_check(HAL_DMA_DeInit(&s_dma_rx_handle) == HAL_OK);

        s_uart_handle = {};
        s_dma_tx_handle = {};
        s_dma_rx_handle = {};

        // Set TX and RX pins as analog
        GPIO_InitTypeDef gpio_deinit = {
            .Pin = (config::GSM_GPIO_TX.pin | config::GSM_GPIO_RX.pin),
            .Mode = GPIO_MODE_ANALOG,
            .Pull = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW
        };
        HAL_GPIO_Init(config::GSM_GPIO_TX.port, &gpio_deinit);
        
        s_is_initialized = false;
    }

    status_t send_sms() {
        utils::assert_check(!s_is_initialized);

        return status_t::OK;
    }

    status_t get_sim_status() {
        utils::assert_check(!s_is_initialized);
        
        return status_t::OK;
    }
    
    etl::expected<uint32_t, status_t> get_imsi() {
        utils::assert_check(!s_is_initialized);

        return 0U;
    }

    extern "C" {
        // DMA TX irq handler
        void DMA1_Channel4_IRQHandler() {
            
        }

        // DMA RX irq handler
        void DMA1_Channel5_IRQHandler() {
            
        }
    }

} // namespace gsm

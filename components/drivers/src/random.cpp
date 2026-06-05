#include "stm32f1xx_hal.h"

#include "random.hpp"
#include "config.hpp"
#include "file.hpp"
#include "utils.hpp"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "ascon/random.h"

#include <array>

namespace rnd {

    namespace {
        // The Random Number Generator being used. Only one instance is needed.
        ascon_random_state_t s_rng_state{};

        // ASCON storage interface for saving and loading the seed from non-volatile storage
        constexpr ascon_storage_t s_ascon_storage = {
            .page_size      = file::PROG_SIZE_BYTES,
            .erase_size     = file::BLOCK_SIZE_BYTES,
            .address        = 0, // Not used since our read/write functions ignore the offset and always operate at offset zero
            .size           = file::BLOCK_SIZE_BYTES,
            .partial_writes = 0,
            .read =
                [](const ascon_storage_t* storage, size_t offset, unsigned char* data, size_t size) {
                    (void)storage;
                    (void)offset;

                    // We read the file at offset zero because `ascon_random_load_seed()` loads data at
                    // offset zero of the storage region, and the file size is exactly the same as the
                    // seed size, so we can be sure that the entire seed is read in one operation.
                    file::read(file::name_t::ASCON_SEED, {data, size});

                    return static_cast<int>(size);
                },
            .write =
                [](const ascon_storage_t* storage, size_t offset, const unsigned char* data, size_t size, int flags) {
                    (void)storage;
                    (void)offset;
                    (void)flags;

                    // We write the file at offset zero because `ascon_random_save_seed()` saves data at
                    // offset zero of the storage region, and the file size is exactly the same as the
                    // seed size, so we can be sure that the entire seed is written in one operation.
                    file::write(file::name_t::ASCON_SEED, {data, size});
                    file::sync(file::name_t::ASCON_SEED);

                    return static_cast<int>(size);
                },
        };

        // ADC, DMA, and RTC handles
        ADC_HandleTypeDef s_adc_handle{};
        DMA_HandleTypeDef s_dma_handle{};
        RTC_HandleTypeDef s_rtc_handle{};

        // Measured jitter between the TIM2 peripheral and the RTC running on the LSI.
        volatile uint32_t s_rtc_jitter{};

        // Six internal channels + the internal temperature sensor
        constexpr uint8_t ADC_NUM_CHANNELS{7};

        // DMA buffer. Half word sized load and stores are atomic on CM3, so we can safely read
        // from this buffer without worrying about concurrency issues with the DMA controller.
        std::array<uint16_t, ADC_NUM_CHANNELS> s_adc_buf{};

        // Stores the boot cycle count
        uint32_t s_boot_cycle_count{};

        // Tasks TCBs and Stacks
        constexpr uint32_t                                ENTROPY_TASK_STACK_BYTES{512};
        TaskHandle_t                                      entropy_task_handle{};
        std::array<StackType_t, ENTROPY_TASK_STACK_BYTES> entropy_task_stack{};
        StaticTask_t                                      entropy_task_tcb{};

        // Synchronization across multi threaded access to the rand API
        SemaphoreHandle_t s_task_mutex{};
        StaticSemaphore_t s_task_mutex_buffer{};

        // RAII helper for taking and freeing the mutex
        struct mutex_t {
        public:
            // We block forever because because access to the rand API is critical
            // to the operation of the system, so we can afford to block indefinitely
            // till we take the mutex.
            mutex_t() {
                xSemaphoreTakeRecursive(s_task_mutex, portMAX_DELAY);
            }

            ~mutex_t() {
                xSemaphoreGiveRecursive(s_task_mutex);
            }

            mutex_t(const mutex_t&)            = delete;
            mutex_t& operator=(const mutex_t&) = delete;
            mutex_t(mutex_t&&)                 = delete;
            mutex_t& operator=(mutex_t&&)      = delete;
        };

        // Helpers
        uint8_t xor_bytes_in_word(uint32_t value) {
            // Fold all four bytes of the input value into a single byte by XORing them together.
            // This is a simple way to extract some entropy from a 32-bit value, and it helps to
            // ensure that changes in any of the bytes will affect the output.
            const uint32_t t1 = value ^ (value >> 8);
            const uint32_t t2 = t1 ^ (t1 >> 16);

            return static_cast<uint8_t>(t2);
        }

        // This function gets the boot cycle counter, timing jitter between TIM2 running on the main PLL and the RTC
        // running on the LSI and ADC samples on seven channels and mixes them together to be used as our entropy source.
        uint8_t get_entropy_mixture() {
            [[maybe_unused]] mutex_t mutex;

            // We use a simple XOR-based mixture function to mix the different sources of entropy together.
            // This is not a cryptographically secure way to mix entropy, but it is sufficient for our
            // purposes since we are feeding the output into a CSPRNG that will further mix the entropy and
            // produce high-quality random numbers. The entropy variable has static duration so that it retains
            // its value across multiple calls to this function, instead of being re-initialized to zero on each
            // call, which allows us to accumulate entropy over time.
            static uint8_t entropy{};

            // Used as a means to ensure that the entropy mixture changes across calls to this function.
            static uint32_t entropy_source_counter{};
            entropy ^= xor_bytes_in_word(entropy_source_counter++);

            // The HAL tick source has already been configured to use TIM2
            // as its clock source. Mix in the entropy from TIM2 and the RTC.
            entropy ^= xor_bytes_in_word(s_rtc_jitter);

            // Pack the LSBs of all the ADC samples before use
            uint32_t packed_adc_samples{};
            for (uint32_t shift{}; const auto sample : s_adc_buf) {
                packed_adc_samples |= (sample & 0xFU) << shift;
                shift += 4;
            }
            entropy ^= xor_bytes_in_word(packed_adc_samples);

            // Mix in the boot cycle count, which tracks the number of boot cycles since first boot of the device.
            // The boot cycle count is more so used as a way to ensure that the entropy mixture changes across
            // reboots, since the other sources of entropy could be potentially similar on each boot.
            entropy ^= xor_bytes_in_word(s_boot_cycle_count);

            // Individual sources of entropy are not very strong on their own, but by mixing them
            // together we can create a stronger overall source of entropy for seeding our CSPRNG.
            return entropy;
        }

        [[noreturn]] void entropy_task(void* arg) {
            (void)arg;

            while (true) {
                // Get entropy to feed the RNG
                std::array<uint8_t, 32> entropy{};

                for (auto& data : entropy) {
                    data = get_entropy_mixture();
                    // Wait some time between calls to the get entropy function to allow
                    // the ADC readings on the channels to change, even a little bit.
                    vTaskDelay(pdMS_TO_TICKS(30));
                }

                {
                    [[maybe_unused]] mutex_t mutex;

                    // Feed the entropy into ASCON and save the current seed to flash
                    ascon_random_feed(&s_rng_state, entropy.data(), entropy.size());
                    utils::assert_check(ascon_random_save_seed(&s_rng_state, &s_ascon_storage) == 0);
                }

                // We gather entropy to update the ASCON seed every 60s
                vTaskDelay(pdMS_TO_TICKS(60'000));
            }
        }

    } // namespace

    // Public API
    void init() {
        // Create the mutex. No need to take it. This function is called from a task.
        s_task_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_task_mutex_buffer);

        // Configure the GPIO pins to be used for ADC sampling
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_DMA1_CLK_ENABLE();

        GPIO_InitTypeDef pins_a = {
            .Pin   = static_cast<uint32_t>(config::ADC_PINS[0].pin | config::ADC_PINS[1].pin | config::ADC_PINS[2].pin |
                                         config::ADC_PINS[3].pin),
            .Mode  = GPIO_MODE_ANALOG,
            .Pull  = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW,
        };

        GPIO_InitTypeDef pins_b = {
            .Pin   = static_cast<uint32_t>(config::ADC_PINS[4].pin | config::ADC_PINS[5].pin),
            .Mode  = GPIO_MODE_ANALOG,
            .Pull  = GPIO_NOPULL,
            .Speed = GPIO_SPEED_FREQ_LOW,
        };

        HAL_GPIO_Init(config::ADC_PINS[0].port, &pins_a);
        HAL_GPIO_Init(config::ADC_PINS[4].port, &pins_b);

        // Configure the DMA controller
        s_dma_handle.Instance                 = DMA1_Channel1;
        s_dma_handle.Init.Direction           = DMA_PERIPH_TO_MEMORY;
        s_dma_handle.Init.PeriphInc           = DMA_PINC_DISABLE;
        s_dma_handle.Init.MemInc              = DMA_MINC_ENABLE;
        s_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        s_dma_handle.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
        s_dma_handle.Init.Mode                = DMA_CIRCULAR;
        s_dma_handle.Init.Priority            = DMA_PRIORITY_LOW;

        utils::assert_check(HAL_DMA_Init(&s_dma_handle) == HAL_OK);
        __HAL_LINKDMA(&s_adc_handle, DMA_Handle, s_dma_handle);

        // Configure the ADC
        s_adc_handle.Instance                   = ADC1;
        s_adc_handle.Init.ScanConvMode          = ADC_SCAN_ENABLE;
        s_adc_handle.Init.ContinuousConvMode    = ENABLE;
        s_adc_handle.Init.DiscontinuousConvMode = DISABLE;
        s_adc_handle.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
        s_adc_handle.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
        s_adc_handle.Init.NbrOfConversion       = ADC_NUM_CHANNELS;
        utils::assert_check(HAL_ADC_Init(&s_adc_handle) == HAL_OK);

        // External channels
        constexpr std::array<uint32_t, ADC_NUM_CHANNELS - 1> ext_channels = {
            ADC_CHANNEL_3,
            ADC_CHANNEL_4,
            ADC_CHANNEL_5,
            ADC_CHANNEL_7,
            ADC_CHANNEL_8,
            ADC_CHANNEL_9,
        };

        ADC_ChannelConfTypeDef reg_group_scan{};

        for (size_t idx{0}; const auto channel : ext_channels) {
            reg_group_scan.Channel      = channel;
            reg_group_scan.Rank         = idx + 1;
            reg_group_scan.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
            utils::assert_check(HAL_ADC_ConfigChannel(&s_adc_handle, &reg_group_scan) == HAL_OK);
            idx++;
        }

        // Internal temperature sensor
        reg_group_scan.Channel      = ADC_CHANNEL_TEMPSENSOR;
        reg_group_scan.Rank         = ADC_NUM_CHANNELS;
        reg_group_scan.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
        utils::assert_check(HAL_ADC_ConfigChannel(&s_adc_handle, &reg_group_scan) == HAL_OK);

        // No point in calibrating the ADC. We don't care since we need as much noise as we can get

        // Start ADC conversion
        utils::assert_check(HAL_ADC_Start_DMA(&s_adc_handle, reinterpret_cast<uint32_t*>(s_adc_buf.data()), ADC_NUM_CHANNELS) == HAL_OK);

        // Configure the RTC peripheral to use the LSI as its clock source, which is a low frequency
        // internal oscillator that has a lot of jitter, making it a good source of entropy for the RNG.
        __HAL_RCC_LSI_ENABLE();
        while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET) {
        }

        // Enable clocks for the power and backup units
        __HAL_RCC_PWR_CLK_ENABLE();
        __HAL_RCC_BKP_CLK_ENABLE();

        // Required to write to the backup domain
        HAL_PWR_EnableBkUpAccess();

        // Set the LSI as clock source for the RTC and then enable the RTC peripheral
        __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
        __HAL_RCC_RTC_ENABLE();

        // Configure the RTC
        s_rtc_handle.Instance          = RTC;
        s_rtc_handle.Init.AsynchPrediv = 39; // RTC clock frequency of 40kHz / (39  + 1) = 1kHz
        s_rtc_handle.Init.OutPut       = RTC_OUTPUTSOURCE_NONE;
        utils::assert_check(HAL_RTC_Init(&s_rtc_handle) == HAL_OK);

        utils::assert_check(HAL_RTCEx_SetSecond_IT(&s_rtc_handle) == HAL_OK);

        HAL_NVIC_SetPriority(RTC_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(RTC_IRQn);

        // Get the boot cycle count
        s_boot_cycle_count = file::get_boot_cycle_count();

        if (s_boot_cycle_count == 0) {
            // If this is the first boot, we block for 1.5s to ensure the RTC interrupt fires at
            // least once so it gives us the jitter we need when we call `ascon_random_init(...)`
            // and `ascon_random_load_seed(...)` since those call `ascon_trng_get_bytes(...)` which
            // calls `get_entropy_mixture(...)` which relies on the RTC-TIM2 jitter.
            vTaskDelay(pdMS_TO_TICKS(1500));
        }

        // FInally, initialize the ASCON random state and load the seed from flash storage
        // ASCON's random API uses inconsistent error codes. Can't be helped.
        utils::assert_check(ascon_random_init(&s_rng_state) != 0);
        utils::assert_check(ascon_random_load_seed(&s_rng_state, &s_ascon_storage) == 0);

        // Create FreeRTOS task to periodically gather entropy
        // It has a very low priority since its a background task
        entropy_task_handle = xTaskCreateStatic(entropy_task,
                                                "Entropy Task",
                                                utils::bytes_to_words(ENTROPY_TASK_STACK_BYTES),
                                                {},
                                                1,
                                                entropy_task_stack.data(),
                                                &entropy_task_tcb);
    }

    void deinit() {
        utils::assert_check(HAL_ADC_Stop_DMA(&s_adc_handle) == HAL_OK);
        utils::assert_check(HAL_DMA_DeInit(&s_dma_handle) == HAL_OK);
        utils::assert_check(HAL_ADC_DeInit(&s_adc_handle) == HAL_OK);
        utils::assert_check(HAL_RTC_DeInit(&s_rtc_handle) == HAL_OK);

        // Delete the task before deletion of the mutex
        vTaskDelete(entropy_task_handle);

        {
            [[maybe_unused]] mutex_t mutex;
            ascon_random_free(&s_rng_state);
        }

        vSemaphoreDelete(s_task_mutex);
    }

    void get_random_numbers(std::span<uint8_t> buffer) {
        [[maybe_unused]] mutex_t mutex;
        ascon_random_fetch(&s_rng_state, buffer.data(), buffer.size());
    }

} // namespace rnd

extern "C" {
    // Used by ASCON's TRNG implementation to get random bytes
    int ascon_trng_get_bytes(unsigned char* out, size_t outlen) {
        for (size_t i{0}; i < outlen; i++) {
            out[i] = rnd::get_entropy_mixture();
        }
        return static_cast<int>(outlen);
    }

    void HAL_RTCEx_RTCEventCallback(RTC_HandleTypeDef* hrtc) {
        (void)hrtc;

        // Get jitter from the counter register in TIM2, which is the timer used for
        // the HAL tick. Since the RTC is running on a separate clock source (the LSI)
        // that is inacurrate, this ISR gets called at a fairly random point while TIM2
        // is running due to the difference in their clock accuracies.
        rnd::s_rtc_jitter = TIM2->CNT;
    }

    void RTC_IRQHandler() {
        HAL_RTCEx_RTCIRQHandler(&rnd::s_rtc_handle);
    }

} // extern "C"

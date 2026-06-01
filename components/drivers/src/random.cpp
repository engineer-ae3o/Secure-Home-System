#include "stm32f1xx_hal.h"

#include "random.hpp"
#include "config.hpp"
#include "flash.hpp"
#include "utils.hpp"

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

        // ADC and DMA handles
        ADC_HandleTypeDef s_adc_handle{};
        DMA_HandleTypeDef s_dma_handle{};

        // Six internal channels + the internal temperature sensor
        constexpr uint8_t ADC_NUM_CHANNELS = 7;

        // DMA buffer. Half word sized load and stores are atomic on CM3, so we can safely read
        // from this buffer without worrying about concurrency issues with the DMA controller.
        std::array<uint16_t, ADC_NUM_CHANNELS> s_adc_buf{};

        // This function gets timing jitter between TIM2 runnimg on the main PLL and the LSI and ADC
        // samples on some floating pins and mixes them together to be used as our entropy source.
        uint8_t get_entropy_mixture();

    } // namespace

    // Public API
    void init() {
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

        for (size_t idx{0}; const auto& channel : ext_channels) {
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

        // Apply calibration
        utils::assert_check(HAL_ADCEx_Calibration_Start(&s_adc_handle) == HAL_OK);

        // Start ADC conversion
        utils::assert_check(HAL_ADC_Start_DMA(&s_adc_handle, reinterpret_cast<uint32_t*>(s_adc_buf.data()), ADC_NUM_CHANNELS) == HAL_OK);

        // FInally, initialize the ASCON random state and load the seed from flash storage
        utils::assert_check(ascon_random_init(&s_rng_state) != 0);
        utils::assert_check(ascon_random_load_seed(&s_rng_state, &s_ascon_storage) == 0);
    }

    void deinit() {
        ascon_random_free(&s_rng_state);
    }

    void get_random_numbers(std::span<uint8_t> buffer) {
        ascon_random_fetch(&s_rng_state, buffer.data(), buffer.size());
    }

    void update_rng_state() {
        // Get entropy to feed the RNG
        std::array<uint8_t, 32> entropy{};

        for (auto& data : entropy) {
            data = get_entropy_mixture();
        }
        ascon_random_feed(&s_rng_state, entropy.data(), entropy.size());

        // Save the current seed to flash
        utils::assert_check(ascon_random_save_seed(&s_rng_state, &s_ascon_storage) == 0);
    }

    namespace {
        uint8_t get_entropy_mixture() {
            // TODO: Implement mixing of ADC samples on floating pins and jitter between the LSI and TIM2
            return 0;
        }
    } // namespace

} // namespace rnd

extern "C" {
    // Used by ASCON's TRNG implementation to get random bytes
    int ascon_trng_get_bytes(unsigned char* out, size_t outlen) {
        for (size_t i{0}; i < outlen; i++) {
            out[i] = static_cast<unsigned char>(rnd::get_entropy_mixture() & 0xFFU);
        }
        return static_cast<int>(outlen);
    }
} // extern "C"

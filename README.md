# STM32F103 Home Security System

A home security system built on the STM32F103C8T6 (Blue Pill), running FreeRTOS, written in C++23 against STMicroelectronics's HAL. The project integrates a GSM module for SMS alerting, an ASCON-based cryptographic layer for securing stored credentials, and a multi-source entropy pool for RNG seeding.

---

## Target Hardware

| Component | Part |
|-----------|------|
| MCU | STM32F103C8T6 |
| GSM module | SIM800L |
| Display | HD44780 16×2 (PCF8574 I2C backpack) |
| Keypad | 4×4 matrix |
| Reed switch | NC reed |
| Tamper switch | NC limit |
| Flash FS | Internal flash (LittleFS) |

### Pin Map

```
GPIOA
  PA0–PA2, PA6        Keypad rows 0–3 (output push-pull, default LOW)
  PA3–PA5, PA7        ADC channels 3–5, 7 (floating, entropy)
  PA9 / PA10          USART1 TX / RX (GSM)

GPIOB
  PB0 / PB1           ADC channels 8–9 (floating, entropy)
  PB3–PB5, PB8        Keypad columns 0–3 (input pull-up, EXTI falling)
  PB6 / PB7           I2C1 SCL / SDA (LCD)
  PB9                 LCD backlight LED (output push-pull)

GPIOC
  PC14                Reed switch (input pull-up, EXTI rising)
  PC15                Tamper switch (input pull-up, EXTI rising)
```

---

## Clock Architecture

The clock tree is configured manually in `syscalls.cpp`.

**Core clock**: HSE (8 MHz) → PLL ×9 → **72 MHz**  
**APB1**: 36 MHz (≤ 36 MHz limit)  
**APB2**: 72 MHz

Flash is configured for 2 wait states (`FLASH_ACR_LATENCY_1`: the macro naming is ST's fault) with the prefetch buffer enabled before the PLL is brought up.

### HAL Tick vs FreeRTOS Tick

FreeRTOS owns SysTick. The HAL tick is therefore redirected to TIM2, configured as a 1 kHz free-running counter. This matters for the entropy subsystem: `TIM2->CNT` is read directly from the RTC second interrupt to measure oscillator jitter.

`vPortSetupTimerInterrupt()` configures SysTick directly for the FreeRTOS tick rate. `vApplicationIdleHook()` executes WFI on every idle cycle.

---

## Module Overview

### `syscalls.cpp`

Beyond clock init and HAL tick redirection, this file covers:

- **Fault handlers**: HardFault, BusFault, and UsageFault handlers are implemented as naked functions that select the correct stack pointer (MSP or PSP based on EXC_RETURN bit 2) and branch to typed dump functions. Each dump function captures the full exception frame (r0–r3, r12, LR, PC, xPSR) plus `SCB->CFSR` and `SCB->BFAR` into `volatile` locals that survive in the debugger's register view, then hits `BKPT #0`.
- **No heap**: Dynamic allocation is not available. Everything is statically allocated.
- **SCB hardening**: `DIV_0_TRP` and `UNALIGN_TRP` are enabled at startup so divide-by-zero and unaligned accesses fault immediately rather than silently producing garbage.

### `file.cpp`: LittleFS Abstraction

Files are stored on a 32 KB region of internal flash (32 × 1 KB blocks, 5000 erase cycles). LittleFS handles wear leveling.

Four logical files exist:

| Enum | Path (obfuscated) | Contents |
|------|-------------------|----------|
| `COUNTER` | `fchdvqv` | 32-bit boot cycle count, opened/closed only at init |
| `PASSWORD` | `yacnywo` | ASCON-hashed password |
| `PNUMBERS` | `cqwogto` | ASCON-encrypted phone numbers |
| `ASCON_SEED` | `sgscjhw` | CSPRNG seed state |

File names in flash are intentionally obfuscated. This is weak protection but adds noise against casual flash dumps: the actual sensitive data is cryptographically protected regardless.

The `COUNTER` file is opened, read, incremented and closed at bootup. The `PNUMBERS`, `ASCON_SEED` and `PASSWORD` files stay open for the lifetime of the system. Write caching is per-file via LittleFS's `lfs_file_config` buffer mechanism; `sync()` forces a commit to flash.

Flash programming on STM32F1 is halfword (16-bit) granular. The LittleFS `prog` callback writes in 2-byte units, unlocking and relocking the flash controller around each page program sequence.

### `csprng.cpp`: Entropy and CSPRNG

The RNG is an ASCON-based CSPRNG (`ascon_random_state_t`) seeded from four mixed sources:

**1. TIM2/RTC oscillator jitter**  
The RTC is driven by the LSI (~40 kHz, ±30%, intentionally inaccurate). An interrupt fires from the RTC ISR every 1s and reads `TIM2->CNT`. Because TIM2 runs on the main PLL and the LSI is a completely independent, unstable oscillator, the CNT value at interrupt time is not predictable across interrupts. This is the strongest entropy source.

**2. ADC noise**  
Six external channels (PA3–PA5, PA7, PB0, PB1) plus the internal temperature sensor are sampled continuously via DMA in circular mode. Only the 4 LSBs of each 12-bit sample are used, since the upper bits are largely dominated by the signal and the noise lives at the bottom.

**3. Boot cycle count**  
Persisted in flash. Ensures the entropy mixture differs across reboots even if the ADC and jitter sources produce similar values early in boot.

**4. Call counter**  
A 32-bit monotonically incrementing counter XORed in on every call to `get_entropy_mixture()`. Ensures the output changes across rapid sequential calls within a single boot.

All four sources are folded into a single `uint8_t` via XOR with a byte-folding reduction (`value ^ (value >> 8) ^ ...`). The accumulator has static storage duration; it retains its state across calls rather than resetting, so entropy accumulates over time.

**First boot handling**: `ascon_random_init()` and `ascon_random_load_seed()` both call `ascon_trng_get_bytes()` (the ASCON library's TRNG hook), which calls `get_entropy_mixture()`. On first boot the RTC hasn't fired yet. A 1.5s blocking delay in `init()` guarantees at least one RTC second interrupt fires before ASCON init proceeds.

A low-priority FreeRTOS task (`entropy_task`) wakes every 60s, accumulates 32 bytes of entropy (with 30 ms gaps between each byte to allow ADC readings to drift), feeds them into ASCON, and saves the updated seed to flash. If seed writes fail 5 consecutive times, `utils::panic()` is called.

The seed is persisted on every 60s cycle so that if power is lost, the next boot starts from a non-zero seed with meaningful prior state.

### `sim800l.cpp`: GSM Driver

The SIM800L communicates over USART1 at 57600 baud with DMA on both TX (DMA1 CH4) and RX (DMA1 CH5). Reception uses `HAL_UARTEx_ReceiveToIdle_DMA()`: the UART idle line interrupt fires when the module stops transmitting, triggering `HAL_UARTEx_RxEventCallback`. The callback stores the received byte count and sends a task notification to the calling task, which blocks on `ulTaskNotifyTake()` with a configurable timeout.

A `cleanup_t` RAII guard zeros `s_rx_idle_line_size` and `s_calling_task_handle` on every transaction exit, preventing stale state from a timed-out transaction leaking into the next one.

AT command handling is table-driven. The response from the GSM module when the `CHECK_SIGNAL` and `CHECK_REG` AT commands are sent require parsing; all other commands use substring matching against an expected response.

**Init sequence**:
1. Send `AT` up to 10 times at 250 ms intervals until `OK` is received (handles SIM800L autobaud settling)
2. Echo off, SMS text mode, set SMSC
3. Check SIM presence, network registration
4. Poll signal strength up to 6 times at 5s intervals

`get_imsi()` returns `std::expected<std::array<char, 16>, error_t>`. The IMSI response format is `\r\n{15 digits}\r\n\r\nOK\r\n` (25 bytes minimum); parsing finds the first `\r\n` and copies the 15 characters that follow.

### `hd44780.cpp`: LCD Driver

The HD44780 is wired via a PCF8574 I2C backpack at address `0x27`. Communication is 4-bit mode. The backpack byte layout is `[D7 D6 D5 D4 | BL | EN | RW | RS]`.

ST's `HAL_I2C_Master_Transmit` requires the 7-bit address left-shifted by 1 (the hardware does this shift on most other platforms). This is handled at the call site.

Initialization follows the HD44780 datasheet power-on sequence with explicit delays between each step. The display is configured for 4-bit mode, 2 lines, 5×8 dots, cursor off, display on.

`println()` accepts a `pad_to_whitespace` parameter (default true) that pads the remainder of the 16-character line with spaces. This prevents ghost characters from a previous longer string persisting on the display.

### `keypad.hpp`: 4×4 Matrix Keypad (Header-Only Template)

`keypad_t<N>` is a header-only template parameterized by queue length. Rows are driven LOW by default. Any key press pulls a column LOW, triggering the falling-edge EXTI interrupt on that column pin.

The ISR (`irq_handler()`) does three things:
1. Clears all column EXTI pending bits
2. Masks all column EXTI lines in `EXTI->IMR` (disables further interrupts globally; no per-pin granularity needed)
3. Starts a 50 ms FreeRTOS software timer

The debounce timer callback (`debounce_timer_cb`) does the actual key identification:
1. Drives all rows HIGH
2. For each row: drives that row LOW and reads all four column pins
3. First LOW column found → key is `KEYS[row][col]`, pushed to the event queue with timeout 0 (drops on full queue)
4. Restores all rows to LOW
5. Clears pending EXTI bits again and unsets the `EXTI->IMR` mask

### `switch.hpp`: NC Switch Detection (Header-Only Template)

`switch_t<type>` handles both the reed switch and tamper switch. NC switches are wired with pull-ups; opening the switch (intrusion) drives the pin HIGH, triggering the rising-edge EXTI interrupt.

`irq_handler()` sends a task notification to `calling_task_handle` using `eSetBits` with `std::to_underlying(type)` as the value. Reed uses bit 0 (`0x01`), limit/tamper uses bit 1 (`0x02`). A waiting task calls `xTaskNotifyWait()` to receive and distinguish which switch fired; the bits can be ORed together if both fire before the task wakes.

The calling task handle is injected via `config_t` at `init()` time rather than captured at IRQ time, which means the target task must be running before the switch can be armed.

---

## Error Handling

All fallible functions return `utils::error_t`, a `[[nodiscard]]` scoped enum.

Two propagation macros:

```cpp
TRY(func)      // Returns the error_t if func != `utils::error_t::NONE`
TRY_HAL(hal_func)  // Returns `utils::error_t::ERR_HAL_FAIL` if hal_func != HAL_OK
```

Functions that return values alongside errors use `std::expected<T, error_t>` (`get_boot_cycle_count()`, `get_event_queue()`, `get_imsi()`).

`utils::panic()` executes `BKPT #0` followed by an infinite loop. It is marked as `[[noreturn]]` and used for unrecoverable states (5 consecutive seed-save failures, stack overflow, unhandled library errors).

---

## Cryptography

ASCON is used for all cryptographic operations. It won the NIST Lightweight Cryptography standardization competition in 2023 and is specifically designed for constrained hardware.

**Password storage**: Passwords are hashed with ASCON-Hash256 (and salted). The digest is stored in the `PASSWORD` file. Comparison is done by hashing the input and comparing to the stored value.

**Phone number storage**: Phone numbers are encrypted with ASCON-AEAD128 before being written to the `PNUMBERS` file. A nonce is rotated on every boot using the boot cycle counter, ensuring ciphertext differs across boots even for identical plaintext.

**CSPRNG**: `ascon_random_fetch()` is used for all random output. `ascon_random_feed()` injects new entropy every 60 seconds. `ascon_random_save_seed()` and `ascon_random_load_seed()` persist state to/from the `ASCON_SEED` file via the `ascon_storage_t` interface, which this project implements using the `file` module.

---

## Concurrency Model

| Resource | Protection |
|----------|-----------|
| Flash / LittleFS | Non-recursive mutex (taken for duration of each call) |
| ASCON RNG state | Recursive mutex (entropy task feeds asynchronously) |
| GSM UART | Recursive mutex with timeout; caller blocks on task notification for DMA completion |
| Keypad events | FreeRTOS queue (producer: timer task; consumer: application task) |
| Switch events | Task notification with `eSetBits` (producer: ISR; consumer: application task) |

All mutex wrappers are RAII structs.

---

## Build

The project uses CMake directly against the HAL. The HAL, LittleFS, ASCON, FreeRTOS, and ETL libraries are cloned abd stored in this repository directly.

`_sbrk` returns an error, so the linker heap section can be removed from the linker script entirely. The LittleFS partition must be mapped to a flash region that the linker script does not overlap with `.text` or `.data`. A symbol `lfs_start` is exported from the linker script and referenced in `file.cpp` to locate the partition base address at runtime.

---

## Testing

Tests use the Unity test framework running on device (not hosted). Each module's test runner is a `test_all()` function called from a FreeRTOS task.

**Dependency order**: `file` must be initialized before `random` (the RNG reads the boot cycle count during init). All other modules are independent.

**Switch and keypad tests** use a manual IRQ simulation pattern:
1. Disable relevant NVIC lines
2. Set GPIO to the expected state (output-driven)
3. Call `irq_handler()` directly
4. Block on `xTaskNotifyWait()` or `xQueueReceive()` with a timeout
5. Assert received value, restore GPIO, re-enable NVIC

For the keypad specifically, the target column pin must be reconfigured as output push-pull and driven LOW before calling `irq_handler()`: the debounce timer callback reads the actual IDR register during scanning, so the GPIO state must be physically correct at callback execution time (~50 ms after the trigger).

**Statistical RNG tests** use 32–64 byte sample sizes. The probability of a false failure on any assertion (all-zero output, all-identical bytes, byte diversity threshold) is below 2⁻²⁵⁶.

---

## External Dependencies

- **ST HAL:** `stm32f1xx_hal`
- **FreeRTOS:** task, queue, timer, semaphore APIs
- **LittleFS:** filesystem for internal flash
- **ASCON:** Ascon-AEAD128 cipher, Ascon-Hash256 and Ascon's CSPRNG
- **ETL (Embedded Template Library):** `etl::string` used in the GSM driver for building AT command strings without heap allocation
- **Unity:** test framework (on-target)

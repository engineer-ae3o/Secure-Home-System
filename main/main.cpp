//#define STM32F103xB
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_gpio.h"

extern "C" {

    void delay_cycles(uint32_t cycles) {
        while (cycles--) {
            __asm("nop");
        }
    }

    void delay_ms(uint32_t ms) {
        delay_cycles(ms * 72000);
    }
    
    [[noreturn]] int main() {

        GPIO_InitTypeDef init = {
            .Pin = GPIO_PIN_13,
            .Mode = GPIO_MODE_OUTPUT_PP,
            .Pull = GPIO_NOPULL,
            .Speed = GPIO_SPEED_LOW
        };
        HAL_GPIO_Init(GPIOC, &init);
        
        while (1) {
            HAL_GPIO_TogglePin(GPIOC, 13);
            delay_ms(500);
        }
    }
}

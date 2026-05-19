#pragma once


#include "etl/string.h"


namespace lcd {
    
    constexpr inline uint8_t ROWS{2};
    constexpr inline uint8_t COLUMNS{16};

    /**
     * @brief Initializes the HD44780 display controller as
     *        well as the I2C and GPIO interfaces
     * 
     * @note Asserts on internal failure, or when the function
     *       is called wrongly. Is not thread safe.
     */
    void init();

    /**
     * @brief Deinitializes the HD44780 display controller as
     *        well as the I2C interface. Sets the GPIOs to analog
     *        mode so as to reduce power consumption
     * 
     * @note Asserts on internal failure, or when the function
     *       is called wrongly. Is not thread safe.
     */
    void deinit();

    /**
     * @brief Prints the given ASCII text to the LCD screen
     * 
     * @param[in] str  String to write to the display. Truncates the
     *                 string output if greater than `COLUMNS - 1`
     * @param[in] line Line number. Can be from 0 to `ROWS - 1`
     * 
     * @note Asserts on internal failure, or when the function
     *       is called wrongly. Is not thread safe.
     */
    void println(const etl::istring& str, uint8_t line);

    /**
     * @brief Prints the given ASCII character to the LCD screen
     * 
     * @param[in] c    Character to write to the LCD screen
     * @param[in] col  Column number. Can be from 0 to `COLUMNS - 1`
     * @param[in] line Line number. Can be from 0 to `ROWS - 1`
     * 
     * @note Asserts on internal failure, or when the function
     *       is called wrongly. Is not thread safe.
     */
    void put_char(unsigned char c, uint8_t col, uint8_t line);

    /**
     * @brief Clears the screen and sets to all whitespaces.
     * 
     * @note Asserts on internal failure, or when the function
     *       is called wrongly. Is not thread safe.
     */
    void clear_screen();

} // namespace lcd

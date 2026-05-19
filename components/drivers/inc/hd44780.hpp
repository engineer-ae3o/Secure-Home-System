#pragma once


#include "etl/string.h"


namespace lcd {
    
    constexpr inline uint8_t ROWS{2};
    constexpr inline uint8_t COLUMNS{16};

    void init();

    void deinit();

    void println(const etl::istring& str, uint8_t line);

    void put_char(unsigned char c, uint8_t pos, uint8_t line);
    
} // namespace lcd

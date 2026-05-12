cmake_minimum_required(VERSION 4.1)

add_library(drivers STATIC
    src/gpio.cpp
    src/i2c.cpp
    src/keypad.cpp
    src/pcf8574.cpp
    src/reed_nc.cpp
    src/sim800l.cpp
    src/secure_system.cpp
    src/syscalls.cpp
    src/tamper.cpp
    src/uart.cpp
)

set_target_properties(drivers PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

target_compile_options(drivers PRIVATE "-ffast-math -fno-math-errno  -Wall -fdata-sections -Wnoexcept -Werror -Wextra \
                         -Wshadow -Wconversion -Wwrite-strings -Wformat=2 -ffunction-sections -Wold-style-cast -Wsign-conversion \
                         -fno-common -fstrict-aliasing -Wdouble-promotion -fstack-usage -Wuseless-cast -Wnull-dereference -Wcast-align \
                         -fstrict-overflow -fno-exceptions -fno-rtti -Wstrict-null-sentinel  -Wduplicated-branches -Wswitch-enum \
                         -Wlogical-op -Wduplicated-cond -Wstrict-aliasing=2 -fno-threadsafe-statics -Wzero-as-null-pointer-constant")

target_include_directories(drivers PUBLIC inc/)
target_include_directories(drivers PUBLIC ../cmsis-device-f1/Core/Include/m-profile/)
target_include_directories(drivers PUBLIC ../cmsis-device-f1/Core/Include/)
target_include_directories(drivers PUBLIC ../cmsis-device-f1/Include/)
target_include_directories(drivers PUBLIC ../littlefs/)
target_include_directories(drivers PUBLIC ../etl-20.39.4/include/)
target_include_directories(drivers PUBLIC ../ascon-suite/src/)

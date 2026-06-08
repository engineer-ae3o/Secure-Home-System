cmake_minimum_required(VERSION 4.1)

add_library(drivers STATIC
    src/file.cpp
    src/tasks.cpp
    src/random.cpp
    src/hd44780.cpp
    src/sim800l.cpp
    src/syscalls.cpp
    src/secure_system.cpp
)

set_target_properties(drivers PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

target_compile_options(drivers PRIVATE -ffast-math
                                       -fno-math-errno
                                       -Wall
                                       -fdata-sections
                                       -Wnoexcept
                                       -Werror
                                       -Wextra
                                       -Wshadow
                                       -Wwrite-strings
                                       -Wformat=2
                                       -ffunction-sections
                                       -fno-common
                                       -fstrict-aliasing
                                       -Wdouble-promotion
                                       -fstack-usage
                                       -Wnull-dereference
                                       -fstrict-overflow
                                       -fno-exceptions
                                       -fno-rtti
                                       -Wstrict-null-sentinel
                                       -Wduplicated-branches
                                       -Wswitch-enum
                                       -Wlogical-op
                                       -Wduplicated-cond
                                       -Wstrict-aliasing=2
                                       -fno-threadsafe-statics)

target_compile_definitions(drivers PUBLIC STM32F103xB USE_HAL_DRIVER)

target_include_directories(drivers PUBLIC inc/
                                          ../freertos/portable/GCC/ARM_CM3/
                                          ../f103-hal/Inc/
                                          ../cmsis-device-f1/Core/Include/m-profile/
                                          ../cmsis-device-f1/Core/Include/
                                          ../cmsis-device-f1/Include/
                                          ../freertos/include/
                                          ../ascon-suite/src/
                                          ../RTT/RTT/
                                          ../littlefs/
                                          ../etl/)

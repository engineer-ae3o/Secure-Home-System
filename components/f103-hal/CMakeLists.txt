cmake_minimum_required(VERSION 4.1)

file(GLOB HAL_SRCS "Src/*.c")

add_library(f103-hal STATIC ${HAL_SRCS})

target_compile_definitions(f103-hal PUBLIC STM32F103xB)

target_include_directories(f103-hal PUBLIC Inc/)
target_include_directories(f103-hal PUBLIC ../cmsis-device-f1/Core/Include/m-profile/)
target_include_directories(f103-hal PUBLIC ../cmsis-device-f1/Core/Include/)
target_include_directories(f103-hal PUBLIC ../cmsis-device-f1/Include/)

set_target_properties(f103-hal PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
)

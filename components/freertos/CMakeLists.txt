cmake_minimum_required(VERSION 4.1)

file(GLOB HAL_SRCS "tasks.c" "./*.c")

add_library(freertos STATIC
            ${HAL_SRCS}
            portable/GCC/ARM_CM3/port.c
            portable/MemMang/heap_4.c)

target_include_directories(freertos PUBLIC
                            include/
                            portable/GCC/ARM_CM3/)

set_target_properties(freertos PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
)

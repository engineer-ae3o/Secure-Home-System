cmake_minimum_required(VERSION 4.1)

add_library(printf STATIC printf.c)
target_include_directories(printf PUBLIC .)
target_compile_definitions(printf PUBLIC PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_HARD=1)

set_target_properties(printf PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
)

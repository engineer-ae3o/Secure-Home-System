cmake_minimum_required(VERSION 4.1)

add_library(littlefs STATIC
    lfs.c
    lfs_util.c
)

target_include_directories(littlefs PUBLIC .)

set_target_properties(littlefs PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
)

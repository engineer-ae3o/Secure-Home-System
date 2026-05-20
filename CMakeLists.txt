cmake_minimum_required(VERSION 4.1)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(CMAKE_C_COMPILER   C:/Users/user/arm-gnu-toolchain/bin/arm-none-eabi-gcc.exe)
set(CMAKE_CXX_COMPILER C:/Users/user/arm-gnu-toolchain/bin/arm-none-eabi-g++.exe)
set(CMAKE_ASM_COMPILER C:/Users/user/arm-gnu-toolchain/bin/arm-none-eabi-gcc.exe)
set(CMAKE_AR           C:/Users/user/arm-gnu-toolchain/bin/arm-none-eabi-ar.exe)
set(CMAKE_OBJCOPY      C:/Users/user/arm-gnu-toolchain/bin/arm-none-eabi-objcopy.exe)
set(CMAKE_OBJDUMP      C:/Users/user/arm-gnu-toolchain/bin/arm-none-eabi-objdump.exe)
set(CMAKE_SIZE         C:/Users/user/arm-gnu-toolchain/bin/arm-none-eabi-size.exe)
set(CMAKE_GDB          C:/Users/user/arm-gnu-toolchain/bin/arm-none-eabi-gdb.exe)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_PROJECT_NAME secure-home)

project(${CMAKE_PROJECT_NAME})
enable_language(C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(CMAKE_C_FLAGS "-mcpu=cortex-m3 -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections -fno-common")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wnoexcept -Wwrite-strings -Wformat=2 -Wnull-dereference -fno-exceptions -fno-rtti -Wstrict-aliasing=2")

set(CMAKE_C_FLAGS_DEBUG "-Og -g3 -ggdb -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -Wl,-s -DNDEBUG")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

set(CMAKE_EXE_LINKER_FLAGS "-T${CMAKE_SOURCE_DIR}/STM32F103XB_FLASH.ld
                            -Wl,-Map=${CMAKE_PROJECT_NAME}.map
                            -Wl,--cref
                            -Wl,--gc-sections
                            --specs=nano.specs")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

message("Build type: ${CMAKE_BUILD_TYPE}")

# Process sub-modules
add_subdirectory(components/drivers/)
add_subdirectory(components/littlefs/)
add_subdirectory(components/ascon-suite/)
add_subdirectory(components/freertos/)
add_subdirectory(components/f103-hal/)

# Create executable
add_executable(${CMAKE_PROJECT_NAME} main/main.cpp startup_stm32f103xb.s)

target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC STM32F103xB)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE 
        -ffast-math
        -fno-math-errno
        -fno-finite-loops
        -Wall
        -fdata-sections
        -Wnoexcept
        -Werror
        -Wextra
        -Wshadow
        -Wconversion
        -Wwrite-strings
        -Wformat=2
        -ffunction-sections
        -Wsign-conversion
        -fno-common
        -fstrict-aliasing
        -Wdouble-promotion
        -fstack-usage
        -Wnull-dereference
        -Wcast-align
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

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ascon_static f103-hal freertos littlefs drivers)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

# Commands
add_custom_target(build
    COMMENT "Building project"
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMAND ${CMAKE_OBJCOPY} -O binary ${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJDUMP} -D -S ${CMAKE_PROJECT_NAME} > ${CMAKE_PROJECT_NAME}.s
    COMMAND ${CMAKE_SIZE} ${CMAKE_PROJECT_NAME}
)

find_program(CPPCHECK cppcheck)
if (CPPCHECK)
    add_custom_target(cppcheck
        COMMAND ${CPPCHECK} --enable=all
                            --inconclusive
                            --output-file=analysis.txt
                            --std=gnu++23
                            --project=${CMAKE_BINARY_DIR}/compile_commands.json
                            --suppress=missingIncludeSystem
                            --inline-suppr
        COMMENT "Running static analysis"
    )
endif()

# Flash via openocd
add_custom_target(flash
    COMMAND openocd -f ${CMAKE_SOURCE_DIR}/openocd.cfg -c "program ${CMAKE_PROJECT_NAME} verify reset exit"
    COMMENT "Flashing device"
)

# Start serial monitor
add_custom_target(monitor
    COMMAND 
    MESSAGE "Loading serial monitor"
)

# Start an OpenOCD server
add_custom_target(openocd
    COMMAND openocd -f ${CMAKE_SOURCE_DIR}/openocd.cfg
    COMMENT "Starting OpenOCD server"
)

# Launch GDB
add_custom_target(gdb
    COMMAND ${CMAKE_GDB} ${CMAKE_PROJECT_NAME}
    COMMENT "Launching GDB"
)

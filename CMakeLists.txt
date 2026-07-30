cmake_minimum_required(VERSION 3.16...4.1)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/toolchain.cmake)
endif()

set(CMAKE_PROJECT_NAME secure-home)

project(${CMAKE_PROJECT_NAME})
enable_language(C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wnoexcept -Wwrite-strings -Wformat=2 -Wnull-dereference -fno-exceptions -fno-rtti -Wstrict-aliasing=2")

set(CMAKE_C_FLAGS_DEBUG "-Og -g3 -ggdb -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -Wl,-s -DNDEBUG")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

set(CMAKE_EXE_LINKER_FLAGS
    "-T${CMAKE_SOURCE_DIR}/STM32F103XB_FLASH.ld -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--cref -Wl,--gc-sections --specs=nano.specs")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

message("Build type: ${CMAKE_BUILD_TYPE}")

# Enable tests to be built. Overridable via -DBUILD_TESTS=ON/OFF or a preset.
option(BUILD_TESTS "Build the Unity test suite" ON)

# Tests pull in Unity, RTT, extra flash/RAM, and are meant for interactive
# debug sessions. A release image with tests baked in isn't a real release
# artifact, so refuse to configure that combination.
if(BUILD_TESTS AND CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR
        "BUILD_TESTS=ON is incompatible with CMAKE_BUILD_TYPE=Release. "
        "Tests are for Debug builds only; use -DBUILD_TESTS=OFF for Release, "
        "or build Debug if you need the test suite.")
endif()

# Process sub-modules
add_subdirectory(components/drivers/)
add_subdirectory(components/littlefs/)
add_subdirectory(components/ascon-suite/)
add_subdirectory(components/freertos/)
add_subdirectory(components/f103-hal/)
add_subdirectory(components/RTT/)

if (BUILD_TESTS)
    add_subdirectory(components/Unity/)
    add_subdirectory(components/test/)
endif()

# Create executable
set(EXECUTABLE_NAME "${CMAKE_PROJECT_NAME}.elf")
add_executable(${EXECUTABLE_NAME} main.cpp startup_stm32f103xb.s)

target_compile_definitions(${EXECUTABLE_NAME} PUBLIC STM32F103xB USE_HAL_DRIVER)

target_include_directories(${EXECUTABLE_NAME} PUBLIC components/test/inc/
                                          components/drivers/inc/
                                          components/Unity/src/
                                          components/freertos/portable/GCC/ARM_CM3/
                                          components/f103-hal/Inc/
                                          components/cmsis-device-f1/Core/Include/m-profile/
                                          components/cmsis-device-f1/Core/Include/
                                          components/cmsis-device-f1/Include/
                                          components/freertos/include/
                                          components/ascon-suite/src/
                                          components/littlefs/
                                          components/RTT/RTT/
                                          components/etl/
)

target_compile_options(${EXECUTABLE_NAME} PRIVATE 
        -ffast-math
        -fno-math-errno
        -fno-finite-loops
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
        -fno-threadsafe-statics
)

if (BUILD_TESTS)
    # Order of dependencies being linked in matters
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE tests drivers rtt ascon_static f103-hal freertos littlefs unity)
    target_compile_definitions(${EXECUTABLE_NAME} PUBLIC BUILD_TESTS=1)
else ()
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE drivers rtt ascon_static f103-hal freertos littlefs)
endif()

set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

# Extra commands
add_custom_target(build
    COMMENT "Building project"
    DEPENDS ${EXECUTABLE_NAME}
    COMMAND ${CMAKE_OBJCOPY} -O binary ${EXECUTABLE_NAME} ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJDUMP} -D -S ${EXECUTABLE_NAME} > ${CMAKE_PROJECT_NAME}.s
    COMMAND ${CMAKE_SIZE} ${EXECUTABLE_NAME}
)

find_program(CPPCHECK cppcheck)
find_program(OPENOCD openocd)
find_program(CLANG_TIDY clang-tidy)

if(CPPCHECK)
    message(STATUS "Cppcheck found: ${CPPCHECK}")
    
    add_custom_target(cppcheck
    COMMAND mkdir -p ${CMAKE_BINARY_DIR}/.cppcheck
    COMMAND ${CPPCHECK} --enable=all
        --inline-suppr
        --cppcheck-build-dir=${CMAKE_BINARY_DIR}/.cppcheck
        --suppressions-list=${CMAKE_SOURCE_DIR}/cppcheck_suppressions.txt
        --error-exitcode=1
        --check-level=exhaustive
        -DSTM32F103xB -D__GNUC__ -D__ARM_ARCH_7M__ -D__ARM_ARCH_PROFILE=77
        -I ${CMAKE_SOURCE_DIR}/components/drivers/inc/
        -I ${CMAKE_SOURCE_DIR}/components/test/inc/
        -I ${CMAKE_SOURCE_DIR}/components/Unity/src/
        -I ${CMAKE_SOURCE_DIR}/components/freertos/portable/GCC/ARM_CM3/
        -I ${CMAKE_SOURCE_DIR}/components/f103-hal/Inc/
        -I ${CMAKE_SOURCE_DIR}/components/cmsis-device-f1/Core/Include/m-profile/
        -I ${CMAKE_SOURCE_DIR}/components/cmsis-device-f1/Core/Include/
        -I ${CMAKE_SOURCE_DIR}/components/cmsis-device-f1/Include/
        -I ${CMAKE_SOURCE_DIR}/components/freertos/include/
        -I ${CMAKE_SOURCE_DIR}/components/ascon-suite/src/
        -I ${CMAKE_SOURCE_DIR}/components/RTT/RTT/
        -I ${CMAKE_SOURCE_DIR}/components/littlefs/
        -I ${CMAKE_SOURCE_DIR}/components/etl/
        -i ${CMAKE_SOURCE_DIR}/components/cmsis-device-f1/
        -i ${CMAKE_SOURCE_DIR}/components/littlefs/
        -i ${CMAKE_SOURCE_DIR}/components/freertos/
        -i ${CMAKE_SOURCE_DIR}/components/Unity/
        -i ${CMAKE_SOURCE_DIR}/components/etl/
        -i ${CMAKE_SOURCE_DIR}/components/f103-hal/
        -i ${CMAKE_SOURCE_DIR}/components/ascon-suite/
        ${CMAKE_SOURCE_DIR}/main.cpp
        ${CMAKE_SOURCE_DIR}/components/drivers/src/
        ${CMAKE_SOURCE_DIR}/components/test/src/
    COMMENT "Running static analysis..."
    )
else()
    message(WARNING "Cppcheck not found. Static analysis will not be available for this target.")
endif()

if (OPENOCD)
    message(STATUS "OpenOCD found: ${OPENOCD}")

    add_custom_target(flash
        COMMAND ${OPENOCD} -f ${CMAKE_SOURCE_DIR}/openocd.cfg -c "program ${EXECUTABLE_NAME} verify reset exit"
        COMMENT "Flashing device"
    )

    add_custom_target(openocd
        COMMAND ${OPENOCD} -f ${CMAKE_SOURCE_DIR}/openocd.cfg
        COMMENT "Starting OpenOCD server"
    )
else()
    message(WARNING "OpenOCD not found. Flashing and debugging won't be possible")
endif()

add_custom_target(gdb
    COMMAND ${CMAKE_GDB} ${EXECUTABLE_NAME}
    COMMENT "Launching GDB"
)
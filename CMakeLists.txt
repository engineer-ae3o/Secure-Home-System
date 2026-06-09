cmake_minimum_required(VERSION 4.1)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_AR           arm-none-eabi-ar)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP      arm-none-eabi-objdump)
set(CMAKE_SIZE         arm-none-eabi-size)
set(CMAKE_GDB          arm-none-eabi-gdb)

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

# Enable tests to be built
set(BUILD_TESTS TRUE)

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
add_executable(${CMAKE_PROJECT_NAME} main.cpp startup_stm32f103xb.s)

target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC STM32F103xB USE_HAL_DRIVER)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC components/test/inc/
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
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE tests drivers rtt ascon_static f103-hal freertos littlefs unity)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC BUILD_TESTS=1)
else ()
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE drivers rtt ascon_static f103-hal freertos littlefs)
endif()

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
find_program(OPENOCD openocd)

if(CPPCHECK)
    message(STATUS "Cppcheck found: ${CPPCHECK}")
    
    add_custom_target(cppcheck
        COMMAND mkdir -p .cppcheck
        COMMAND ${CPPCHECK} --enable=all
            --cppcheck-build-dir=${CMAKE_BINARY_DIR}/.cppcheck
            --suppressions-list=${CMAKE_SOURCE_DIR}/cppcheck_suppressions.txt
            --error-exitcode=1
            --check-level=exhaustive
            --output-file=${CMAKE_BINARY_DIR}/analysis.txt
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
            -I ${CMAKE_SOURCE_DIR}/components/littlefs/
            -I ${CMAKE_SOURCE_DIR}/components/etl/
            ${CMAKE_SOURCE_DIR}/main.cpp
            ${CMAKE_SOURCE_DIR}/components/drivers/src/
            ${CMAKE_SOURCE_DIR}/components/test/src/
        COMMENT "Running static analysis..."
        VERBATIM
    )
else()
    message(WARNING "Cppcheck not found. Static analysis will not be available.")
endif()


if (OPENOCD)
    # Flash via openocd
    add_custom_target(flash
        COMMAND ${OPENOCD} -f ${CMAKE_SOURCE_DIR}/openocd.cfg -c "program ${CMAKE_PROJECT_NAME} verify reset exit"
        COMMENT "Flashing device"
    )

    # Start an OpenOCD server
    add_custom_target(openocd
        COMMAND ${OPENOCD} -f ${CMAKE_SOURCE_DIR}/openocd.cfg
        COMMENT "Starting OpenOCD server"
    )
endif()

# Launch GDB
add_custom_target(gdb
    COMMAND ${CMAKE_GDB} ${CMAKE_PROJECT_NAME}
    COMMENT "Launching GDB"
)

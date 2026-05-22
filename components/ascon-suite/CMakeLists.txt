cmake_minimum_required(VERSION 4.1)
include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckCSourceCompiles)

# Set the project name and version number.
project(AsconSuite VERSION 0.3.0 LANGUAGES C CXX ASM)

# Options to force the use of specific backends for testing.
# Use "cmake -DBACKEND_X=ON" to enable the specific backend.
option(BACKEND_C32 "Force the use of the c32 backend" OFF)
option(BACKEND_C64 "Force the use of the c64 backend" OFF)
option(BACKEND_DIRECT_XOR "Force the use of the direct-xor backend" OFF)
option(BACKEND_GENERIC "Force the use of the generic backend" OFF)

# Set the number of shares for masked ciphers.  Note that
# DATA_SHARES must be less than or equal to KEY_SHARES.
# If either is greater than MAX_SHARES, then it will be clamped.
set(KEY_SHARES 4 CACHE STRING "Set the number of key shares for masked ciphers (2, 3, or 4)")
set_property(CACHE KEY_SHARES PROPERTY STRINGS 2 3 4)
set(DATA_SHARES 2 CACHE STRING "Set the number of data shares for masked ciphers (1, 2, 3, or 4)")
set_property(CACHE DATA_SHARES PROPERTY STRINGS 1 2 3 4)
set(MAX_SHARES 4 CACHE STRING "Set the maximum number of shares for masked ciphers (2, 3, or 4)")
set_property(CACHE MAX_SHARES PROPERTY STRINGS 2 3 4)

# Other options.
option(CHECK_ACQUIRE_RELEASE "Check that acquire/release calls are balanced" OFF)
option(COVERAGE "Enable the use of gcov for coverage testing" OFF)

# Option to compile a minimal configuration with just the static library.
# This may be needed when cross-compiling for embedded microcontrollers.
option(MINIMAL "Build a minimal configuration only" OFF)

# Add configuration parameters to the version.h include file.
configure_file(src/ascon/version.h.in version.h)

# Set up the type of build and the compiler flags to use.
if(NOT CMAKE_BUILD_TYPE)
    if(COVERAGE)
        set(CMAKE_BUILD_TYPE Debug)
    else()
        set(CMAKE_BUILD_TYPE Release)
    endif()
endif()
set(CMAKE_C_FLAGS "-Wall -Wextra -DHAVE_CONFIG_H ${CMAKE_C_FLAGS}")

set(ASCON_EXTRA_C_FLAGS "")
if(BACKEND_C32)
    set(ASCON_EXTRA_C_FLAGS "${ASCON_EXTRA_C_FLAGS} -DASCON_FORCE_C32")
endif()
if(BACKEND_C64)
    set(ASCON_EXTRA_C_FLAGS "${ASCON_EXTRA_C_FLAGS} -DASCON_FORCE_C64")
endif()
if(BACKEND_DIRECT_XOR)
    set(ASCON_EXTRA_C_FLAGS "${ASCON_EXTRA_C_FLAGS} -DASCON_FORCE_DIRECT_XOR")
endif()
if(BACKEND_GENERIC)
    set(ASCON_EXTRA_C_FLAGS "${ASCON_EXTRA_C_FLAGS} -DASCON_FORCE_GENERIC")
endif()
if(CHECK_ACQUIRE_RELEASE)
    set(ASCON_EXTRA_C_FLAGS "${ASCON_EXTRA_C_FLAGS} -DASCON_FORCE_GENERIC -DASCON_CHECK_ACQUIRE_RELEASE")
endif()
if(COVERAGE)
    set(ASCON_EXTRA_C_FLAGS "${ASCON_EXTRA_C_FLAGS} -fprofile-arcs -ftest-coverage")
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ASCON_EXTRA_C_FLAGS}")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -DHAVE_CONFIG_H ${CMAKE_CXX_FLAGS} ${ASCON_EXTRA_C_FLAGS}") 

# Set up the assembler options.
set(ASM_OPTIONS "-x assembler-with-cpp")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} ${ASM_OPTIONS}")

# Detect thread-local storage support in the compiler.
check_c_source_compiles("__thread int var; int main() { var = 1; return var; }" HAVE_THREAD_KEYWORD)
if (NOT HAVE_THREAD_KEYWORD)
    check_c_source_compiles("_Thread_local int var; int main() { var = 1; return var; }" HAVE_THREAD_LOCAL_KEYWORD)
endif()

# Set up the main include directory.
include_directories(src/ascon)

# Add the subdirectories.
add_subdirectory(src)
if(NOT MINIMAL)
    add_subdirectory(apps)
endif()

# Install the version.h include file.
install(FILES ${PROJECT_BINARY_DIR}/version.h DESTINATION include/ascon)

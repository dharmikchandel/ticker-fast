# Specify the minimum CMake version required to build the project.
cmake_minimum_required(VERSION 3.20)

# Define the project name and language.
project(ticker_fast CXX)

# Enforce the C++20 standard across all targets.
# Standard required prevents silently falling back to older C++ standards.
# Compiler extensions are disabled so we write standard, portable C++.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release build type if none was specified on the command line.
# Low-latency engines rely heavily on compiler optimizations (e.g. -O3).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Generate compile_commands.json, which LSP tools (like clangd) use to index the codebase.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define an INTERFACE library for compiler warning flags.
# Using an INTERFACE target allows our targets to opt in without imposing these
# strict flags on third-party dependencies such as GoogleTest.
add_library(ticker_fast_warnings INTERFACE)
target_compile_options(ticker_fast_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

# Define the core library as an INTERFACE target (header-only for now).
# It will transition to a compiled library in Phase 5 when shared memory logic is added.
add_library(ticker_fast_core INTERFACE)
target_include_directories(ticker_fast_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Download and integrate GoogleTest v1.14.0 via FetchContent at configure time.
# We mark the dependency as SYSTEM so third-party header warnings do not trigger
# our strict warning flags.
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    SYSTEM
)
# Prevent GoogleTest from overriding compiler runtime settings on Windows if built there.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable CTest infrastructure and recurse into the tests directory.
enable_testing()
add_subdirectory(tests)

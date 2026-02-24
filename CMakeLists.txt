cmake_minimum_required(VERSION 3.15)
project(bop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Includes
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/exchanges
    ${PROJECT_SOURCE_DIR}/include
)

# Find Dependencies
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# Static Analysis
find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=*,-fuchsia-*,-google-*,-zircon-*,-abseil-*,-modernize-use-trailing-return-type")
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
else()
    message(WARNING "clang-tidy not found.")
endif()

# Core Library
add_library(bop_core
    core/impl.cpp
    core/algo.cpp
)
target_link_libraries(bop_core PUBLIC OpenSSL::SSL OpenSSL::Crypto CURL::libcurl Boost::system)

# Main Executable
add_executable(main main.cpp)
target_link_libraries(main PRIVATE bop_core)

# Tests
add_executable(test_auth test_auth.cpp)
target_link_libraries(test_auth PRIVATE bop_core)

add_executable(test_keccak test_keccak.cpp)
target_link_libraries(test_keccak PRIVATE bop_core)

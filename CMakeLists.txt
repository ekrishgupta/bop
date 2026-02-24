cmake_minimum_required(VERSION 3.15)
project(bop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Includes
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/core/external
    ${PROJECT_SOURCE_DIR}/exchanges
    ${PROJECT_SOURCE_DIR}/include
)

# Find Dependencies
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED)
find_package(SQLite3 REQUIRED)

include(FetchContent)
FetchContent_Declare(
  simdjson_content
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG v3.10.1
)
FetchContent_MakeAvailable(simdjson_content)

# Use an alias to get the expected target name
add_library(simdjson::simdjson ALIAS simdjson)

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
    core/database.cpp
)
target_link_libraries(bop_core PUBLIC OpenSSL::SSL OpenSSL::Crypto CURL::libcurl Boost::boost simdjson::simdjson SQLite::SQLite3)

# Main Executable
add_executable(main main.cpp)
target_link_libraries(main PRIVATE bop_core)

# Tests
add_executable(test_dsl tests/test_dsl.cpp)
target_link_libraries(test_dsl PRIVATE bop_core simdjson)

add_executable(backtest_example examples/backtest_example.cpp)
target_link_libraries(backtest_example PRIVATE bop_core)

add_executable(universal_market_example examples/universal_market_example.cpp)
target_link_libraries(universal_market_example PRIVATE bop_core)

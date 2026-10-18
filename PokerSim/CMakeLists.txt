cmake_minimum_required(VERSION 3.16)
project(PokerSim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_library(pokersim_core STATIC
  src/cards.cpp src/equity.cpp src/config.cpp src/engine.cpp src/brains.cpp src/runner.cpp
  src/ohlang.cpp src/shanky.cpp)
target_include_directories(pokersim_core PUBLIC include PRIVATE third_party)
target_link_libraries(pokersim_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(pokersim_core PUBLIC /utf-8 /W4 /permissive- /EHsc)
  target_compile_definitions(pokersim_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(pokersim_core PUBLIC -Wall -Wextra)
endif()

add_executable(pokersim apps/cli/main.cpp)
target_link_libraries(pokersim PRIVATE pokersim_core)

enable_testing()
add_executable(pokersim_tests tests/test_core.cpp)
target_link_libraries(pokersim_tests PRIVATE pokersim_core)
add_test(NAME core COMMAND pokersim_tests)

add_executable(ohdump apps/ohdump/main.cpp)
target_link_libraries(ohdump PRIVATE pokersim_core)

add_executable(ohlang_tests tests/test_ohlang.cpp)
target_link_libraries(ohlang_tests PRIVATE pokersim_core)
add_test(NAME ohlang COMMAND ohlang_tests)

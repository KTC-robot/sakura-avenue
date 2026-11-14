cmake_minimum_required(VERSION 3.19)
project(sakura_host LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_vectors.py
 ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/golden_vectors.json ${CMAKE_CURRENT_BINARY_DIR}/vectors.hpp
 COMMAND_ERROR_IS_FATAL ANY)
add_executable(control_tests test.cpp ../components/control/speed_controller.cpp)
target_include_directories(control_tests PRIVATE ../components/control/include ../components/protocol/include ../components/device/include ../components/network/include ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(control_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
enable_testing()
add_test(NAME control COMMAND control_tests)

cmake_minimum_required(VERSION 3.19)
project(sakura_host LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_vectors.py
 ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/golden_vectors.json ${CMAKE_CURRENT_BINARY_DIR}/vectors.hpp
 COMMAND_ERROR_IS_FATAL ANY)
set(P ${CMAKE_CURRENT_SOURCE_DIR}/../../platform)
add_executable(control_tests test.cpp
 ${P}/control/speed_controller.cpp ${P}/control/drive_command.cpp ${P}/control/omni.cpp
 ${P}/control/safety.cpp ${P}/control/chassis.cpp ${P}/control/vehicle_config.cpp
 ${P}/device/c620.cpp ${P}/protocol/protocol.cpp
 ${P}/network/receiver.cpp ${P}/network/control_loop.cpp)
target_include_directories(control_tests PRIVATE ../../platform ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(control_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
enable_testing()
add_test(NAME control COMMAND control_tests)

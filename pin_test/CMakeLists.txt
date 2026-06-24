cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK (PICO_SDK_PATH env var, or the copy beside this repo).
if(NOT DEFINED ENV{PICO_SDK_PATH} AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../pico-sdk")
    set(ENV{PICO_SDK_PATH} "${CMAKE_CURRENT_LIST_DIR}/../../pico-sdk")
endif()

# Default to Pico 2 (RP2350). Override with -DPICO_BOARD=pico for the original.
# Must be set (as a cache var) before pico_sdk_init() does its board defaulting.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2 CACHE STRING "Target board")
endif()

include(pico_sdk_import.cmake)

project(pin_test C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pin_test pin_test.c)

target_link_libraries(pin_test
    pico_stdlib
    hardware_adc
    hardware_gpio
)

# Interactive test over USB serial; no UART output needed.
pico_enable_stdio_usb(pin_test 1)
pico_enable_stdio_uart(pin_test 0)

pico_add_extra_outputs(pin_test)

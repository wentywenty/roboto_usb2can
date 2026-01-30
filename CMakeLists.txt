# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(roboparty_canfd)

# Version definition
set(APP_VERSION_MAJOR 1)
set(APP_VERSION_MINOR 0)
set(APP_VERSION_PATCH 0)

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/src/version.h)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src)

target_sources(app PRIVATE src/main.c
    src/status_led.c
)

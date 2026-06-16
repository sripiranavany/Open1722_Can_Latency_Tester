cmake_minimum_required(VERSION 3.20.0)

get_filename_component(OPEN1722_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Open1722"
    ABSOLUTE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(avtp_talker)

file(GLOB_RECURSE OPEN1722_SOURCES "${OPEN1722_DIR}/src/*.c")

target_sources(app PRIVATE
    src/main.c
    src/can_handler.c
    src/net_handler.c
    src/avtp_handler.c
    ${OPEN1722_SOURCES}
)

target_include_directories(app PUBLIC
    ${OPEN1722_DIR}/include
)

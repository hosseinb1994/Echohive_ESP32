cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Ecohive_ESP32)

# Force linker to include all mqtt archive objects — fixes outbox symbol stripping
target_link_libraries(${CMAKE_PROJECT_NAME}.elf PRIVATE
    "-Wl,--whole-archive"
    "${CMAKE_BINARY_DIR}/esp-idf/mqtt/libmqtt.a"
    "-Wl,--no-whole-archive"
)
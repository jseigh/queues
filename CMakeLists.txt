# queues main
cmake_minimum_required(VERSION 3.20)
project(queues)

add_subdirectory(test test)

install(DIRECTORY ${CMAKE_BINARY_DIR}/include
    DESTINATION .
    FILES_MATCHING PATTERN "*.h"
    )
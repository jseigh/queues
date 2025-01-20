# queues test
cmake_minimum_required(VERSION 3.20)

project(queues_test)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -std=gnu17 -ggdb")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -std=c++23 -ggdb")
else ()
    message(FATAL_ERROR "unsupported system type")
endif ()

message(STATUS "CMAKE_SOURCE_DIR : ${CMAKE_SOURCE_DIR}")
message(STATUS "PROJECT_SOURCE_DIR : ${PROJECT_SOURCE_DIR}")
message(STATUS "CMAKE_BINARY_DIR : ${CMAKE_BINARY_DIR}")

add_executable(dump_queue dump_queue.cpp)
target_include_directories(dump_queue PUBLIC
    .
    ${PROJECT_SOURCE_DIR}/../include
    )

add_executable(qtest qtest.cpp)
target_include_directories(qtest PUBLIC
    .
    ${PROJECT_SOURCE_DIR}/../include
    )
    
cmake_minimum_required(VERSION 2.8)

project(kiwi C)

add_extra_yb_flags_in_odyssey()

set(CLANGCOMPILERSTANDART gnu11)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE "Debug")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "RELEASE")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=${CLANGCOMPILERSTANDART} -Wall -Wextra -fPIC -g -O2 -pedantic ")
elseif("${CMAKE_BUILD_TYPE}" STREQUAL "DEBUG")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=${CLANGCOMPILERSTANDART} -Wall -Wextra -fPIC -g -O0 -pedantic ")
endif()

include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_BINARY_DIR}/")
include_directories("${MACHINARIUM_INCLUDE_DIRS}")

add_subdirectory(kiwi)

message(STATUS "")
message(STATUS "Kiwi: PostgreSQL protocol interaction library.")
message(STATUS "")
message(STATUS "CMAKE_BUILD_TYPE:         ${CMAKE_BUILD_TYPE}")
message(STATUS "MACHINARIUM_INCLUDE_DIRS: ${MACHINARIUM_INCLUDE_DIRS}")
message(STATUS "MACHINARIUM_LIBRARIES:    ${MACHINARIUM_LIBRARIES}")
message(STATUS "")

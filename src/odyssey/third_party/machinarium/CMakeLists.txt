cmake_minimum_required(VERSION 3.10)

project(machinarium C)

add_extra_yb_flags_in_odyssey()

set(CLANGCOMPILERSTANDART gnu11)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(Threads REQUIRED)
set(CMAKE_THREAD_PREFER_PTHREAD true)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE "DEBUG")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "RELEASE")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=${CLANGCOMPILERSTANDART} -fPIC -g -O2 -D_GNU_SOURCE -pthread -pedantic ")
elseif("${CMAKE_BUILD_TYPE}" STREQUAL "DEBUG")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=${CLANGCOMPILERSTANDART} -fPIC -g -O0 -D_GNU_SOURCE -pthread -pedantic ")
endif()

include_directories("${PROJECT_SOURCE_DIR}/sources")
include_directories("${PROJECT_BINARY_DIR}/sources")

option(BUILD_SHARED "Enable SHARED" OFF)
option(BUILD_VALGRIND "Enable VALGRIND" OFF)

set(mm_libraries "")

# valgrind
if (BUILD_VALGRIND)
    find_path(VALGRIND_INCLUDE_PATH "valgrind/valgrind.h"
              "/usr/include"
              "/usr/local/include")
    if (${VALGRIND_INCLUDE_PATH} STREQUAL "VALGRIND_INCLUDE_PATH-NOTFOUND")
    else()
        set(HAVE_VALGRIND 1)
    endif()
endif()

set(compression_libraries "")
if (BUILD_COMPRESSION)
    add_definitions(-DMM_BUILD_COMPRESSION)

    # use zstd
    find_package(Zstd)
    if(ZSTD_FOUND)
        include_directories(${ZSTD_INCLUDE_DIR})
        set(compression_libraries ${compression_libraries} ${ZSTD_LIBRARY})
        add_definitions(-DMM_HAVE_ZSTD)
    endif()

    # use zlib
    find_package(ZLIB)
    if(ZLIB_FOUND)
        include_directories(${ZLIB_INCLUDE_DIRS})
        set(compression_libraries ${compression_libraries} ${ZLIB_LIBRARIES})
        add_definitions(-DMM_HAVE_ZLIB)
    endif()
endif()

# use BoringSSL or OpenSSL
option(USE_BORINGSSL "Use BoringSSL" OFF)
if (USE_BORINGSSL)
    find_package(BoringSSL REQUIRED)
    if (BORINGSSL_FOUND)
        list(APPEND mm_libraries "${BORINGSSL_LIBRARIES}")
        include_directories(${BORINGSSL_INCLUDE_DIR})
    endif()
else ()
    if ("${OPENSSL_LIBRARIES}" STREQUAL "")
        find_package(OpenSSL REQUIRED)
    endif()

    list(APPEND mm_libraries "${OPENSSL_LIBRARIES}")
    include_directories(${OPENSSL_INCLUDE_DIR})
endif()

add_subdirectory(sources)

message(STATUS "")
message(STATUS "Machinarium: Cooperative multitasking framework.")
message(STATUS "")
message(STATUS "CMAKE_BUILD_TYPE:      ${CMAKE_BUILD_TYPE}")
message(STATUS "BUILD_SHARED:          ${BUILD_SHARED}")
message(STATUS "BUILD_VALGRIND:        ${BUILD_VALGRIND}")
message(STATUS "USE_BORINGSSL:         ${USE_BORINGSSL}")
message(STATUS "BORINGSSL_ROOT_DIR:    ${BORINGSSL_ROOT_DIR}")
message(STATUS "BORINGSSL_INCLUDE_DIR: ${BORINGSSL_INCLUDE_DIR}")
message(STATUS "OPENSSL_VERSION:       ${OPENSSL_VERSION}")
message(STATUS "OPENSSL_ROOT_DIR:      ${OPENSSL_ROOT_DIR}")
message(STATUS "OPENSSL_INCLUDE_DIR:   ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OPENSSL_LIBRARIES:     ${OPENSSL_LIBRARIES}")
message(STATUS "BUILD_COMPRESSION:     ${BUILD_COMPRESSION}")
if (BUILD_COMPRESSION)
    message(STATUS "ZSTD_INCLUDE_DIR:      ${ZSTD_INCLUDE_DIR}")
    message(STATUS "ZSTD_LIBRARY:          ${ZSTD_LIBRARY}")
    message(STATUS "ZLIB_INCLUDE_DIRS:     ${ZLIB_INCLUDE_DIRS}")
    message(STATUS "ZLIB_LIBRARIES:        ${ZLIB_LIBRARIES}")
endif()
message(STATUS "")

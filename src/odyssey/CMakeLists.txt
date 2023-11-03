cmake_minimum_required(VERSION 3.10)

project(odyssey C)

add_extra_yb_flags_in_odyssey()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CLANGCOMPILERSTANDART gnu11)

set(OD_VERSION_GIT $ENV{VERSION})
if ("${OD_VERSION_GIT}" STREQUAL "")
    execute_process(COMMAND git describe --always
                    OUTPUT_VARIABLE OD_VERSION_GIT
                    RESULT_VARIABLE OD_VERSION_GIT_RESULT
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)

    if(NOT "${OD_VERSION_GIT_RESULT}" STREQUAL "0")
        add_definitions(-DOD_VERSION_GIT="unknown")
    else()
        add_definitions(-DOD_VERSION_GIT="${OD_VERSION_GIT}")
    endif()
else()
    add_definitions(-DOD_VERSION_GIT="${OD_VERSION_GIT}")
endif()

set(OD_VERSION_NUMBER $ENV{BUILD_NUMBER})
if ("${OD_VERSION_NUMBER}" STREQUAL "")

    execute_process(COMMAND git rev-list --count HEAD
                    OUTPUT_VARIABLE OD_VERSION_NUMBER
                    RESULT_VARIABLE OD_VERSION_NUMBER_RESULT
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)

    if(NOT "${OD_VERSION_NUMBER_RESULT}" STREQUAL "0")
        add_definitions(-DOD_VERSION_NUMBER="unknown")
    else()
        add_definitions(-DOD_VERSION_NUMBER="${OD_VERSION_NUMBER}")
    endif()
else()
    add_definitions(-DOD_VERSION_NUMBER="${OD_VERSION_NUMBER}")
endif()

execute_process(COMMAND ${CMAKE_C_COMPILER} --version
        OUTPUT_VARIABLE OD_COMPILER_VERSION
        RESULT_VARIABLE OD_COMPILER_VERSION_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)


set(OD_DEVEL_LVL -1)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
        set(CMAKE_BUILD_TYPE "DEBUG")
        set(OD_DEVEL_LVL 1)
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE)
if ("${CMAKE_BUILD_TYPE}" STREQUAL "RELEASE")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=${CLANGCOMPILERSTANDART} -pedantic -Wall -Wextra -Wstrict-aliasing -g -O2 -pthread -D_GNU_SOURCE")
        set(OD_DEVEL_LVL -1)
elseif("${CMAKE_BUILD_TYPE}" STREQUAL "DEBUG")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=${CLANGCOMPILERSTANDART} -pedantic -Wall -Wextra -Wstrict-aliasing -g -O0 -pthread -D_GNU_SOURCE -DLDAP_DBG")
        set(OD_DEVEL_LVL 1)
elseif("${CMAKE_BUILD_TYPE}" STREQUAL "ASAN")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=${CLANGCOMPILERSTANDART} -g -fsanitize=address,undefined -fno-sanitize-recover=all -pedantic -Wall -Wextra -Wstrict-aliasing -g -O0 -pthread -D_GNU_SOURCE")
elseif("${CMAKE_BUILD_TYPE}" STREQUAL "TSAN")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=${CLANGCOMPILERSTANDART} -g -fsanitize=thread -fno-sanitize-recover=all -pedantic -Wall -Wextra -Wstrict-aliasing -g -O0 -pthread -D_GNU_SOURCE")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CC_FLAGS}")

string(TOLOWER ${CMAKE_BUILD_TYPE} OD_VERSION_BUILD)
add_definitions(-DOD_VERSION_BUILD="${OD_VERSION_BUILD}")

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)

add_custom_target(build_libs)
set(od_libraries "rt")

# debian build
option(BUILD_DEBIAN "Enable Debian Build" OFF)
if (BUILD_DEBIAN)
    include(BuildDebian)
endif()

# postgresql
if("${POSTGRESQL_LIBRARY}" STREQUAL "")
    find_package(PostgreSQL REQUIRED)
endif()

set(od_libraries ${od_libraries} ${POSTGRESQL_LIBRARY} ${POSTGRESQL_LIBPGPORT} ${PQ_LIBRARY})
include_directories(${POSTGRESQL_INCLUDE_DIR})

# use BoringSSL or OpenSSL
option(USE_BORINGSSL "Use BoringSSL" OFF)
if (USE_BORINGSSL)
    find_package(BoringSSL REQUIRED)
    if (BORINGSSL_FOUND)
        set(od_ssl ${BORINGSSL_LIBRARIES})
        include_directories(${BORINGSSL_INCLUDE_DIR})
    endif ()
elseif ("${OPENSSL_LIBRARIES}" STREQUAL "")
    find_package(OpenSSL REQUIRED)
    set(od_ssl ${OPENSSL_LIBRARIES})
    include_directories(${OPENSSL_INCLUDE_DIR})
    add_definitions(-DUSE_SSL)
endif ()

# use PAM
if(NOT OD_SKIP_PAM)
    if("${PAM_LIBRARY}" STREQUAL "")
        find_package(PAM)
    endif()
    if (PAM_FOUND)
        set(od_pam ${PAM_LIBRARY})
        include_directories(${PAM_INCLUDE_DIR})
    endif()
    set(od_libraries ${od_libraries} ${od_pam})
endif()

if("${LDAP_LIBRARY}" STREQUAL "")
    find_package(LDAP)
endif()
if (LDAP_FOUND)
    set(od_ldap ${LDAP_LIBRARY})
    include_directories(${LDAP_INCLUDE_DIR})
endif()
set(od_libraries ${od_libraries} ${od_ldap})

# use Prom
find_package(Prom)

if (PROMHTTP_FOUND)
    set(od_promhttp ${PROMHTTP_LIBRARY})
    include_directories(${PROMHTTP_INCLUDE_DIR})
endif()

set(od_libraries ${od_libraries} ${od_promhttp})


if (PROM_FOUND)
    set(od_prom ${PROM_LIBRARY})
    include_directories(${PROM_INCLUDE_DIR})
endif()

set(od_libraries ${od_libraries} ${od_prom})

#scram

if (USE_SCRAM)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_SCRAM")
endif()

#compression
option(OD_BUILD_COMPRESSION "Build with compression support" OFF)
if(OD_BUILD_COMPRESSION)
    set(compression_libraries "")
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

# machinarium
if("${MACHINARIUM_LIBRARIES}" STREQUAL "")
    include(BuildMachinarium)
    build_machinarium()
endif()
set(od_libraries ${od_libraries} ${MACHINARIUM_LIBRARIES} ${od_ssl} "dl")
include_directories(${MACHINARIUM_INCLUDE_DIRS})

# kiwi
if("${KIWI_LIBRARIES}" STREQUAL "")
    include(BuildKiwi)
    build_kiwi()
endif()
set(od_libraries ${od_libraries} ${KIWI_LIBRARIES})
include_directories(${KIWI_INCLUDE_DIRS})

message(STATUS "")
message(STATUS "Odyssey (version: ${OD_VERSION_NUMBER}-${OD_VERSION_GIT}-${OD_VERSION_BUILD})")
message(STATUS "")
message(STATUS "using compiler:         ${CMAKE_C_COMPILER} version
${OD_COMPILER_VERSION}")
message(STATUS "c lang standart:        ${CLANGCOMPILERSTANDART}")
message(STATUS "                                                                          ")
message(STATUS "CMAKE_BUILD_TYPE:       ${CMAKE_BUILD_TYPE}")
message(STATUS "BUILD_DEBIAN:           ${BUILD_DEBIAN}")
message(STATUS "POSTGRESQL_INCLUDE_DIR: ${POSTGRESQL_INCLUDE_DIR}")
message(STATUS "POSTGRESQL_LIBRARY:     ${POSTGRESQL_LIBRARY}")
message(STATUS "POSTGRESQL_LIBPGPORT:   ${POSTGRESQL_LIBPGPORT}")
message(STATUS "PG_VERSION_NUM:         ${PG_VERSION_NUM}")
message(STATUS "PQ_LIBRARY:             ${PQ_LIBRARY}")
message(STATUS "USE_BORINGSSL:          ${USE_BORINGSSL}")
message(STATUS "BORINGSSL_ROOT_DIR:     ${BORINGSSL_ROOT_DIR}")
message(STATUS "BORINGSSL_INCLUDE_DIR:  ${BORINGSSL_INCLUDE_DIR}")
message(STATUS "OPENSSL_VERSION:        ${OPENSSL_VERSION}")
message(STATUS "OPENSSL_ROOT_DIR:       ${OPENSSL_ROOT_DIR}")
message(STATUS "OPENSSL_INCLUDE_DIR:    ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OPENSSL_LIBRARIES:      ${OPENSSL_LIBRARIES}")

if (PAM_FOUND)
    message(STATUS "PAM_LIBRARY:            ${PAM_LIBRARY}")
    message(STATUS "PAM_INCLUDE_DIR:        ${PAM_INCLUDE_DIR}")
endif()


message(STATUS "USE_SCRAM:              ${USE_SCRAM}")


message(STATUS "BUILD_COMPRESSION:      ${BUILD_COMPRESSION}")
if (BUILD_COMPRESSION)
    message(STATUS "ZSTD_INCLUDE_DIR:       ${ZSTD_INCLUDE_DIR}")
    message(STATUS "ZSTD_LIBRARY:           ${ZSTD_LIBRARY}")
    message(STATUS "ZLIB_INCLUDE_DIRS:      ${ZLIB_INCLUDE_DIRS}")
    message(STATUS "ZLIB_LIBRARIES:         ${ZLIB_LIBRARIES}")
endif()

    message(STATUS "LDAP_SUPPORT:           ${LDAP_FOUND}")
if (LDAP_FOUND)
    message(STATUS "LDAP_LIBRARY:           ${LDAP_LIBRARY}")
    message(STATUS "LDAP_INCLUDE_DIR:       ${LDAP_INCLUDE_DIR}")
endif()

add_subdirectory(sources)
add_subdirectory(test)
add_subdirectory(stress)

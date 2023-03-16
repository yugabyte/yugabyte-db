
# - Try to find the PostgreSQL libraries
#
#  POSTGRESQL_INCLUDE_DIR - PostgreSQL include directory
#  POSTGRESQL_LIBRARY     - PostgreSQL library
#  PQ_LIBRARY             - PostgreSQL PQ library
#  PQ_LIBRARY             - PostgreSQL version

if("${POSTGRESQL_INCLUDE_DIR}" STREQUAL "" OR "${POSTGRESQL_INCLUDE_DIR}" STREQUAL "POSTGRESQL_INCLUDE_DIR-NOTFOUND") 
    find_path(
        POSTGRESQL_INCLUDE_DIR
        NAMES common/base64.h common/saslprep.h common/scram-common.h common/sha2.h
        PATH_SUFFIXES PG_INCLUDE_SERVER
    )
    execute_process (
        COMMAND pg_config --includedir-server
        OUTPUT_VARIABLE PG_INCLUDE_SERVER
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    execute_process (
        COMMAND pg_config --version
        OUTPUT_VARIABLE PG_VERSION_NUM
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(POSTGRESQL_INCLUDE_DIR ${PG_INCLUDE_SERVER})

endif()


execute_process (
        COMMAND pg_config --libdir
        OUTPUT_VARIABLE PG_LIBDIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process (
        COMMAND pg_config --pkglibdir
        OUTPUT_VARIABLE PG_PKGLIBDIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

find_library(
    POSTGRESQL_LIBRARY
    NAMES pgcommon
    HINTS ${PG_PKGLIBDIR} ${PG_LIBDIR}
)

find_library(
    POSTGRESQL_LIBPGPORT
    NAMES pgport
    HINTS ${PG_PKGLIBDIR} ${PG_LIBDIR}
)

find_library(
    PQ_LIBRARY
    NAMES libpq.a libpq.so
    HINTS ${PG_LIBDIR}
)

find_package_handle_standard_args(
    PostgreSQL
    REQUIRED_VARS POSTGRESQL_LIBRARY PQ_LIBRARY POSTGRESQL_INCLUDE_DIR
)

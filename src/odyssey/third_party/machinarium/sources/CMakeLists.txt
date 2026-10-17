set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

set(machine_library machinarium)
set(machine_src
    thread.c
    pg_rand48.c
    lrand48.c
    loop.c
    clock.c
    socket.c
    epoll.c
    context_stack.c
    context.c
    coroutine.c
    coroutine_cache.c
    scheduler.c
    call.c
    signal_mgr.c
    event_mgr.c
    machine.c
    mm.c
    machine_mgr.c
    msg_cache.c
    msg.c
    channel_fast.c
    channel.c
    channel_api.c
    task_mgr.c
    tls.c
    io.c
    iov.c
    close.c
    connect.c
    bind.c
    eventfd.c
    cond.c
    read.c
    write.c
    accept.c
    shutdown.c
    dns.c
    wait_list.c
    zpq_stream.c
    compression.c
    # mm_tls_get_context() builds the server SSL_CTX through be_tls_init();
    # The definition lives in this archive so that every consumer resolves
    # it without linking the odyssey executable (like odyssey_test and
    # odyssey_stress). The Postgres symbols this file needs come from
    # yb_pg_tls_link_support.h, which in turn reads mm_io_t/mm_tls_t internals,
    # so the two archives would be mutually undefined.
    ${YB_SRC_ROOT}/src/postgres/src/backend/libpq/be-secure-openssl.c)

add_library(machine_library_static STATIC ${machine_src})
set_target_properties(machine_library_static PROPERTIES OUTPUT_NAME ${machine_library})
# Clang only. Under -Wextra/-pedantic two diagnostics fire while compiling this
# file: extra ';' from yb/yql/pggate's ybc_pg_typedefs.h, which postgres.h pulls
# in, and a GNU statement expression in this file itself. -Werror would make
# both fatal, so downgrade them for this one file.
# GCC needs nothing: it rejects these Clang option names, and yb_setup_odyssey()
# already passes -Wno-pedantic for GCC (cmake_modules/YugabyteFunctions.cmake),
# which turns the same diagnostics off.
if(IS_CLANG)
    set(_be_secure_openssl_flags
        "-Wno-error=extra-semi -Wno-error=gnu-statement-expression-from-macro-expansion")
endif()
# Scope the Postgres include path to this one file rather than to the whole
# target: It's required to include postgres.h, libpq/libpq.h and generated
# pg_config*.h.
# This file also needs ${YB_SRC_ROOT}/src (for yb/yql/pggate's ybc_util.h).
# It is not listed here: which is supplied by YB's global include directories.
set_source_files_properties(
    ${YB_SRC_ROOT}/src/postgres/src/backend/libpq/be-secure-openssl.c
    PROPERTIES
        COMPILE_DEFINITIONS "YB_CONN_MGR=1"
        COMPILE_FLAGS "${_be_secure_openssl_flags}"
        INCLUDE_DIRECTORIES "${YB_BUILD_ROOT}/postgres_build/src/include")
# be-secure-openssl.c reads generated headers from postgres_build/src/include
# (pg_config*.h, catalog/pg_*_d.h, utils/errcodes.h, storage/lwlocknames.h) that
# only exist once the postgres target has run.
add_dependencies(machine_library_static postgres)

if (BUILD_COMPRESSION)
    target_link_libraries(machine_library_static ${compression_libraries})
endif()

if (BUILD_SHARED)
    add_library(machine_library_shared SHARED ${machine_src})
    set_target_properties(machine_library_shared PROPERTIES OUTPUT_NAME ${machine_library})
    target_link_libraries(machine_library_shared ${machine_libraries} m rt)
endif()

target_link_libraries(machine_library_static ${mm_libraries})

configure_file("build.h.cmake" "build.h")

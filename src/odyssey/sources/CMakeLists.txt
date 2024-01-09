
configure_file("build.h.cmake" "build.h")

if("${od_binary}" STREQUAL "")
    set(od_binary {odyssey})
endif()

set(od_src
    daemon.c
    pid.c
    logger.c
    pool.c
    rules.c
    config.c
    config_reader.c
    dns.c
    router.c
    system.c
    cron.c
    worker.c
    tls.c
    attribute.c
    auth_query.c
    auth.c
    cancel.c
    console.c
    deploy.c
    reset.c
    frontend.c
    backend.c
    instance.c
    main.c
    odyssey_main.cc
    misc.c
    tdigest.c
    module.c
    counter.c
    err_logger.c
    setproctitle.c
    debugprintf.c
    restart_sync.c
    grac_shutdown_worker.c
    sighandler.c
    watchdog.c
    ejection.c
    thread_global.c
    compression.c
    option.c
    tls_config.c
    query.c
    storage.c
    murmurhash.c
    hashmap.c
    hba.c
    hba_reader.c
    hba_rule.c
    yb_auth_passthrough.c)

if (PAM_FOUND)
        list(APPEND od_src pam.c)
endif()

if (USE_SCRAM)
        list(APPEND od_src scram.c)
endif()

if (LDAP_FOUND)
        list(APPEND od_src ldap.c)
endif()

if (PROM_FOUND)
        list(APPEND od_src prom_metrics.c)
endif()

include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_SOURCE_DIR}/sources")
include_directories("${PROJECT_BINARY_DIR}/")
include_directories("${PROJECT_BINARY_DIR}/sources")

add_executable(${od_binary} ${od_src})
add_dependencies(${od_binary} build_libs)

if(NOT "${od_extra_dependencies}" STREQUAL "")
    add_dependencies(${od_binary} ${od_extra_dependencies})
endif()

if(THREADS_HAVE_PTHREAD_ARG)
    set_property(TARGET ${od_binary} PROPERTY COMPILE_OPTIONS "-pthread")
    set_property(TARGET ${od_binary} PROPERTY INTERFACE_COMPILE_OPTIONS "-pthread")
endif()

target_link_libraries(${od_binary} ${od_libraries} ${OD_EXTRA_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} m)

if (BUILD_COMPRESSION)
    target_link_libraries(${od_binary} ${compression_libraries})
endif()

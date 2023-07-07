
set(od_test_binary odyssey_test)
set(od_test_src
    odyssey_test.c
    machinarium/test_init.c
    machinarium/test_create0.c
    machinarium/test_create1.c
    machinarium/test_config.c
    machinarium/test_context_switch.c
    machinarium/test_sleep.c
    machinarium/test_sleep_yield.c
    machinarium/test_sleep_cancel0.c
    machinarium/test_join.c
    machinarium/test_condition0.c
    machinarium/test_eventfd.c
    machinarium/test_stat.c
    machinarium/test_signal0.c
    machinarium/test_signal1.c
    machinarium/test_signal2.c
    machinarium/test_channel_create.c
    machinarium/test_channel_rw0.c
    machinarium/test_channel_rw1.c
    machinarium/test_channel_rw2.c
    machinarium/test_channel_rw3.c
    machinarium/test_channel_rw4.c
    machinarium/test_channel_timeout.c
    machinarium/test_channel_cancel.c
    machinarium/test_channel_shared_create.c
    machinarium/test_channel_shared_rw0.c
    machinarium/test_channel_shared_rw1.c
    machinarium/test_channel_shared_rw2.c
    machinarium/test_sleeplock.c
    machinarium/test_producer_consumer0.c
    machinarium/test_producer_consumer1.c
    machinarium/test_producer_consumer2.c
    machinarium/test_io_new.c
    machinarium/test_connect.c
    machinarium/test_connect_timeout.c
    machinarium/test_connect_cancel0.c
    machinarium/test_connect_cancel1.c
    machinarium/test_accept_timeout.c
    machinarium/test_accept_cancel.c
    machinarium/test_getaddrinfo0.c
    machinarium/test_getaddrinfo1.c
    machinarium/test_getaddrinfo2.c
    machinarium/test_client_server0.c
    machinarium/test_client_server1.c
    machinarium/test_client_server2.c
    machinarium/test_client_server_unix_socket.c
    machinarium/test_read_10mb0.c
    machinarium/test_read_10mb1.c
    machinarium/test_read_10mb2.c
    machinarium/test_read_timeout.c
    machinarium/test_read_cancel.c
    machinarium/test_read_var.c
    machinarium/test_tls0.c
    machinarium/test_tls_unix_socket.c
    machinarium/test_tls_read_10mb0.c
    machinarium/test_tls_read_10mb1.c
    machinarium/test_tls_read_10mb2.c
    machinarium/test_tls_read_multithread.c
    machinarium/test_tls_read_var.c
        ../sources/attribute.c
        ../sources/tdigest.c
        ../sources/util.h
        ../sources/build.h
        ../sources/debugprintf.h
        ../sources/hba.c
        ../sources/hba_rule.c
        ../sources/hba_reader.c
        odyssey/test_attribute.c
        odyssey/test_tdigest.c
        odyssey/test_util.c
        odyssey/test_locks.c
        odyssey/test_hba_parse.c
   )

file(COPY machinarium/ca.crt DESTINATION machinarium)
file(COPY machinarium/client.crt DESTINATION machinarium)
file(COPY machinarium/client.key DESTINATION machinarium)
file(COPY machinarium/server.crt DESTINATION machinarium)
file(COPY machinarium/server.key DESTINATION machinarium)

include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_SOURCE_DIR}/sources/")
include_directories("${PROJECT_BINARY_DIR}/")
include_directories("${PROJECT_SOURCE_DIR}/test")
include_directories("${PROJECT_BINARY_DIR}/test")

add_executable(${od_test_binary} ${od_test_src})
add_dependencies(${od_test_binary} build_libs ${od_binary})

if(THREADS_HAVE_PTHREAD_ARG)
    set_property(TARGET ${od_test_binary} PROPERTY COMPILE_OPTIONS "-pthread")
    set_property(TARGET ${od_test_binary} PROPERTY INTERFACE_COMPILE_OPTIONS "-pthread")
endif()

target_link_libraries(${od_test_binary} ${od_libraries} ${CMAKE_THREAD_LIBS_INIT} m)

if (BUILD_COMPRESSION)
    target_link_libraries(${od_test_binary} ${compression_libraries})
endif()

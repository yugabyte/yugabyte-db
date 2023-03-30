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
    zpq_stream.c
    compression.c)

add_library(machine_library_static STATIC ${machine_src})
set_target_properties(machine_library_static PROPERTIES OUTPUT_NAME ${machine_library})

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

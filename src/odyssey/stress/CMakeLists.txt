
set(od_stress_binary odyssey_stress)
set(od_stress_src odyssey_stress.c)

include_directories("${PROJECT_SOURCE_DIR}/")
include_directories("${PROJECT_BINARY_DIR}/")

add_executable(${od_stress_binary} ${od_stress_src})
add_dependencies(${od_stress_binary} build_libs)

if(THREADS_HAVE_PTHREAD_ARG)
    set_property(TARGET ${od_stress_binary} PROPERTY COMPILE_OPTIONS "-pthread")
    set_property(TARGET ${od_stress_binary} PROPERTY INTERFACE_COMPILE_OPTIONS "-pthread")
endif()

target_link_libraries(${od_stress_binary} ${od_libraries} ${CMAKE_THREAD_LIBS_INIT})

if (BUILD_COMPRESSION)
    target_link_libraries(${od_stress_binary} ${compression_libraries})
endif()

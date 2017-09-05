###
# compilation options
###
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")


###
# includes
###
include_directories(${PROJECT_SOURCE_DIR}/includes
                    ${CPP_REDIS_INCLUDES})


###
# executable
###
add_executable(redis_client redis_client.cpp)
target_link_libraries(redis_client cpp_redis)
set_target_properties(redis_client
                      PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/target")

add_executable(redis_subscriber redis_subscriber.cpp)
target_link_libraries(redis_subscriber cpp_redis)
set_target_properties(redis_subscriber
                      PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/target")

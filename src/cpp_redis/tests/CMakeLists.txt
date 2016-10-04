###
# project
###
set(PROJECT cpp_redis_tests)
project(${PROJECT} CXX)


###
# compilation options
###
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")


###
# includes
###
include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/includes
                    ${CPP_REDIS_INCLUDES}
                    ${GTEST_INCLUDES})


###
# sources
###
set(SOURCES "")
set(DIRS "sources" "sources/**")
foreach(dir ${DIRS})
  # get directory sources
  file(GLOB s_${dir} "${dir}/*.cpp")
  # set sources
  set(SOURCES ${SOURCES} ${s_${dir}})
endforeach()


###
# executable
###
add_executable(${PROJECT} ${SOURCES})
target_link_libraries(${PROJECT} cpp_redis gtest pthread)
set_target_properties(${PROJECT}
                      PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/target")

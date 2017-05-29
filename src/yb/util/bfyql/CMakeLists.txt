# Copyright (c) YugaByte, Inc.

# Build the code generator.
add_executable(bfyql_codegen
               directory.cc
               codegen.cc)
target_link_libraries(bfyql_codegen yb_util)

# Use ${CMAKE_BINARY_DIR}/bin/bfyql_codegen to generate builtin function files.
SET(BF_GENERATED_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gen_opcode_table.cc
    ${CMAKE_CURRENT_BINARY_DIR}/gen_opspec_table.cc
    ${CMAKE_CURRENT_BINARY_DIR}/gen_opcodes.h
    ${CMAKE_CURRENT_BINARY_DIR}/gen_operator.h
    ${CMAKE_CURRENT_BINARY_DIR}/gen_bfunc_table.h)

add_custom_command(OUTPUT ${BF_GENERATED_FILES}
                   DEPENDS bfyql_codegen
                   COMMAND bfyql_codegen ${CMAKE_CURRENT_BINARY_DIR})

# Build the actual library.
set(YB_BFYQL_SRCS
    directory.cc
    bfyql.cc
    ${BF_GENERATED_FILES})

set(YB_BFYQL_LIBS
    yb_util)

ADD_YB_LIBRARY(yb_bfyql
                       SRCS ${YB_BFYQL_SRCS}
                       DEPS ${YB_BFYQL_LIBS})

# Make sure code-generator is built before yb_bfyql library.
add_dependencies(yb_bfyql bfyql_codegen)

# Build the test cases.
set(YB_TEST_LINK_LIBS yb_bfyql ${YB_MIN_TEST_LIBS} yb_common)
ADD_YB_TEST(bfyql-test)

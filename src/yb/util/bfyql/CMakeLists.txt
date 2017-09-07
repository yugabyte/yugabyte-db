# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

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

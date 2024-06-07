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

set(YB_PCH_PREFIX bfql)
set(YB_PCH_DEP_LIBS gutil yb_test_util yb_util)

# Build the code generator.
add_executable(bfql_codegen
               directory.cc
               codegen.cc)
target_link_libraries(bfql_codegen yb_util yb_common_proto consensus_metadata_proto)
# Use ${CMAKE_BINARY_DIR}/bin/bfql_codegen to generate builtin function files.
SET(BF_GENERATED_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gen_opcode_table.cc
    ${CMAKE_CURRENT_BINARY_DIR}/gen_opspec_table.cc
    ${CMAKE_CURRENT_BINARY_DIR}/gen_opcodes.h
    ${CMAKE_CURRENT_BINARY_DIR}/gen_operator.h
    ${CMAKE_CURRENT_BINARY_DIR}/gen_bfunc_table.cc)

add_custom_command(OUTPUT ${BF_GENERATED_FILES}
                   DEPENDS bfql_codegen
                   COMMAND bfql_codegen ${CMAKE_CURRENT_BINARY_DIR})

# Build the actual library.
set(YB_BFQL_SRCS
    directory.cc
    bfql.cc
    ${BF_GENERATED_FILES})

set(YB_BFQL_LIBS
    gutil
    yb_bfcommon
    yb_util
    yb_common_base)

ADD_YB_LIBRARY(yb_bfql
               SRCS ${YB_BFQL_SRCS}
               DEPS ${YB_BFQL_LIBS})

# Make sure code-generator is built before yb_bfql library.
add_dependencies(yb_bfql bfql_codegen)

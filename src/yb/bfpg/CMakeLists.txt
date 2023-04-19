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

set(YB_PCH_PREFIX bfpg)
set(YB_PCH_DEP_LIBS gutil yb_test_util yb_util)

# Build the code generator.
add_executable(bfpg_codegen
               directory.cc
               codegen.cc)
target_link_libraries(bfpg_codegen yb_util yb_common_proto consensus_metadata_proto)

# Use ${CMAKE_BINARY_DIR}/bin/bfpg_codegen to generate builtin function files.
SET(BFPG_GENERATED_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gen_opcode_table.cc
    ${CMAKE_CURRENT_BINARY_DIR}/gen_opspec_table.cc
    ${CMAKE_CURRENT_BINARY_DIR}/gen_opcodes.h
    ${CMAKE_CURRENT_BINARY_DIR}/gen_operator.h
    ${CMAKE_CURRENT_BINARY_DIR}/gen_bfunc_table.cc)

add_custom_command(OUTPUT ${BFPG_GENERATED_FILES}
                   DEPENDS bfpg_codegen
                   COMMAND bfpg_codegen ${CMAKE_CURRENT_BINARY_DIR})

# Build the actual library.
set(YB_BFPG_SRCS
    directory.cc
    bfpg.cc
    ${BFPG_GENERATED_FILES})

set(YB_BFPG_LIBS
    yb_bfcommon
    yb_util
    yb_common_base)

ADD_YB_LIBRARY(yb_bfpg
               SRCS ${YB_BFPG_SRCS}
               DEPS ${YB_BFPG_LIBS})

add_dependencies(
    # Make sure code-generator is built before yb_bfpg library.
    yb_bfpg bfpg_codegen
    # Also make sure that the protobuf headers we depend on here are generated.
    gen_src_yb_common_pgsql_protocol_proto
    gen_src_yb_common_ql_protocol_proto
    )

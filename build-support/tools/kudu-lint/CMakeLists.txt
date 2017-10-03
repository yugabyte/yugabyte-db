# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

cmake_minimum_required(VERSION 2.8)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake_modules" ${CMAKE_MODULE_PATH})

# Find LLVM
find_package(LLVM REQUIRED COMPONENTS
  option
  analysis
  bitreader
  mcparser
  instrumentation
)
find_package(Clang REQUIRED COMPONENTS
    clang
    clangAnalysis
    clangAST
    clangASTMatchers
    clangBasic
    clangCodeGen
    clangDriver
    clangEdit
    clangFrontend
    clangFrontendTool
    clangLex
    clangParse
    clangSema
    clangSerialization
    clangTooling
)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${CLANG_INCLUDE})
include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

set(START_GROUP "-Wl,--start-group")
set(END_GROUP "-Wl,--end-group")

add_executable(kudu-lint kudu-lint.cc)
target_link_libraries(kudu-lint
  ${START_GROUP}
  ${CLANG_LIBS}
  ${LLVM_LIBS}
  ${END_GROUP}
  ${LLVM_LDFLAGS}
  )

# Disable RTTI since we have to inherit from Clang-provided classes,
# and Clang does not enable RTTI.
set_target_properties(kudu-lint PROPERTIES
    COMPILE_FLAGS "${LLVM_CFLAGS} -fno-rtti -g")

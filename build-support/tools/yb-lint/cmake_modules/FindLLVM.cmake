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
# Find LLVM
#
# It defines the following variables
#  LLVM_FOUND        - True if llvm found.
#  LLVM_INCLUDE_DIRS - where to find llvm include files
#  LLVM_LIBRARY_DIRS - where to find llvm libs
#  LLVM_CFLAGS       - llvm compiler flags
#  LLVM_LDFLAGS      - llvm linker flags
#  LLVM_LIBS         - list of llvm libs for working with modules.

if(NOT DEFINED CLANG_ROOT)
  set(CLANG_ROOT $ENV{CLANG_ROOT})
endif()

find_program(LLVM_CONFIG_EXECUTABLE llvm-config
  DOC "llvm-config executable"
  HINTS ${CLANG_ROOT}/bin)

if(LLVM_CONFIG_EXECUTABLE)
  message(STATUS "LLVM llvm-config found at: ${LLVM_CONFIG_EXECUTABLE}")
elseif(LLVM_FIND_REQUIRED)
  message(FATAL_ERROR "Could NOT find llvm-config executable")
endif()

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
  OUTPUT_VARIABLE LLVM_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REGEX REPLACE "^([0-9]+)\\.([0-9]+).*" "\\1" LLVM_VERSION_MAJOR ${LLVM_VERSION})
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+).*" "\\2" LLVM_VERSION_MINOR ${LLVM_VERSION})

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --includedir
  OUTPUT_VARIABLE LLVM_INCLUDE_DIRS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
  OUTPUT_VARIABLE LLVM_LIBRARY_DIRS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --cppflags
  OUTPUT_VARIABLE LLVM_CFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags
  OUTPUT_VARIABLE LLVM_LDFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs ${LLVM_FIND_COMPONENTS}
  OUTPUT_VARIABLE LLVM_LIBS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(LLVM_FOUND TRUE)

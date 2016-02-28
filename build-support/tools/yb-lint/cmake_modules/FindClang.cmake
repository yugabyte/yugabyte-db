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
# Find Clang
#
# It defines the following variables
# CLANG_FOUND        - True if Clang found.
# CLANG_INCLUDE_DIRS - where to find Clang include files
# CLANG_LIBS         - list of clang libs
# CLANG_LDFLAGS      - list w/format: -lclangAST -lclangLex...
if(NOT LLVM_INCLUDE_DIRS OR NOT LLVM_LIBRARY_DIRS)
  message(FATAL_ERROR "Clang support requires LLVM to be set up first.")
endif()

if(NOT Clang_FIND_COMPONENTS)
  message(FATAL_ERROR "Must specify which clang COMPONENTS are required")
endif()

foreach(component ${Clang_FIND_COMPONENTS})
  find_library(CLANG_${component}_LIB ${component}
    PATHS ${LLVM_LIBRARY_DIRS} ${CLANG_LIBRARY_DIRS})
  if(CLANG_${component}_LIB)
    message(STATUS "Adding Clang component: ${component}")
    set(CLANG_LIBS ${CLANG_LIBS} ${CLANG_${component}_LIB})
    set(CLANG_LDFLAGS ${CLANG_LDFLAGS} "-l${component}")
  elseif(Clang_FIND_REQUIRED_${component})
    message(FATAL_ERROR "Could not find required Clang component ${component}\n"
      "Please set CLANG_ROOT.")
  endif()
endforeach(component)

find_path(CLANG_INCLUDE_DIRS clang/Basic/Version.h HINTS ${LLVM_INCLUDE_DIRS})
if(CLANG_LIBS AND CLANG_INCLUDE_DIRS)
  message(STATUS "Found Clang libs: ${CLANG_LIBS}")
  message(STATUS "Found Clang includes: ${CLANG_INCLUDE_DIRS}")
elseif(Clang_FIND_REQUIRED)
  message(FATAL_ERROR "Could NOT find Clang")
endif()

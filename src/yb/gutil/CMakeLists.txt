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
#
# The following only applies to changes made to this file as part of YugabyteDB development.
#
# Portions Copyright (c) YugabyteDB, Inc.
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

add_library(gutil_c STATIC dynamic_annotations.c utf/rune.c)

set(YB_PCH_PREFIX gutil)
set(YB_PCH_DEP_LIBS gmock gtest)

set(GUTIL_SRCS
  bits.cc
  callback_internal.cc
  casts.cc
  cpu.cc
  hash/city.cc
  hash/hash.cc
  hash/jenkins.cc
  int128.cc
  mathlimits.cc
  once.cc
  ref_counted.cc
  ref_counted_memory.cc
  spinlock.cc
  spinlock_internal.cc
  stringprintf.cc
  strings/ascii_ctype.cc
  strings/charset.cc
  strings/escaping.cc
  strings/human_readable.cc
  strings/join.cc
  strings/memutil.cc
  strings/numbers.cc
  strings/split.cc
  strings/strcat.cc
  strings/stringpiece.cc
  strings/strip.cc
  strings/substitute.cc
  strings/util.cc
  strtoint.cc
  sysinfo.cc
  threading/thread_collision_warner.cc
  walltime.cc)

if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
  list(APPEND GUTIL_SRCS atomicops-internals-x86.cc)
elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^(ppc64|ppc64le)$")
  # PowerPC atomics are header-only in atomicops-internals-powerpc.h
  # No additional source files needed
  
  # Disable -Wshorten-64-to-32 for files that include linux_syscall_support.h on PowerPC64
  # The linux_syscall_support.h header has unavoidable long->int conversions
  set_source_files_properties(
    spinlock.cc
    spinlock_internal.cc
    threading/thread_collision_warner.cc
    PROPERTIES COMPILE_FLAGS "-Wno-shorten-64-to-32"
  )
endif()

set(GUTIL_LIBS
  glog
  gflags
  protobuf
  gutil_c)

if (NOT APPLE)
  set(GUTIL_LIBS
    ${GUTIL_LIBS}
    rt) # clock_gettime() requires -lrt
endif()

ADD_YB_LIBRARY(gutil
  SRCS ${GUTIL_SRCS}
  DEPS ${GUTIL_LIBS})

ADD_YB_TEST(dynamic_annotations-test)
YB_TEST_TARGET_LINK_LIBRARIES(dynamic_annotations-test absl)
ADD_YB_TEST(sysinfo-test)
ADD_YB_TEST(strings/string_util-test)
ADD_YB_TEST(hash/jenkins-test)


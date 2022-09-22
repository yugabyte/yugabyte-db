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

set(YB_PCH_PREFIX fs)
set(YB_PCH_DEP_LIBS gmock gtest gutil yb_test_util yb_util)

PROTOBUF_GENERATE_CPP(
  FS_PROTO_SRCS FS_PROTO_HDRS FS_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES fs.proto)
ADD_YB_LIBRARY(fs_proto
  SRCS ${FS_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${FS_PROTO_TGTS})

add_library(yb_fs fs_manager.cc)

target_link_libraries(yb_fs
  fs_proto
  yb_util
  gutil)

# Tests
set(YB_TEST_LINK_LIBS yb_fs ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(fs_manager-test)

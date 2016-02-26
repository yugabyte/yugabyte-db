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

PROTOBUF_GENERATE_CPP(
  FS_PROTO_SRCS FS_PROTO_HDRS FS_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES fs.proto)
ADD_EXPORTABLE_LIBRARY(fs_proto
  SRCS ${FS_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${FS_PROTO_TGTS})

add_library(kudu_fs
  block_id.cc
  block_manager.cc
  block_manager_metrics.cc
  block_manager_util.cc
  file_block_manager.cc
  fs_manager.cc
  log_block_manager.cc)

target_link_libraries(kudu_fs
  fs_proto
  kudu_util
  gutil)

# Tests
set(KUDU_TEST_LINK_LIBS kudu_fs ${KUDU_MIN_TEST_LIBS})
ADD_KUDU_TEST(block_manager-test)
ADD_KUDU_TEST(block_manager_util-test)
ADD_KUDU_TEST(block_manager-stress-test RUN_SERIAL true)
ADD_KUDU_TEST(fs_manager-test)

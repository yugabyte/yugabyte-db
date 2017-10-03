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
  CFILE_PROTO_SRCS CFILE_PROTO_HDRS CFILE_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES cfile.proto)
set(CFILE_PROTO_LIBS
  kudu_common_proto
  protobuf)
ADD_EXPORTABLE_LIBRARY(cfile_proto
  SRCS ${CFILE_PROTO_SRCS}
  DEPS ${CFILE_PROTO_LIBS}
  NONLINK_DEPS ${CFILE_PROTO_TGTS})

add_library(cfile
  binary_dict_block.cc
  binary_plain_block.cc
  binary_prefix_block.cc
  block_cache.cc
  block_compression.cc
  bloomfile.cc
  bshuf_block.cc
  cfile_reader.cc
  cfile_util.cc
  cfile_writer.cc
  compression_codec.cc
  gvint_block.cc
  index_block.cc
  index_btree.cc
  type_encodings.cc)

target_link_libraries(cfile
  kudu_common
  kudu_fs
  kudu_util
  gutil
  cfile_proto
  lz4
  bitshuffle
  snappy
  zlib)

# Tests
set(KUDU_TEST_LINK_LIBS cfile ${KUDU_MIN_TEST_LIBS})
ADD_KUDU_TEST(index-test)
ADD_KUDU_TEST(cfile-test)
ADD_KUDU_TEST(encoding-test LABELS no_tsan)
ADD_KUDU_TEST(bloomfile-test)
ADD_KUDU_TEST(mt-bloomfile-test)
ADD_KUDU_TEST(block_cache-test)
ADD_KUDU_TEST(compression-test)

# Tools
add_executable(cfile-dump cfile-dump.cc)
target_link_libraries(cfile-dump cfile ${KUDU_BASE_LIBS})

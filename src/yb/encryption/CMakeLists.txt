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

set(YB_PCH_PREFIX encryption)
set(YB_PCH_DEP_LIBS yb_test_util yb_util)

#########################################
# encryption_proto
#########################################
YRPC_GENERATE(
  ENCRYPTION_PROTO_SRCS ENCRYPTION_PROTO_HDRS ENCRYPTION_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  NO_SERVICE_PROTO_FILES encryption.proto)
ADD_YB_LIBRARY(encryption_proto
  SRCS ${ENCRYPTION_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${ENCRYPTION_PROTO_TGTS})

ADD_YB_LIBRARY(encryption
  SRCS
      ctr_cipher_stream.cc
      encrypted_file.cc
      encrypted_file_factory.cc
      encryption_util.cc
      header_manager_impl.cc
      universe_key_manager.cc
  DEPS
      encryption_proto
      yb_util
      yrpc)


ADD_YB_TEST_LIBRARY(
  encryption_test_util
  SRCS encryption_test_util.cc header_manager_mock_impl.cc
  DEPS yb_test_util)

set(YB_TEST_LINK_LIBS encryption encryption_test_util ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(ctr_cipher_stream-test)
ADD_YB_TEST(encrypted_env-test)

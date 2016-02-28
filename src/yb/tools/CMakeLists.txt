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

set(LINK_LIBS
  yb_client
  log
  consensus
  tserver
  yb_common
  yb_fs
  yb_util
  gutil
  cfile
  tablet
  ${YB_BASE_LIBS}
)

add_library(yb_tools_util
  data_gen_util.cc)
target_link_libraries(yb_tools_util
  ${LINK_LIBS})

add_executable(create-demo-table create-demo-table.cc)
target_link_libraries(create-demo-table
  ${LINK_LIBS})

add_executable(insert-generated-rows insert-generated-rows.cc)
target_link_libraries(insert-generated-rows
  yb_tools_util
  ${LINK_LIBS})

add_executable(yb-admin yb-admin.cc)
target_link_libraries(yb-admin
  ${LINK_LIBS})

add_executable(yb-ts-cli ts-cli.cc)
target_link_libraries(yb-ts-cli
  ${LINK_LIBS})

add_library(fs_tool fs_tool.cc)
target_link_libraries(fs_tool
  gutil
  yb_common
  server_common
  consensus
  tablet)

add_executable(yb-fs_list fs_list-tool.cc)
target_link_libraries(yb-fs_list
  ${LINK_LIBS}
  fs_tool)

add_executable(yb-fs_dump fs_dump-tool.cc)
target_link_libraries(yb-fs_dump
  ${LINK_LIBS}
  fs_tool)

add_library(ksck
    ksck.cc
    ksck_remote.cc
)
target_link_libraries(ksck
  master_proto
  server_base_proto
  tserver_proto
  tserver_service_proto
  ${YB_BASE_LIBS}
)

add_executable(yb-ksck yb-ksck.cc)
target_link_libraries(yb-ksck
  ksck
)

add_executable(yb-pbc-dump pbc-dump.cc)
target_link_libraries(yb-pbc-dump
  ${LINK_LIBS}
)

set(YB_TEST_LINK_LIBS
  ksck
  yb_tools_util
  integration-tests
  ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(ksck-test)
ADD_YB_TEST(ksck_remote-test)
ADD_YB_TEST(yb-admin-test)
ADD_YB_TEST_DEPENDENCIES(yb-admin-test
  yb-admin)
ADD_YB_TEST(yb-ts-cli-test)
ADD_YB_TEST_DEPENDENCIES(yb-ts-cli-test
  yb-ts-cli)


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
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
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

include_directories(../redisserver/cpp_redis/includes)

set(INTEGRATION_TESTS_SRCS
  cluster_itest_util.cc
  cluster_verifier.cc
  external_mini_cluster.cc
  external_mini_cluster_fs_inspector.cc
  mini_cluster.cc
  test_workload.cc
  load_generator.cc
  yb_table_test_base.cc
  yb_mini_cluster_test_base.cc
  redis_table_test_base.cc
)

add_library(integration-tests ${INTEGRATION_TESTS_SRCS})
target_link_libraries(integration-tests
  cpp_redis
  tserver
  tserver_test_util
  master
  ysck
  yb_client
  yb_client_test_util
  yb_fs
  yb_test_util
  yb-redis)
add_dependencies(integration-tests
  yb-tserver
  yb-master)

# Tests
set(YB_TEST_LINK_LIBS integration-tests ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(alter_table-test)
ADD_YB_TEST(alter_table-randomized-test)
ADD_YB_TEST(registration-test RESOURCE_LOCK "master-web-port")
ADD_YB_TEST(clock_synchronization-itest)
ADD_YB_TEST(client_failover-itest)
ADD_YB_TEST(client-stress-test
  RESOURCE_LOCK "master-rpc-ports"
  RUN_SERIAL true)
ADD_YB_TEST(cluster_trace-test)
# Tests which fail on purpose for checking Jenkins test failures reporting, disabled
# (commented out) by default:
# ADD_YB_TEST(test_failures-test)
# ADD_YB_TEST(non_gtest_failures-test.sh)

# Not sure if we really need RUN_SERIAL here as this might not be a resource-intensive test.
ADD_YB_TEST(master_replication-itest RESOURCE_LOCK "master-rpc-ports" RUN_SERIAL true)

ADD_YB_TEST(master_sysnamespace-itest RESOURCE_LOCK "master-rpc-ports")
ADD_YB_TEST(master_failover-itest RESOURCE_LOCK "master-rpc-ports")
ADD_YB_TEST(master_config-itest RESOURCE_LOCK "master-rpc-ports")
ADD_YB_TEST(raft_consensus-itest RUN_SERIAL true)
ADD_YB_TEST(flush-test RUN_SERIAL true)
ADD_YB_TEST(ts_tablet_manager-itest)
ADD_YB_TEST(ts_recovery-itest)
ADD_YB_TEST(create-table-stress-test)
ADD_YB_TEST(delete_table-test)
ADD_YB_TEST(external_mini_cluster-test RESOURCE_LOCK "master-rpc-ports")
ADD_YB_TEST(linked_list-test RESOURCE_LOCK "master-rpc-ports")
ADD_YB_TEST(all_types-itest RESOURCE_LOCK "master-rpc-ports")
ADD_YB_TEST(remote_bootstrap-itest)
ADD_YB_TEST(tablet_replacement-itest)
ADD_YB_TEST(create-table-itest)
ADD_YB_TEST(placement_info-itest)
ADD_YB_TEST(kv_table-test RUN_SERIAL true)
ADD_YB_TEST(kv_table_ts_failover-test RUN_SERIAL true)
ADD_YB_TEST(stepdown_under_load-test RUN_SERIAL true)

# Some tests have additional dependencies
set(YB_TEST_LINK_LIBS yb_client yb_tools_util ${YB_TEST_LINK_LIBS})
ADD_YB_TEST(full_stack-insert-scan-test RUN_SERIAL true)
ADD_YB_TEST(redis_table-test RUN_SERIAL true)
ADD_YB_TEST(update_scan_delta_compact-test RUN_SERIAL true)
ADD_YB_TEST(flex_partitioning-itest)

# Additional tests
YB_INCLUDE_EXTENSIONS()

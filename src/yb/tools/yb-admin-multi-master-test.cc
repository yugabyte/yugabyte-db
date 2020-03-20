// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
// Tests for the yb-admin command-line tool with multiple masters.

#include <regex>

#include <boost/algorithm/string.hpp>

#include <gtest/gtest.h>

#include "yb/integration-tests/external_mini_cluster-itest-base.h"

#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"

namespace yb {
namespace tools {

namespace {

static const char* const kAdminToolName = "yb-admin";

} // namespace

class YBAdminMultiMasterTest : public ExternalMiniClusterITestBase {
};

TEST_F(YBAdminMultiMasterTest, InitialMasterAddresses) {
  auto admin_path = GetToolPath(kAdminToolName);
  ASSERT_NO_FATALS(StartCluster({}, {}, 1/*num tservers*/, 3/*num masters*/));

  // Verify that yb-admin query results with --init_master_addrs match
  // query results with the full master addresses
  int non_leader_idx = -1;
  ASSERT_OK(cluster_->GetFirstNonLeaderMasterIndex(&non_leader_idx));
  auto non_leader = cluster_->master(non_leader_idx);
  HostPort non_leader_hp = non_leader->bound_rpc_hostport();
  std::string output1;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "-init_master_addrs", non_leader_hp.ToString(),
      "list_all_masters"), &output1));
  LOG(INFO) << "init_master_addrs: list_all_masters: " << output1;
  std::string output2;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "-master_addresses", cluster_->GetMasterAddresses(),
      "list_all_masters"), &output2));
  LOG(INFO) << "full master_addresses: list_all_masters: " << output2;
  ASSERT_EQ(output1, output2);

  output1.clear();
  output2.clear();
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "-init_master_addrs", non_leader_hp.ToString(),
      "list_all_tablet_servers"), &output1));
  LOG(INFO) << "init_master_addrs: list_all_tablet_servers: " << output1;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "-master_addresses", cluster_->GetMasterAddresses(),
      "list_all_tablet_servers"), &output2));
  LOG(INFO) << "full master_addresses: list_all_tablet_servers: " << output2;
  ASSERT_EQ(output1, output2);
}

}  // namespace tools
}  // namespace yb

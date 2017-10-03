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
// Tests for the kudu-admin command-line tool.

#include <boost/assign/list_of.hpp>
#include <gtest/gtest.h>

#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/integration-tests/cluster_itest_util.h"
#include "kudu/integration-tests/external_mini_cluster-itest-base.h"
#include "kudu/integration-tests/test_workload.h"
#include "kudu/util/path_util.h"
#include "kudu/util/subprocess.h"

using boost::assign::list_of;
using kudu::itest::TabletServerMap;
using kudu::itest::TServerDetails;
using strings::Split;
using strings::Substitute;

namespace kudu {
namespace tools {

static const char* const kTsCliToolName = "kudu-ts-cli";

class KuduTsCliTest : public ExternalMiniClusterITestBase {
 protected:
  // Figure out where the admin tool is.
  string GetTsCliToolPath() const;
};

string KuduTsCliTest::GetTsCliToolPath() const {
  string exe;
  CHECK_OK(Env::Default()->GetExecutablePath(&exe));
  string binroot = DirName(exe);
  string tool_path = JoinPathSegments(binroot, kTsCliToolName);
  CHECK(Env::Default()->FileExists(tool_path)) << "kudu-admin tool not found at " << tool_path;
  return tool_path;
}

// Test deleting a tablet.
TEST_F(KuduTsCliTest, TestDeleteTablet) {
  MonoDelta timeout = MonoDelta::FromSeconds(30);
  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--enable_leader_failure_detection=false");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  NO_FATALS(StartCluster(ts_flags, master_flags));

  TestWorkload workload(cluster_.get());
  workload.Setup(); // Easy way to create a new tablet.

  vector<tserver::ListTabletsResponsePB::StatusAndSchemaPB> tablets;
  for (const itest::TabletServerMap::value_type& entry : ts_map_) {
    TServerDetails* ts = entry.second;
    ASSERT_OK(itest::WaitForNumTabletsOnTS(ts, 1, timeout, &tablets));
  }
  string tablet_id = tablets[0].tablet_status().tablet_id();

  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    ASSERT_OK(itest::WaitUntilTabletRunning(ts_map_[cluster_->tablet_server(i)->uuid()],
                                            tablet_id, timeout));
  }

  string exe_path = GetTsCliToolPath();
  vector<string> argv;
  argv.push_back(exe_path);
  argv.push_back("--server_address");
  argv.push_back(cluster_->tablet_server(0)->bound_rpc_addr().ToString());
  argv.push_back("delete_tablet");
  argv.push_back(tablet_id);
  argv.push_back("Deleting for kudu-ts-cli-test");
  ASSERT_OK(Subprocess::Call(argv));

  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(0, tablet_id, tablet::TABLET_DATA_TOMBSTONED));
  TServerDetails* ts = ts_map_[cluster_->tablet_server(0)->uuid()];
  ASSERT_OK(itest::WaitUntilTabletInState(ts, tablet_id, tablet::SHUTDOWN, timeout));
}

} // namespace tools
} // namespace kudu

// Copyright (c) YugabyteDB, Inc.
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

#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/meta_data_cache.h"

#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster_fs_inspector.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/path_util.h"
#include "yb/util/status_log.h"
#include "yb/util/subprocess.h"

#include "yb/yql/cql/cqlserver/cql_service.h"

using std::string;
using std::vector;

namespace yb {
namespace integration_tests {
namespace {

const auto kDefaultTimeout = 30000ms;
const auto kServerIndex = 0;
const char* const kTsCliToolName = "yb-ts-cli";

template <class TabletServerType>
void RunTsCliTool(const TabletServerType* ts, const vector<string>& argv) {
  string exe_path = GetToolPath(kTsCliToolName);
  vector<string> cmd_line = {exe_path, "--server_address", AsString(ts->bound_rpc_addr())};
  cmd_line.insert(cmd_line.end(), argv.begin(), argv.end());

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    LOG(INFO) << "Run tool: " << ToString(cmd_line);
    Status s = Subprocess::Call(cmd_line);
    LOG(INFO) << "Tool result: " << s;
    return s.ok();
  }, kDefaultTimeout * 2, "CallTsCli"));
}

} // anonymous namespace

class YBTsCliITest : public YBTableTestBase {
 protected:
  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return true; }

  int num_tablets() override {
    return 12;
  }

  int num_drives() override {
    return 2;
  }

  bool enable_ysql() override {
    // Do not create the transaction status table.
    return false;
  }

  void WaitForTablet(const string& tablet_id) {
    ExternalTabletServer* ts = external_mini_cluster()->tablet_server(kServerIndex);
    tserver::TabletServerServiceProxy proxy(&external_mini_cluster()->proxy_cache(),
                                                 ts->bound_rpc_addr());
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      rpc::RpcController rpc;
      tserver::GetTabletStatusRequestPB req;
      tserver::GetTabletStatusResponsePB resp;
      rpc.set_timeout(kDefaultTimeout);
      req.set_tablet_id(tablet_id);
      RETURN_NOT_OK(proxy.GetTabletStatus(req, &resp, &rpc));
      if (resp.has_error() || resp.tablet_status().state() != tablet::RaftGroupStatePB::RUNNING) {
        return false;
      }
      return true;
    }, kDefaultTimeout * 2, "WaitForTablet"));
  }
};

// Test steps:
// Setup a cluster with three drives on each ts, reduce drives on first ts
// create table and increase drives on first ts to lead to bad distribution across drives
// call rpc DeleteTablet and restart a ts to validate that tablet was moved
TEST_F(YBTsCliITest, MoveTablet) {
  auto inspect = std::make_unique<itest::ExternalMiniClusterFsInspector>(external_mini_cluster());

  string tablet_id;
  string root_dir;

  ExternalTabletServer* ts = external_mini_cluster()->tablet_server(kServerIndex);

  ts->Shutdown();
  ASSERT_OK(ts->SetNumDrives(3));
  ASSERT_OK(ts->Restart());

  size_t max_count = 0;
  // Look for TS with max number of tablets on one drive and get one tablet to try move it.
  for (const auto& drive_and_tablets : inspect->DrivesOnTS(kServerIndex)) {
    const vector<string>& tablets = drive_and_tablets.second;
    if (tablets.size() > max_count) {
      root_dir = drive_and_tablets.first;
      max_count = tablets.size();
      tablet_id = tablets.front();
    }
  }

  // Tablet id to be moved should not be empty.
  ASSERT_FALSE(tablet_id.empty());
  WaitForTablet(tablet_id);

  RunTsCliTool(ts, {"delete_tablet", "--force", tablet_id, "Deleting for yb-ts-cli-itest"});

  ts->Shutdown();
  ASSERT_OK(ts->Restart());

  ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(num_tablet_servers(),
                                                              kDefaultTimeout));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
      for (const auto& drive_and_tablets : inspect->DrivesOnTS(kServerIndex)) {
        const vector<string>& tablets = drive_and_tablets.second;
        if (find(tablets.begin(), tablets.end(), tablet_id) != tablets.end()) {
          bool same = drive_and_tablets.first == root_dir;
          if (same) {
            LOG(INFO) << "TS still have tablet " << tablet_id << " at " << root_dir;
          }
          return !same;
        }
      }
      return false;
    }, kDefaultTimeout * 2, "WaitForTabletMovedFromTS"));
}

class  YBTsCliCqlITest : public CqlTestBase<MiniCluster> {
 public:
  virtual ~YBTsCliCqlITest() = default;

  int num_tablet_servers() override {
    return 1;
  }
};

TEST_F(YBTsCliCqlITest, TestClearYCQLMetaDataCache) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  auto cql = [&](const std::string query) { ASSERT_OK(session.ExecuteQuery(query)); };
  cql("CREATE TABLE test_tbl(h INT PRIMARY KEY, v INT) WITH transactions = {'enabled': 'true'}");
  cql("CREATE INDEX ON test_tbl(v)");
  cql("INSERT INTO test_tbl(h, v) VALUES(1, 2)");

  auto select = [&]() -> Result<string> {
    auto result = VERIFY_RESULT(session.ExecuteWithResult("SELECT h FROM test_tbl WHERE v=2"));
    auto iter = result.CreateIterator();
    DFATAL_OR_RETURN_ERROR_IF(!iter.Next(), STATUS(NotFound, "Did not find result in test table."));
    auto row = iter.Row();
    return row.Value(0).ToString();
  };

  EXPECT_EQ(ASSERT_RESULT(select()), "1");

  const std::shared_ptr<client::YBMetaDataCache>& cache =
      cql_server_->TEST_cql_service()->metadata_cache();
  LOG(INFO) << "Number of cached YCQL table entries = " << cache->TEST_NumberOfCachedTableEntries();
  ASSERT_GT(cache->TEST_NumberOfCachedTableEntries(), 0)
      << "Expected not empty YCQL MetaData cache";

  tserver::MiniTabletServer* ts = cluster_->mini_tablet_server(kServerIndex);
  RunTsCliTool(ts, {"clear_ycql_metadatacache"});

  LOG(INFO) << "Number of cached YCQL table entries = " << cache->TEST_NumberOfCachedTableEntries();
  ASSERT_EQ(cache->TEST_NumberOfCachedTableEntries(), 0) << "Expected empty YCQL MetaData cache";
}

} // namespace integration_tests
} // namespace yb

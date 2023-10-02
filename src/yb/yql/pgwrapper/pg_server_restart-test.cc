// Copyright (c) YugaByte, Inc.
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

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/master/master_cluster.proxy.h"

#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"

using namespace std::literals;

DECLARE_int32(replication_factor);

namespace yb::pgwrapper {

class PgSingleServerRestartTest : public LibPqTestBase {
 protected:
  void SetUp() override {
    FLAGS_replication_factor = 1;
    LibPqTestBase::SetUp();
  }
  int GetNumTabletServers() const override {
    return 1;
  }
};

// Test for rf-1 setup, during tablet bootstrap, max_safe_time_without_lease_ is updated to kNow
// before pending replicates are applied, and cause hybrid time too low issue.
TEST_F(PgSingleServerRestartTest, GetSafeTimeBeforeConsensusStarted) {
  auto conn = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (c int) split into 1 tablets"));

  auto tablet_id = ASSERT_RESULT(GetSingleTabletId("t"));
  LOG(INFO) << "Tablet id is " << tablet_id;

  auto leader = cluster_->tablet_server(0);

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.Execute("INSERT INTO t (c) values (3)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (c) values (3)"));

  ASSERT_OK(cluster_->SetFlag(leader, "TEST_pause_update_majority_replicated", "true"));
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&] {
    ASSERT_NOK(conn2.Execute("INSERT INTO t (c) values (4)"));
  });
  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(
      cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>(), &cluster_->proxy_cache()));
  ASSERT_OK(itest::WaitForServersToAgree(10s, ts_map, tablet_id, /* minimum_index = */ 4));
  SleepFor(1s);

  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(cluster_->tablet_server(0), {tablet_id}, false));
  leader->Shutdown(SafeShutdown::kFalse);
  ASSERT_OK(cluster_->WaitForTSToCrash(leader, 10s));

  // Defer applying pending operations at RaftConsensus::Start.
  ASSERT_OK(leader->Restart(
      ExternalMiniClusterOptions::kDefaultStartCqlProxy,
      { std::make_pair("TEST_pause_replica_start_before_triggering_pending_operations", "true") }));
  SleepFor(3s);
  // Resume applying pending operations. If max_safe_time_without_lease_ has been updated to kNow,
  // will raise 'Hybrid time too low' issue.
  // See more in https://github.com/yugabyte/yugabyte-db/issues/17682
  ASSERT_OK(cluster_->SetFlag(
      leader, "TEST_pause_replica_start_before_triggering_pending_operations", "false"));
  ASSERT_OK(cluster_->WaitForTabletsRunning(leader, 10s));

  thread_holder.WaitAndStop(10s);
}
} // namespace yb::pgwrapper

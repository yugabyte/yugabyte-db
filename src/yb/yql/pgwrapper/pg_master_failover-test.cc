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

#include "yb/master/mini_master.h"
#include "yb/master/ts_manager.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tools/tools_test_utils.h"

#include "yb/util/backoff_waiter.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

DECLARE_bool(TEST_timeout_non_leader_master_rpcs);

namespace yb::pgwrapper {

YB_STRONGLY_TYPED_BOOL(WaitForTS);

class PgMasterFailoverTest : public PgMiniTestBase {
 protected:
  void ElectNewLeaderAfterShutdown();
  void TestNonRespondingMaster(WaitForTS wait_for_ts);
 public:
  size_t NumMasters() override {
    return 3;
  }
};

void PgMasterFailoverTest::ElectNewLeaderAfterShutdown() {
  // Failover to a new master.
  LOG(INFO) << "Failover to new Master";
  auto old_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Shutdown();
  auto new_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  ASSERT_NE(nullptr, new_master);
  ASSERT_NE(old_master, new_master);
  // Wait for all the TabletServers to report in, so we can run CREATE TABLE with working replicas.
  ASSERT_OK(cluster_->WaitForAllTabletServers());
}

TEST_F(PgMasterFailoverTest, YB_DISABLE_TEST_IN_SANITIZERS(DropAllTablesInColocatedDB)) {
  const std::string kDatabaseName = "testdb";
  // Create a colocated DB, create some tables, delete all of them.
  {
    PGConn conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 with colocated=true", kDatabaseName));
    {
      PGConn conn_new = ASSERT_RESULT(ConnectToDB(kDatabaseName));
      ASSERT_OK(conn_new.Execute("CREATE TABLE foo (i int)"));
      ASSERT_OK(conn_new.Execute("DROP TABLE foo"));
    }
  }
  ElectNewLeaderAfterShutdown();
  // Ensure we can still access the colocated DB on restart.
  {
    PGConn conn_new = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(conn_new.Execute("CREATE TABLE foo (i int)"));
  }
}

TEST_F(PgMasterFailoverTest, YB_DISABLE_TEST_IN_SANITIZERS(DropAllTablesInTablegroup)) {
  const std::string database_name = "testdb";
  const std::string tablegroup_name = "testtg";
  // Create a DB, create some tables, delete all of them.
  {
    PGConn conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", database_name));
    {
      PGConn conn_new = ASSERT_RESULT(ConnectToDB(database_name));
      ASSERT_OK(conn_new.ExecuteFormat("CREATE TABLEGROUP $0", tablegroup_name));
      ASSERT_OK(conn_new.ExecuteFormat("CREATE TABLE foo (i int) TABLEGROUP $0", tablegroup_name));
      ASSERT_OK(conn_new.Execute("DROP TABLE foo"));
    }
  }
  ElectNewLeaderAfterShutdown();
  // Ensure we can still access the tablegroup on restart.
  {
    PGConn conn_new = ASSERT_RESULT(ConnectToDB(database_name));
    ASSERT_OK(conn_new.ExecuteFormat("CREATE TABLE foo (i int) TABLEGROUP $0", tablegroup_name));
  }
}

void PgMasterFailoverTest::TestNonRespondingMaster(WaitForTS wait_for_ts) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_timeout_non_leader_master_rpcs) = true;
  tools::TmpDirProvider tmp_dir;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test"));
  conn = ASSERT_RESULT(ConnectToDB("test"));
  ASSERT_OK(conn.Execute("CREATE TABLE t (i INT)"));

  auto peer = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->tablet_peer();
  LOG(INFO) << "Old leader: " << peer->permanent_uuid();
  ASSERT_OK(StepDown(peer, /* new_leader_uuid */ std::string(), ForceStepDown::kTrue));
  ASSERT_OK(WaitFor([this, peer]() -> Result<bool> {
    auto leader = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->tablet_peer();
    if (leader->permanent_uuid() != peer->permanent_uuid()) {
      LOG(INFO) << "New leader: " << leader->permanent_uuid();
      return true;
    }
    return false;
  }, 10s, "Wait leader change"));

  if (wait_for_ts) {
    master::TSManager& ts_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->ts_manager();
    ASSERT_OK(WaitFor([this, &ts_manager]() -> Result<bool> {
      return ts_manager.GetAllDescriptors().size() == NumTabletServers();
    }, 10s, "Wait all TServers to be registered"));
  }

  ASSERT_OK(tools::RunBackupCommand(
      pg_host_port(), cluster_->GetMasterAddresses(), cluster_->GetTserverHTTPAddresses(),
      *tmp_dir, {"--backup_location", tmp_dir / "backup", "--no_upload", "--keyspace", "ysql.test",
       "create"}));
}

// Use special mode when non leader master times out all rpcs.
// Then step down master leader and perform backup.
TEST_F(PgMasterFailoverTest, YB_DISABLE_TEST_IN_SANITIZERS(NonRespondingMaster)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
  TestNonRespondingMaster(WaitForTS::kFalse);
}

TEST_F(PgMasterFailoverTest, YB_DISABLE_TEST_IN_SANITIZERS(NonRespondingMasterWithTSWaiting)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
  TestNonRespondingMaster(WaitForTS::kTrue);
}

}  // namespace yb::pgwrapper

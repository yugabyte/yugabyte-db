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

#include <gmock/gmock.h>

#include "yb/master/master_backup.pb.h"
#include "yb/tools/yb-admin_client.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/pb_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

using namespace std::literals;

DECLARE_bool(TEST_hang_on_namespace_transition);

namespace yb {
namespace tools {

// Tests for the client. Used to verify behaviour that cannot be verified by using yb-admin as an
// external process.
class ClusterAdminClientTest : public pgwrapper::PgCommandTestBase {
 public:
  std::unique_ptr<ClusterAdminClient> cluster_admin_client_;

 protected:
  ClusterAdminClientTest() : pgwrapper::PgCommandTestBase(false, false) {}

  void SetUp() override {
    pgwrapper::PgCommandTestBase::SetUp();
    ASSERT_OK(CreateClient());
    cluster_admin_client_ = std::make_unique<ClusterAdminClient>(
        cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(60));
    ASSERT_OK(cluster_admin_client_->Init());
  }

  Result<pgwrapper::PGConn> PgConnect(const std::string& db_name = std::string()) {
    auto* ts = cluster_->tablet_server(
        RandomUniformInt<size_t>(0, cluster_->num_tablet_servers() - 1));
    return pgwrapper::PGConnBuilder({
      .host = ts->bind_host(),
      .port = ts->pgsql_rpc_port(),
      .dbname = db_name
    }).Connect();
  }
};

TEST_F(ClusterAdminClientTest, YB_DISABLE_TEST_IN_SANITIZERS(ListSnapshotsWithDetails)) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE test_table (k INT PRIMARY KEY, v TEXT)"));
  const TypedNamespaceName database {
    .db_type = YQL_DATABASE_PGSQL,
    .name = "yugabyte"};
  ASSERT_OK(cluster_admin_client_->CreateNamespaceSnapshot(
      database, 0 /* retention_duration_hours */));
  EnumBitSet<ListSnapshotsFlag> flags;
  flags.Set(ListSnapshotsFlag::SHOW_DETAILS);
  auto resp = ASSERT_RESULT(cluster_admin_client_->ListSnapshots(flags));
  EXPECT_EQ(resp.snapshots_size(), 1);
  std::unordered_set<master::SysRowEntryType> expected_types = {
      master::SysRowEntryType::NAMESPACE, master::SysRowEntryType::TABLE};
  std::unordered_set<master::SysRowEntryType> missing_types = {
      master::SysRowEntryType::NAMESPACE, master::SysRowEntryType::TABLE};
  for (const auto& entry : resp.snapshots(0).entry().entries()) {
    EXPECT_THAT(expected_types, testing::Contains(entry.type()));
    missing_types.erase(entry.type());
    switch (entry.type()) {
      case master::SysRowEntryType::NAMESPACE: {
        auto meta =
            ASSERT_RESULT(pb_util::ParseFromSlice<master::SysNamespaceEntryPB>(entry.data()));
        EXPECT_EQ(meta.name(), "yugabyte");
        EXPECT_EQ(meta.database_type(), YQL_DATABASE_PGSQL);
        EXPECT_EQ(
            meta.state(), master::SysNamespaceEntryPB_State::SysNamespaceEntryPB_State_RUNNING);
        break;
      }
      case master::SysRowEntryType::TABLE: {
        auto meta = ASSERT_RESULT(pb_util::ParseFromSlice<master::SysTablesEntryPB>(entry.data()));
        EXPECT_EQ(meta.name(), "test_table");
        EXPECT_EQ(meta.table_type(), yb::TableType::PGSQL_TABLE_TYPE);
        EXPECT_EQ(meta.namespace_name(), "yugabyte");
        EXPECT_EQ(meta.state(), master::SysTablesEntryPB_State::SysTablesEntryPB_State_RUNNING);
        break;
      }
      default:
        break;
    }
  }
  EXPECT_THAT(missing_types, testing::IsEmpty());
}

TEST_F(ClusterAdminClientTest, YB_DISABLE_TEST_IN_SANITIZERS(ListSnapshotsWithoutDetails)) {
  CreateTable("CREATE TABLE test_table (k INT PRIMARY KEY, v TEXT)");
  const TypedNamespaceName database {
    .db_type = YQL_DATABASE_PGSQL,
    .name = "yugabyte"
  };
  ASSERT_OK(cluster_admin_client_->CreateNamespaceSnapshot(
      database, 0 /* retention_duration_hours */));
  auto resp = ASSERT_RESULT(cluster_admin_client_->ListSnapshots(EnumBitSet<ListSnapshotsFlag>()));
  EXPECT_EQ(resp.snapshots_size(), 1);
  EXPECT_EQ(resp.snapshots(0).entry().entries_size(), 0);
}

TEST_F(
    ClusterAdminClientTest,
    CreateSnapshotAfterMultiNamespaceCreateIssuedWithSameName) {
  std::string db_name = "test_pgsql";
  TestThreadHolder threads;
  SetAtomicFlag(true, &FLAGS_TEST_hang_on_namespace_transition);

  auto create_database = [this, &db_name] {
    auto conn = ASSERT_RESULT(PgConnect());
    auto res = conn.ExecuteFormat("CREATE DATABASE $0", db_name);
    if (!res.ok()) {
      ASSERT_STR_CONTAINS(res.ToString(), Format("Keyspace '$0' already exists", db_name));
    }
  };

  threads.AddThreadFunctor(create_database);
  threads.AddThreadFunctor(create_database);
  threads.Stop();

  SetAtomicFlag(false, &FLAGS_TEST_hang_on_namespace_transition);

  ASSERT_OK(WaitFor([this, &db_name]() -> Result<bool> {
    bool create_in_progress = true;
    RETURN_NOT_OK(client_->IsCreateNamespaceInProgress(
        db_name, YQL_DATABASE_PGSQL, "" /* namespace_id */, &create_in_progress));

    return !create_in_progress;
  }, 15s, "Wait for create namespace to finish async setup tasks."));

  auto conn = ASSERT_RESULT(PgConnect(db_name));
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));
  const TypedNamespaceName database {
    .db_type = YQL_DATABASE_PGSQL,
    .name = db_name
  };
  ASSERT_OK(cluster_admin_client_->CreateNamespaceSnapshot(
      database, 0 /* retention_duration_hours */));
}

}  // namespace tools
}  // namespace yb

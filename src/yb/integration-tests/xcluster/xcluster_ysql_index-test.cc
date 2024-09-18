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

#include "yb/cdc/cdc_state_table.h"
#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"

#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/flags.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

DECLARE_string(vmodule);
DECLARE_bool(TEST_disable_apply_committed_transactions);
DECLARE_bool(TEST_xcluster_fail_table_create_during_bootstrap);
DECLARE_int32(TEST_user_ddl_operation_timeout_sec);
DECLARE_bool(TEST_fail_universe_replication_merge);

using std::string;
using namespace std::chrono_literals;

namespace yb {

const string kTableName = "test_table";
const string kIndexName = "test_index";
const auto kInsertStmtFormat = Format("INSERT INTO $0 VALUES($1, $1)", kTableName, "$0");
const auto kDropIndexStmt = Format("DROP INDEX $0", kIndexName);
const auto kId1CountStmt = Format("SELECT COUNT(*) FROM $0 WHERE id1 >= 0", kTableName);
const auto kId2CountStmt = Format("SELECT COUNT(*) FROM $0 WHERE id2 >= 0", kTableName);
const auto kSelectAllId12Stmt = Format("SELECT id1, id2 FROM $0 ORDER BY id1, id2", kTableName);

class XClusterYsqlIndexTest : public XClusterYsqlTestBase {
 public:
  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();
    XClusterYsqlTestBase::SetUp();
    google::SetVLOGLevel("backfill_index*", 4);
    google::SetVLOGLevel("xrepl*", 4);
    google::SetVLOGLevel("xcluster*", 4);
    google::SetVLOGLevel("add_table*", 4);
    google::SetVLOGLevel("multi_step*", 4);
    google::SetVLOGLevel("catalog*", 4);

    ASSERT_OK(Initialize(3 /* replication_factor */));

    ASSERT_OK(CreateObjects());

    producer_master_ = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->master();

    yb_table_name_ = ASSERT_RESULT(
        GetYsqlTable(&producer_cluster_, namespace_name, "" /* schema_name */, kTableName));

    client::YBTablePtr producer_table;
    ASSERT_OK(producer_client()->OpenTable(yb_table_name_, &producer_table));
    namespace_id_ = producer_table->name().namespace_id();
    producer_tables_.push_back(std::move(producer_table));

    ASSERT_OK(SetupUniverseReplication(
        producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
        ASSERT_RESULT(GetReplicationTableIds()), {} /*bootstrap_ids*/,
        {LeaderOnly::kTrue, IsTransactional()}));

    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(kReplicationGroupId, &resp));
    if (IsTransactional()) {
      ASSERT_OK(WaitForValidSafeTimeOnAllTServers(namespace_id_));
    }

    producer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));
    for (; row_count_ < 10; row_count_++) {
      ASSERT_OK(producer_conn_->ExecuteFormat(kInsertStmtFormat, row_count_));
    }

    if (IsTransactional()) {
      ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    } else {
      ASSERT_OK(WaitForRowCount(yb_table_name_, row_count_, &consumer_cluster_));
    }

    consumer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));
    // auto r = ASSERT_RESULT(consumer_conn_->HasIndexScan(kId1CountStmt));
    // ASSERT_FALSE(r);
    ASSERT_FALSE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId2CountStmt)));

    ASSERT_OK(ValidateRows());
  }

  Status CreateTable(Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    return conn.ExecuteFormat("CREATE TABLE $0(id1 INT PRIMARY KEY, id2 INT);", kTableName);
  }

  virtual Status CreateObjects() {
    return RunOnBothClusters([&](Cluster* cluster) { return CreateTable(cluster); });
  }

  virtual Transactional IsTransactional() { return Transactional::kTrue; }

  virtual Result<std::vector<TableId>> GetReplicationTableIds() {
    std::vector<TableId> result;
    for (const auto& table : producer_tables_) {
      result.push_back(table->id());
    }
    return result;
  }

  virtual Status CreateIndex(pgwrapper::PGConn& conn) {
    return conn.Execute(Format("CREATE INDEX $0 ON $1 (id2 ASC)", kIndexName, kTableName));
  }

  auto GetAllRows(pgwrapper::PGConn* conn) {
    return conn->FetchRows<int32_t, int32_t>(kSelectAllId12Stmt);
  }

  Status ValidateRows() {
    // With should be less than or equal to row_count_ since some inserts may fail due to
    // transactions aborted by the DDLs.
    const auto all_prod_rows = VERIFY_RESULT(GetAllRows(producer_conn_.get()));
    SCHECK_LE(all_prod_rows.size(), row_count_, IllegalState, "Producer row count mismatch.");
    const auto actual_count = all_prod_rows.size();

    const auto all_cons_rows = VERIFY_RESULT(GetAllRows(consumer_conn_.get()));
    SCHECK_EQ(
        all_prod_rows, all_cons_rows, IllegalState, "Producer and consumer have different rows.");

    const auto producer_count1 =
        VERIFY_RESULT(producer_conn_->FetchRow<pgwrapper::PGUint64>(kId1CountStmt));
    SCHECK_EQ(producer_count1, actual_count, IllegalState, "Id1 count mismatch in producer");
    const auto producer_count2 =
        VERIFY_RESULT(producer_conn_->FetchRow<pgwrapper::PGUint64>(kId2CountStmt));
    SCHECK_EQ(producer_count2, actual_count, IllegalState, "Id2 count mismatch in producer");

    const auto consumer_count1 =
        VERIFY_RESULT(consumer_conn_->FetchRow<pgwrapper::PGUint64>(kId1CountStmt));
    SCHECK_EQ(consumer_count1, actual_count, IllegalState, "Id1 count mismatch in consumer");
    const auto consumer_count2 =
        VERIFY_RESULT(consumer_conn_->FetchRow<pgwrapper::PGUint64>(kId2CountStmt));
    SCHECK_EQ(consumer_count2, actual_count, IllegalState, "Id2 count mismatch in consumer");

    return Status::OK();
  }

  // Get row count using indexed table and index when available. Both counts should match.
  // Row count should never move backwards.
  void ValidateRowsDuringCreateIndex(uint64_t initial_count, std::atomic_bool* stop) {
    auto min_count = initial_count;
    auto consumer_conn = CHECK_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    auto row_counts_getter = [&consumer_conn]() -> Result<std::pair<uint64_t, uint64_t>> {
      // Intentionally fetch id1 count first.
      const auto count1 = VERIFY_RESULT(consumer_conn.FetchRow<pgwrapper::PGUint64>(kId1CountStmt));
      const auto count2 = VERIFY_RESULT(consumer_conn.FetchRow<pgwrapper::PGUint64>(kId2CountStmt));
      return std::make_pair(count1, count2);
    };

    while (!*stop) {
      auto result = row_counts_getter();
      if (!result.ok()) {
        // Failure expected from the index create DDL. DDL version is bumped and propagated to pg
        // clients asynchronously leading to transient errors.
        CHECK(result.status().message().Contains("schema version mismatch"));
        continue;
      }
      auto [id1_count, id2_count] = *result;

      CHECK_GE(id2_count, min_count)
          << "Id2 count reduced." << ToString(CHECK_RESULT(GetAllRows(consumer_conn_.get())));

      // id1_count should be less than or equal to id2_count because it was fetched first and the
      // two statements are run in different transactions.
      CHECK_LE(id1_count, id2_count) << "Id1 count should be <= id2 row count."
                                     << ToString(CHECK_RESULT(GetAllRows(consumer_conn_.get())));

      min_count = id2_count;

      SleepFor(kTimeMultiplier * 100ms);
    }
  }

  Status TestCreateIndexConcurrentWorkload() {
    RETURN_NOT_OK(CreateIndex(*producer_conn_));
    SCHECK(
        VERIFY_RESULT(producer_conn_->HasIndexScan(kId2CountStmt)), IllegalState,
        "Index scan should be present on Producer col id2.");

    {
      auto test_thread_holder = TestThreadHolder();
      std::atomic_bool stop_threads(false);
      auto se = ScopeExit([&] {
        stop_threads = true;
        test_thread_holder.JoinAll();
      });

      // Insert workload.
      test_thread_holder.AddThread([&]() {
        while (!stop_threads) {
          LOG(INFO) << "Inserting row: " << row_count_;
          auto s = producer_conn_->ExecuteFormat(kInsertStmtFormat, row_count_);
          if (!s.ok()) {
            // Transactions are killed during bootstrap so these are expected.
            LOG(INFO) << "Insert " << row_count_ << " failed: " << s;
          }
          row_count_++;
        }
      });

      test_thread_holder.AddThread(std::bind(
          &XClusterYsqlIndexTest::ValidateRowsDuringCreateIndex, this, 10, &stop_threads));

      // Wait for threads to start.
      SleepFor(3s * kTimeMultiplier);

      RETURN_NOT_OK(CreateIndex(*consumer_conn_));

      // Keep running for a while with the index.
      SleepFor(3s * kTimeMultiplier);
    }

    SCHECK(
        VERIFY_RESULT(consumer_conn_->HasIndexScan(kId2CountStmt)), IllegalState,
        "Index scan should be present on Consumer col id2.");

    RETURN_NOT_OK(WaitForSafeTimeToAdvanceToNow());

    RETURN_NOT_OK(ValidateRows());

    for (int i = 0; i < 20; row_count_++, i++) {
      RETURN_NOT_OK(producer_conn_->ExecuteFormat(kInsertStmtFormat, row_count_));
    }

    RETURN_NOT_OK(WaitForSafeTimeToAdvanceToNow());
    return ValidateRows();
  }

  master::Master* producer_master_ = nullptr;
  client::YBTableName yb_table_name_;
  NamespaceId namespace_id_;
  std::unique_ptr<pgwrapper::PGConn> producer_conn_, consumer_conn_;
  uint row_count_ = 0;
};

TEST_F(XClusterYsqlIndexTest, CreateIndex) {
  // Create index on producer.
  ASSERT_OK(CreateIndex(*producer_conn_));
  ASSERT_FALSE(ASSERT_RESULT(producer_conn_->HasIndexScan(kId1CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(producer_conn_->HasIndexScan(kId2CountStmt)));

  {
    auto test_thread_holder = TestThreadHolder();
    std::atomic_bool stop_threads(false);
    auto se = ScopeExit([&] {
      stop_threads = true;
      test_thread_holder.JoinAll();
    });

    test_thread_holder.AddThread(
        std::bind(&XClusterYsqlIndexTest::ValidateRowsDuringCreateIndex, this, 10, &stop_threads));

    // Wait for thread to start.
    SleepFor(3s * kTimeMultiplier);

    // Create index on consumer.
    ASSERT_OK(CreateIndex(*consumer_conn_));
  }
  ASSERT_FALSE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId1CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId2CountStmt)));

  ASSERT_OK(ValidateRows());

  for (; row_count_ < 20; row_count_++) {
    ASSERT_OK(producer_conn_->ExecuteFormat(kInsertStmtFormat, row_count_));
  }

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  ASSERT_OK(ValidateRows());
}

TEST_F(XClusterYsqlIndexTest, CreateIndexWithWorkload) {
  ASSERT_OK(TestCreateIndexConcurrentWorkload());
}

TEST_F(XClusterYsqlIndexTest, FailedCreateIndex) {
  // Create index on consumer before producer should fail.
  ASSERT_NOK_STR_CONTAINS(
      CreateIndex(*consumer_conn_), "Failed to bootstrap table on the source universe");

  ASSERT_OK(CreateIndex(*producer_conn_));

  // Create index while replication is paused should fail.
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, false));
  ASSERT_NOK_STR_CONTAINS(CreateIndex(*consumer_conn_), "is currently disabled");

  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, true));

  // Failure during bootstrap
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_fail_table_create_during_bootstrap) = true;
  ASSERT_NOK_STR_CONTAINS(
      CreateIndex(*consumer_conn_), "FLAGS_TEST_xcluster_fail_table_create_during_bootstrap");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_fail_table_create_during_bootstrap) = false;

  // Failure when adding table to the replication group
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_universe_replication_merge) = true;
  ASSERT_NOK_STR_CONTAINS(CreateIndex(*consumer_conn_), "TEST_fail_universe_replication_merge");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_universe_replication_merge) = false;

  for (int i = 0; i < 20; row_count_++, i++) {
    ASSERT_OK(producer_conn_->ExecuteFormat(kInsertStmtFormat, row_count_));
  }

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(ValidateRows());

  ASSERT_OK(CreateIndex(*consumer_conn_));

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(ValidateRows());
}

TEST_F(XClusterYsqlIndexTest, MasterFailoverRetryAddTableToXcluster) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_user_ddl_operation_timeout_sec) = NonTsanVsTsan(60, 90);

  ASSERT_OK(CreateIndex(*producer_conn_));

  SyncPoint::GetInstance()->LoadDependency(
      {{"AddTableToXClusterTargetTask::RunInternal::BeforeBootstrap",
        "MasterFailoverRetryAddTableToXcluster::BeforeStepDown"}});

  SyncPoint::GetInstance()->SetCallBack(
      "AddTableToXClusterTargetTask::RunInternal::BeforeBootstrap",
      [](void* stuck_add_table_to_xcluster) {
        *(reinterpret_cast<bool*>(stuck_add_table_to_xcluster)) = true;
      });
  SyncPoint::GetInstance()->EnableProcessing();

  auto test_thread_holder = TestThreadHolder();
  Status status;
  test_thread_holder.AddThread([this, &status]() {
    // Create index on consumer.
    status = CreateIndex(*consumer_conn_);
  });

  // Wait for the task to start and get stuck.
  TEST_SYNC_POINT("MasterFailoverRetryAddTableToXcluster::BeforeStepDown");
  SyncPoint::GetInstance()->ClearAllCallBacks();

  auto master_leader = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());

  ASSERT_OK(StepDown(
      master_leader->tablet_peer(), std::string() /* new_leader_uuid */, ForceStepDown::kTrue));

  test_thread_holder.JoinAll();
  ASSERT_OK(status);

  ASSERT_OK(ValidateRows());
}

class XClusterYsqlNonTransactionalTest : public XClusterYsqlIndexTest {
 public:
  Transactional IsTransactional() override { return Transactional::kFalse; }
};

TEST_F(XClusterYsqlNonTransactionalTest, CreateIndex) {
  // Create index on producer.
  ASSERT_OK(CreateIndex(*producer_conn_));
  ASSERT_FALSE(ASSERT_RESULT(producer_conn_->HasIndexScan(kId1CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(producer_conn_->HasIndexScan(kId2CountStmt)));

  // Create index on consumer.
  ASSERT_OK(CreateIndex(*consumer_conn_));
  ASSERT_FALSE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId1CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId2CountStmt)));

  ASSERT_OK(ValidateRows());

  auto index_table_name = ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, "" /* schema_name */, kIndexName));

  client::YBTablePtr producer_index_table;
  ASSERT_OK(producer_client()->OpenTable(index_table_name, &producer_index_table));
  ASSERT_OK(
      AlterUniverseReplication(kReplicationGroupId, {producer_index_table}, true /* add_tables */));
  producer_tables_.push_back(std::move(producer_index_table));

  for (int i = 0; i < 10; i++, row_count_++) {
    ASSERT_OK(producer_conn_->ExecuteFormat(kInsertStmtFormat, row_count_));
  }

  // Wait for all tablets to catchup
  ASSERT_OK(WaitForReplicationDrain());
  ASSERT_OK(ValidateRows());
}

class XClusterColocatedIndexTest : public XClusterYsqlIndexTest {
 public:
  virtual Status CreateObjects() override {
    namespace_name = "colocated_db";

    return RunOnBothClusters([&](Cluster* cluster) {
      constexpr int colocation_id = 111111;
      RETURN_NOT_OK(CreateDatabase(cluster, namespace_name, /* colocated = */ true));
      auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
      return conn.ExecuteFormat(
          "CREATE TABLE $0(id1 INT PRIMARY KEY, id2 INT) WITH (colocation_id = $1);", kTableName,
          colocation_id);
    });
  }

  Result<std::vector<TableId>> GetReplicationTableIds() override {
    return std::vector<TableId>{VERIFY_RESULT(GetColocatedDatabaseParentTableId())};
  }

  Status CreateIndex(pgwrapper::PGConn& conn) override {
    return conn.Execute(Format(
        "CREATE INDEX $0 ON $1 (id2 ASC) WITH(colocation_id =111112)", kIndexName, kTableName));
  }
};

TEST_F(XClusterColocatedIndexTest, CreateIndexWithWorkload) {
  ASSERT_OK(TestCreateIndexConcurrentWorkload());
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(kReplicationGroupId, &resp));
  ASSERT_FALSE(resp.has_error());

  // We should only have 1 stream
  ASSERT_EQ(resp.entry().table_streams_size(), 1);
}

class XClusterColocatedNonTransactionalIndexTest : public XClusterColocatedIndexTest {
 public:
 public:
  Transactional IsTransactional() override { return Transactional::kFalse; }

  Status WaitForSafeTimeToAdvanceToNow() override {
    // There is no SafeTime in Non-transactional xCluster so instead wait for replication drain.
    return WaitForReplicationDrain(
        /* expected_num_nondrained */ 0, /* timeout_secs */ kRpcTimeout,
        /* target_time */ std::nullopt, VERIFY_RESULT(GetReplicationTableIds()));
  }
};

TEST_F(XClusterColocatedNonTransactionalIndexTest, CreateIndexWithWorkload) {
  ASSERT_OK(TestCreateIndexConcurrentWorkload());
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(kReplicationGroupId, &resp));
  ASSERT_FALSE(resp.has_error());

  // We should only have 1 stream
  ASSERT_EQ(resp.entry().table_streams_size(), 1);
}

class XClusterDbScopedYsqlIndexTest : public XClusterYsqlIndexTest {
  Status SetupUniverseReplication(
      MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::vector<TableId>& producer_table_ids,
      const std::vector<xrepl::StreamId>& bootstrap_ids, SetupReplicationOptions opts) override {
    RETURN_NOT_OK(CheckpointReplicationGroup());
    RETURN_NOT_OK(CreateReplicationFromCheckpoint());
    return Status::OK();
  }
};

TEST_F(XClusterDbScopedYsqlIndexTest, CreateIndex) {
  ASSERT_OK(CreateIndex(*producer_conn_));
  ASSERT_FALSE(ASSERT_RESULT(producer_conn_->HasIndexScan(kId1CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(producer_conn_->HasIndexScan(kId2CountStmt)));

  ASSERT_OK(ValidateRows());

  ASSERT_OK(CreateIndex(*consumer_conn_));
  ASSERT_FALSE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId1CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId2CountStmt)));

  ASSERT_OK(ValidateRows());

  // Insert more rows and validate.
  for (int i = 0; i < 10; i++, row_count_++) {
    ASSERT_OK(producer_conn_->ExecuteFormat(kInsertStmtFormat, row_count_));
  }

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(ValidateRows());
}

TEST_F(XClusterDbScopedYsqlIndexTest, CreateIndexWithWorkload) {
  ASSERT_OK(TestCreateIndexConcurrentWorkload());
}

class XClusterYsqlIndexProducerOnlyTest : public XClusterYsqlIndexTest {
  void SetUp() override {
    XClusterYsqlTestBase::SetUp();
    google::SetVLOGLevel("backfill_index*", 4);
    google::SetVLOGLevel("xrepl*", 4);
    google::SetVLOGLevel("xcluster*", 4);
    google::SetVLOGLevel("add_table*", 4);
    google::SetVLOGLevel("multi_step*", 4);
    google::SetVLOGLevel("catalog*", 4);

    XClusterYsqlTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    opts.num_masters = 1;
    ASSERT_OK(InitProducerClusterOnly(opts));

    ASSERT_OK(CreateTable(&producer_cluster_));

    producer_master_ = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->master();

    yb_table_name_ = ASSERT_RESULT(
        GetYsqlTable(&producer_cluster_, namespace_name, "" /* schema_name */, kTableName));

    client::YBTablePtr producer_table;
    ASSERT_OK(producer_client()->OpenTable(yb_table_name_, &producer_table));
    namespace_id_ = producer_table->name().namespace_id();
    producer_tables_.push_back(std::move(producer_table));
    producer_table_ = producer_tables_.front();

    producer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));
  }
};

// Make sure indexes are checkpointed to the End of WAL.
TEST_F_EX(
    XClusterDbScopedYsqlIndexTest, IndexCheckpointLocation, XClusterYsqlIndexProducerOnlyTest) {
  ASSERT_OK(CheckpointReplicationGroup());

  for (; row_count_ < 100; row_count_++) {
    ASSERT_OK(producer_conn_->ExecuteFormat(kInsertStmtFormat, row_count_));
  }

  ASSERT_OK(CreateIndex(*producer_conn_));
  auto index_table_name = ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, "" /* schema_name */, kIndexName));
  client::YBTablePtr index_table;
  ASSERT_OK(producer_client()->OpenTable(index_table_name, &index_table));

  auto stream_id = ASSERT_RESULT(GetCDCStreamID(index_table->id()));

  std::vector<TabletId> tablet_ids;
  ASSERT_OK(producer_cluster_.client_->GetTablets(index_table_name, (int32_t)1, &tablet_ids, NULL));
  ASSERT_EQ(tablet_ids.size(), 1);

  cdc::CDCStateTable cdc_state_table(producer_client());
  LOG(INFO) << "Fetching CDC state for tablet " << tablet_ids.front() << " and stream "
            << stream_id;
  auto key = cdc::CDCStateTableKey(tablet_ids.front(), stream_id);
  auto cdc_row = ASSERT_RESULT(
      cdc_state_table.TryFetchEntry(key, cdc::CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(cdc_row.has_value());

  ASSERT_GT(cdc_row->checkpoint->index, OpId().Min().index);
}

}  // namespace yb

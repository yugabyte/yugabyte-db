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

using std::string;
using namespace std::chrono_literals;

namespace yb {
using OK = Status::OK;

const string kTableName = "test_table";
const string kIndexName = "test_index";
const auto kInsertStmtFormat = Format("INSERT INTO $0 VALUES($1, $1)", kTableName, "$0");
const auto kCreateIndexStmt = Format("CREATE INDEX $0 ON $1 (id2 ASC)", kIndexName, kTableName);
const auto kDropIndexStmt = Format("DROP INDEX $0", kIndexName);
const auto kId1CountStmt = Format("SELECT COUNT(*) FROM $0 WHERE id1 >= 0", kTableName);
const auto kId2CountStmt = Format("SELECT COUNT(*) FROM $0 WHERE id2 >= 0", kTableName);
const auto kSelectAllId1Stmt = Format("SELECT id1 FROM $0 ORDER BY id1", kTableName);
const auto kSelectAllId2Stmt = Format("SELECT id2 FROM $0 ORDER BY id2", kTableName);

class XClusterYsqlIndexTest : public XClusterYsqlTestBase {
 public:
  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();
    XClusterYsqlTestBase::SetUp();
    ASSERT_OK(SET_FLAG(vmodule, "backfill_index*=4,xrepl*=4,xcluster*=4,add_table*=4,catalog*=4"));

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_user_ddl_operation_timeout_sec) = NonTsanVsTsan(60, 90);

    ASSERT_OK(Initialize(3 /* replication_factor */));

    ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) {
      auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
      return conn.ExecuteFormat("CREATE TABLE $0(id1 INT PRIMARY KEY, id2 INT);", kTableName);
    }));

    producer_master_ = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->master();

    auto yb_table_name = ASSERT_RESULT(
        GetYsqlTable(&producer_cluster_, namespace_name, "" /* schema_name */, kTableName));

    client::YBTablePtr producer_table;
    ASSERT_OK(producer_client()->OpenTable(yb_table_name, &producer_table));
    namespace_id_ = producer_table->name().namespace_id();

    ASSERT_OK(SetupUniverseReplication(
        kReplicationGroupId, {producer_table}, {LeaderOnly::kTrue, Transactional::kTrue}));
    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(kReplicationGroupId, &resp));
    ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));
    ASSERT_OK(WaitForValidSafeTimeOnAllTServers(namespace_id_));

    producer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));
    for (; row_count_ < 10; row_count_++) {
      ASSERT_OK(producer_conn_->ExecuteFormat(kInsertStmtFormat, row_count_));
    }

    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

    consumer_conn_ = std::make_unique<pgwrapper::PGConn>(
        ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));
    ASSERT_FALSE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId1CountStmt)));
    ASSERT_FALSE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId2CountStmt)));

    ASSERT_OK(ValidateRows());
  }

  Status WaitForSafeTimeToAdvanceToNow() {
    HybridTime now = producer_master_->clock()->Now();
    for (auto ts : producer_cluster()->mini_tablet_servers()) {
      now.MakeAtLeast(ts->server()->clock()->Now());
    }
    return WaitForSafeTime(namespace_id_, now);
  }

  template <class T>
  pgwrapper::GetValueResult<T> FetchValue(const std::string& command, pgwrapper::PGConn* conn) {
    auto res = VERIFY_RESULT(conn->Fetch(command));

    auto fetched_rows = PQntuples(res.get());
    auto fetched_columns = PQnfields(res.get());
    if (fetched_rows != 1 || fetched_columns != 1) {
      return STATUS_FORMAT(
          RuntimeError, "Fetched $0 rows and $1 columns, while only 1 expected", fetched_rows,
          fetched_columns);
    }

    return pgwrapper::GetValue<T>(res.get(), 0 /* row */, 0 /* column */);
  }

  Result<string> GetAllRows(pgwrapper::PGConn* conn) {
    return Format(
        "\nId1 rows: $0\nId2 rows: $1\n",
        VERIFY_RESULT(conn->FetchAllAsString(kSelectAllId1Stmt)),
        VERIFY_RESULT(conn->FetchAllAsString(kSelectAllId2Stmt)));
  }

  Status ValidateRows() {
    const auto producer_row_count =
        VERIFY_RESULT(FetchValue<int64_t>(kId1CountStmt, producer_conn_.get()));
    const auto id1_count = VERIFY_RESULT(FetchValue<int64_t>(kId1CountStmt, consumer_conn_.get()));
    const auto id2_count = VERIFY_RESULT(FetchValue<int64_t>(kId2CountStmt, consumer_conn_.get()));
    SCHECK_EQ(
        producer_row_count, id1_count, IllegalState,
        "Producer consumer row count mismatch.\nProducer: " +
            VERIFY_RESULT(GetAllRows(producer_conn_.get())) +
            "Consumer: " + VERIFY_RESULT(GetAllRows(consumer_conn_.get())));
    SCHECK_EQ(
        id1_count, id2_count, IllegalState,
        "Id1 id2 row count mismatch." + VERIFY_RESULT(GetAllRows(consumer_conn_.get())));

    SCHECK_EQ(
        VERIFY_RESULT(GetAllRows(producer_conn_.get())),
        VERIFY_RESULT(GetAllRows(consumer_conn_.get())), IllegalState,
        "Producer and consumer have different rows.");
    return OK();
  }

  // Get row count using indexed table and index when available. Both counts should match.
  // Row count should never move backwards.
  void ValidateRowsDuringCreateIndex(int initial_count, std::atomic_bool* stop) {
    int64_t min_count = initial_count;
    auto consumer_conn = CHECK_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    while (!*stop) {
      int64_t id1_count, id2_count;
      auto get_row_counts = [&]() -> Status {
        id1_count = VERIFY_RESULT(FetchValue<int64_t>(kId1CountStmt, &consumer_conn));
        id2_count = VERIFY_RESULT(FetchValue<int64_t>(kId2CountStmt, &consumer_conn));
        return OK();
      };
      auto s = get_row_counts();
      if (!s.ok()) {
        // Failure expected from the index create DDL. DDL version is bumped and propagated to pg
        // clients asynchronously leading to transient errors.
        CHECK(s.message().Contains("schema version mismatch"));
        continue;
      }

      CHECK_GE(id2_count, min_count)
          << "Id2 count reduced." << CHECK_RESULT(GetAllRows(consumer_conn_.get()));

      // id1_count should be less than or equal to id2_count because it was fetched first and the
      // two statements are run in different transactions.
      CHECK_LE(id1_count, id2_count) << "Id1 count should be <= id2 row count."
                                     << CHECK_RESULT(GetAllRows(consumer_conn_.get()));

      min_count = id2_count;

      SleepFor(kTimeMultiplier * 100ms);
    }
  }

  master::Master* producer_master_ = nullptr;
  NamespaceId namespace_id_;
  std::unique_ptr<pgwrapper::PGConn> producer_conn_, consumer_conn_;
  int row_count_ = 0;
};

TEST_F(XClusterYsqlIndexTest, CreateIndex) {
  // Create index on producer.
  ASSERT_OK(producer_conn_->Execute(kCreateIndexStmt));
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
    ASSERT_OK(consumer_conn_->Execute(kCreateIndexStmt));
  }
  ASSERT_FALSE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId1CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId2CountStmt)));

  ASSERT_OK(ValidateRows());
}

TEST_F(XClusterYsqlIndexTest, CreateIndexWithWorkload) {
  ASSERT_OK(producer_conn_->Execute(kCreateIndexStmt));
  ASSERT_FALSE(ASSERT_RESULT(producer_conn_->HasIndexScan(kId1CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(producer_conn_->HasIndexScan(kId2CountStmt)));

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

    test_thread_holder.AddThread(
        std::bind(&XClusterYsqlIndexTest::ValidateRowsDuringCreateIndex, this, 10, &stop_threads));

    // Wait for threads to start.
    SleepFor(3s * kTimeMultiplier);

    ASSERT_OK(consumer_conn_->Execute(kCreateIndexStmt));
  }

  ASSERT_FALSE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId1CountStmt)));
  ASSERT_TRUE(ASSERT_RESULT(consumer_conn_->HasIndexScan(kId2CountStmt)));

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(ValidateRows());
}

TEST_F(XClusterYsqlIndexTest, FailedCreateIndex) {
  // Create index on consumer before producer should fail.
  ASSERT_NOK(consumer_conn_->Execute(kCreateIndexStmt));

  ASSERT_OK(producer_conn_->Execute(kCreateIndexStmt));

  // Create index while replication is paused should fail.
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, false));
  ASSERT_NOK(consumer_conn_->Execute(kCreateIndexStmt));
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, true));

  // Failure during bootstrap
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_fail_table_create_during_bootstrap) = true;
  ASSERT_NOK(consumer_conn_->Execute(kCreateIndexStmt));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_fail_table_create_during_bootstrap) = false;

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(ValidateRows());

  ASSERT_OK(consumer_conn_->Execute(kCreateIndexStmt));

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(ValidateRows());
}

TEST_F(XClusterYsqlIndexTest, MasterFailoverRetryAddTableToXcluster) {
  ASSERT_OK(producer_conn_->Execute(kCreateIndexStmt));

  SyncPoint::GetInstance()->LoadDependency(
      {{"AddTableToXClusterTask::RunInternal::BeforeBootstrap",
        "MasterFailoverRetryAddTableToXcluster::BeforeStepDown"}});

  SyncPoint::GetInstance()->SetCallBack(
      "AddTableToXClusterTask::RunInternal::BeforeBootstrap",
      [](void* stuck_add_table_to_xcluster) {
        *(reinterpret_cast<bool*>(stuck_add_table_to_xcluster)) = true;
      });
  SyncPoint::GetInstance()->EnableProcessing();

  auto test_thread_holder = TestThreadHolder();
  Status status;
  test_thread_holder.AddThread([this, &status]() {
    // Create index on consumer.
    status = consumer_conn_->Execute(kCreateIndexStmt);
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

// TODO(Hari): #16758 Test collocated table

}  // namespace yb

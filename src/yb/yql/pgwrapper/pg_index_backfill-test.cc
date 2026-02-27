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

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "yb/client/client-test-util.h"
#include "yb/client/table_info.h"

#include "yb/common/schema.h"

#include "yb/integration-tests/backfill-test-util.h"
#include "yb/integration-tests/external_mini_cluster_validator.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_error.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using std::string;

using namespace std::chrono_literals;
using namespace std::literals;

namespace yb::pgwrapper {

namespace {

constexpr auto kColoDbName = "colodb";
const auto kDatabaseName = "yugabyte"s;
constexpr auto kIndexName = "iii";
constexpr auto kTableName = "ttt";
const auto kCommandConcurrently = "CREATE INDEX CONCURRENTLY"s;
const auto kCommandNonconcurrently = "CREATE INDEX NONCONCURRENTLY"s;
const auto kPhase = "phase"s;
const auto kPhaseBackfilling = "backfilling"s;
const auto kPhaseInitializing = "initializing"s;
const client::YBTableName kYBTableName(YQLDatabase::YQL_DATABASE_PGSQL, kDatabaseName, kTableName);

}  // namespace

YB_DEFINE_ENUM(IndexStateFlag, (kIndIsLive)(kIndIsReady)(kIndIsValid));
typedef EnumBitSet<IndexStateFlag> IndexStateFlags;

class PgIndexBackfillTest : public LibPqTestBase, public ::testing::WithParamInterface<bool> {
 public:
  void SetUp() override {
    LibPqTestBase::SetUp();
    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  }

  bool EnableTableLocks() const { return GetParam(); }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--ysql_disable_index_backfill=false");
    options->extra_master_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));
    options->extra_tserver_flags.push_back("--ysql_disable_index_backfill=false");
    options->extra_tserver_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));

    if (EnableTableLocks()) {
      options->extra_master_flags.push_back(
          "--allowed_preview_flags_csv=enable_object_locking_for_table_locks");
      options->extra_master_flags.push_back("--enable_object_locking_for_table_locks=true");
      options->extra_master_flags.push_back("--enable_ysql_operation_lease=true");

      options->extra_tserver_flags.push_back(
          "--allowed_preview_flags_csv=enable_object_locking_for_table_locks");
      options->extra_tserver_flags.push_back("--enable_object_locking_for_table_locks=true");
      options->extra_tserver_flags.push_back("--enable_ysql_operation_lease=true");
      options->extra_tserver_flags.push_back("--TEST_tserver_enable_ysql_lease_refresh=true");
    }
  }

  Status CheckIndexConsistency(const std::string& index_name) {
    LOG(INFO) << "Running index consistency checker for index: " << index_name;
    RETURN_NOT_OK(conn_->Fetch(Format("SELECT yb_index_check('$0'::regclass)", index_name)));
    LOG(INFO) << "Index consistency check successfully completed for index: " << index_name;
    return Status::OK();
  }

 protected:
  Result<bool> IsAtTargetIndexStateFlags(
      const std::string& index_name,
      const IndexStateFlags& target_index_state_flags) {
    Result<IndexStateFlags> res = GetIndexStateFlags(index_name);
    IndexStateFlags actual_index_state_flags;
    if (res.ok()) {
      actual_index_state_flags = res.get();
    } else if (res.status().IsNotFound()) {
      LOG(WARNING) << res.status();
      return false;
    } else {
      return res.status();
    }

    if (actual_index_state_flags < target_index_state_flags) {
      LOG(INFO) << index_name
                << " not yet at target index state flags "
                << ToString(target_index_state_flags);
      return false;
    } else if (actual_index_state_flags > target_index_state_flags) {
      return STATUS(RuntimeError,
                    Format("$0 exceeded target index state flags $1",
                           index_name,
                           target_index_state_flags));
    }
    return true;
  }

  Status WaitForIndexStateFlags(const IndexStateFlags& index_state_flags,
                                const std::string& index_name = kIndexName) {
    RETURN_NOT_OK(WaitFor(
        [this, &index_name, &index_state_flags] {
          return IsAtTargetIndexStateFlags(index_name, index_state_flags);
        },
        30s,
        Format("get index state flags: $0", index_state_flags)));
    return Status::OK();
  }

  template <class... Args>
  Status WaitForIndexProgressOutput(
      const std::string& columns, const std::tuple<Args...>& expected) {
    return WaitForIndexProgressOutputImpl<decltype(expected), Args...>(columns, expected);
  }

  template <class T>
  Status WaitForIndexProgressOutput(const std::string& columns, const T& expected) {
    return WaitForIndexProgressOutputImpl<T, T>(columns, expected);
  }

  Status WaitForIndexScan(const std::string& query) {
    return WaitFor(
        [this, &query] {
          return conn_->HasIndexScan(query);
        },
        30s,
        "Wait for IndexScan");
  }

  bool HasClientTimedOut(const Status& s);
  void TestSimpleBackfill(const std::string& table_create_suffix = "");
  void TestLargeBackfill(const int num_rows);
  void TestRetainDeleteMarkers(const std::string& db_name);
  void TestRetainDeleteMarkersRecovery(const std::string& db_name, bool use_multiple_requests);
  Status TestInsertsWhileCreatingIndex(bool expect_missing_row);

  const int kTabletsPerServer = RegularBuildVsSanitizers(8, 2);

  std::unique_ptr<PGConn> conn_;
  TestThreadHolder thread_holder_;

  std::string GenerateSplitClause(int num_rows, int num_tablets) {
    std::string split_clause = "SPLIT AT VALUES (";
    for (int i = 1; i < num_tablets; ++i) {
      if (i > 1) {
        split_clause += ", ";
      }
      split_clause += Format("($0)", i * num_rows / num_tablets + 1);
    }
    split_clause += ")";
    return split_clause;
  }

 private:
  Result<IndexStateFlags> GetIndexStateFlags(const std::string& index_name) {
    const std::string quoted_index_name = PqEscapeLiteral(index_name);

    PGResultPtr res = VERIFY_RESULT(conn_->FetchFormat(
        "SELECT indislive, indisready, indisvalid"
        " FROM pg_class INNER JOIN pg_index ON pg_class.oid = pg_index.indexrelid"
        " WHERE pg_class.relname = $0",
        quoted_index_name));
    if (PQntuples(res.get()) == 0) {
      return STATUS_FORMAT(NotFound, "$0 not found in pg_class and/or pg_index", quoted_index_name);
    }
    if (int num_cols = PQnfields(res.get()) != 3) {
      return STATUS_FORMAT(Corruption, "got unexpected number of columns: $0", num_cols);
    }

    IndexStateFlags index_state_flags;
    if (VERIFY_RESULT(GetValue<bool>(res.get(), 0, 0))) {
      index_state_flags.Set(IndexStateFlag::kIndIsLive);
    }
    if (VERIFY_RESULT(GetValue<bool>(res.get(), 0, 1))) {
      index_state_flags.Set(IndexStateFlag::kIndIsReady);
    }
    if (VERIFY_RESULT(GetValue<bool>(res.get(), 0, 2))) {
      index_state_flags.Set(IndexStateFlag::kIndIsValid);
    }

    return index_state_flags;
  }

  template <class T, class... Args>
  Status WaitForIndexProgressOutputImpl(const std::string& columns, const T& expected) {
    const auto query = Format("SELECT $0 FROM pg_stat_progress_create_index", columns);
    return WaitFor(
        [this, &query, &expected]() -> Result<bool> {
          auto values = VERIFY_RESULT(conn_->FetchRows<Args...>(query));
          if (values.size() == 0) {
            // Likely the index doesn't exist yet.
            return false;
          }
          SCHECK_EQ(values.size(), 1, IllegalState, "unexpected number of rows");
          return values[0] == expected;
        },
        30s * kTimeMultiplier,
        Format("Wait on index progress columns $0", columns));
  }

  class PgRetainDeleteMarkersValidator final : public itest::RetainDeleteMarkersValidator {
    using Base = itest::RetainDeleteMarkersValidator;

   public:
    PgRetainDeleteMarkersValidator(
        ExternalMiniCluster* cluster, client::YBClient* client,
        PGConn* conn, const std::string& db_name)
        : Base(cluster, client, db_name), conn_(*CHECK_NOTNULL(conn)) {
    }

   private:
    Status RestartCluster() override {
      RETURN_NOT_OK(Base::RestartCluster());
      conn_.Reset(); // Should be enough to restore connection after the cluster restart.
      return Status::OK();
    }

    Status CreateIndex(const std::string &index_name, const std::string &table_name) override {
      return conn_.ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", index_name, table_name);
    }

    Status CreateTable(const std::string &table_name) override {
      return conn_.ExecuteFormat("CREATE TABLE $0 (i int)", table_name);
    }

    PGConn& conn_;
  };
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillTest, ::testing::Bool());

namespace {

Result<int> TotalBackfillRpcMetric(ExternalMiniCluster* cluster, const char* type) {
  int total_rpc_calls = 0;
  constexpr auto metric_name = "handler_latency_yb_tserver_TabletServerAdminService_BackfillIndex";
  for (auto ts : cluster->tserver_daemons()) {
    auto val = VERIFY_RESULT(ts->GetMetric<int64>("server", "yb.tabletserver", metric_name, type));
    total_rpc_calls += val;
    VLOG(1) << ts->bind_host() << " for " << type << " returned " << val;
  }
  return total_rpc_calls;
}

Result<int> TotalBackfillRpcCalls(ExternalMiniCluster* cluster) {
  return TotalBackfillRpcMetric(cluster, "total_count");
}

Result<double> AvgBackfillRpcLatencyInMicros(ExternalMiniCluster* cluster) {
  auto num_calls = VERIFY_RESULT(TotalBackfillRpcMetric(cluster, "total_count"));
  double total_latency = VERIFY_RESULT(TotalBackfillRpcMetric(cluster, "total_sum"));
  return total_latency / num_calls;
}

} // namespace

bool PgIndexBackfillTest::HasClientTimedOut(const Status& s) {
  if (!s.IsNetworkError()) {
    return false;
  }

  // The client timeout is set using the same backfill_index_client_rpc_timeout_ms for
  // postgres-tserver RPC and tserver-master RPC.  Since they are the same value, it _may_ be
  // possible for either timeout message to show up, so accept either, even though the
  // postgres-tserver timeout is far more likely to show up.
  //
  // The first is postgres-tserver; the second is tserver-master.
  const std::string msg = s.message().ToBuffer();
  return msg.find("timed out after") != std::string::npos ||
         msg.find("Timed out waiting for Backfill Index") != std::string::npos;
}

void PgIndexBackfillTest::TestSimpleBackfill(const std::string& table_create_suffix) {
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (c CHAR, i INT, p POINT) $1", kTableName, table_create_suffix));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES ('a', 0, '(1, 2)'), ('y', -5, '(0, -2)'), ('b', 100, '(868, 9843)')",
      kTableName));
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 (c ASC)", kTableName));

  // Index scan to verify contents of index table.
  const auto query = Format("SELECT c, i FROM $0 ORDER BY c", kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));

  const auto rows = ASSERT_RESULT((conn_->FetchRows<char, int32_t>(query)));
  ASSERT_EQ(rows, (decltype(rows){{'a', 0}, {'b', 100}, {'y', -5}}));
}


// Checks that retain_delete_markers is false after index creation.
void PgIndexBackfillTest::TestRetainDeleteMarkers(const std::string& db_name) {
  ASSERT_OK(EnsureClientCreated());
  PgRetainDeleteMarkersValidator{ cluster_.get(), client_.get(), conn_.get(), db_name }.Test();
}

// Test that retain_delete_markers is recovered after not being properly set after index backfill.
void PgIndexBackfillTest::TestRetainDeleteMarkersRecovery(
    const std::string& db_name, bool use_multiple_requests) {
  ASSERT_OK(EnsureClientCreated());
  auto validator =
      PgRetainDeleteMarkersValidator{ cluster_.get(), client_.get(), conn_.get(), db_name };
  validator.TestRecovery(use_multiple_requests);
}

void PgIndexBackfillTest::TestLargeBackfill(const int num_rows) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  // Insert bunch of rows.
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))",
      kTableName,
      num_rows));

  // Create index.
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 (i ASC)", kTableName));

  // All rows should be in the index.
  const std::string query = Format(
      "SELECT COUNT(*) FROM $0 WHERE i > 0",
      kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
  auto actual_num_rows = ASSERT_RESULT(conn_->FetchRow<PGUint64>(query));
  ASSERT_EQ(actual_num_rows, num_rows);
}

// Make sure that backfill works.
TEST_P(PgIndexBackfillTest, Simple) {
  TestSimpleBackfill();
}

TEST_P(PgIndexBackfillTest, WaitForSplitsToComplete) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  constexpr int kTimeoutSec = 3;
  constexpr int kNumRows = 1000;
  // Use 1 tablet so we guarantee we have a middle key to split by.
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int) SPLIT INTO 1 TABLETS", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))", kTableName, kNumRows));

  const TabletId tablet_to_split = ASSERT_RESULT(GetSingleTabletId(kTableName));
  // Flush the data to generate SST files that can be split.
  const std::string table_id = ASSERT_RESULT(GetTableIdByTableName(
      client.get(), kDatabaseName, kTableName));
  ASSERT_OK(client->FlushTables(
      {table_id},
      false /* add_indexes */,
      kTimeoutSec,
      false /* is_compaction */));

  // Create a split that will not complete until we set the test flag to true.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_pause_tserver_get_split_key", "true"));
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>();
  master::SplitTabletRequestPB req;
  req.set_tablet_id(tablet_to_split);
  master::SplitTabletResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  rpc::RpcController controller;
  ASSERT_OK(proxy.SplitTablet(req, &resp, &controller));

  // The create index should fail while there is an ongoing split.
  auto status = conn_->ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName);
  ASSERT_TRUE(status.message().ToBuffer().find("failed") != std::string::npos);

  // Drop the index since we don't automatically clean it up.
  ASSERT_OK(conn_->ExecuteFormat("DROP INDEX $0", kIndexName));
  // Allow the split to complete. We intentionally do not wait for the split to complete before
  // trying to create the index again, to validate that in a normal case (in which we don't have
  // a split that is stuck), the timeout on FLAGS_index_backfill_tablet_split_completion_timeout_sec
  // is large enough to allow for splits to complete.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_pause_tserver_get_split_key", "false"));
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));
}

// Make sure that partial indexes work for index backfill.
TEST_P(PgIndexBackfillTest, Partial) {
  constexpr int kNumRows = 7;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(-1, -$1, -1))",
      kTableName,
      kNumRows));
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 (i ASC) WHERE j > -5", kTableName));

  // Index scan to verify contents of index table.
  {
    const auto query = Format("SELECT j FROM $0 WHERE j > -3 ORDER BY i", kTableName);
    ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
    const auto values = ASSERT_RESULT(conn_->FetchRows<int32_t>(query));
    ASSERT_EQ(values, (decltype(values){-1, -2}));
  }
  {
    const auto query = Format(
        "SELECT i FROM $0 WHERE j > -5 ORDER BY i DESC LIMIT 2",
        kTableName);
    ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
    const auto values = ASSERT_RESULT(conn_->FetchRows<int32_t>(query));
    ASSERT_EQ(values, (decltype(values){4, 3}));
  }
}

// Make sure that expression indexes work for index backfill.
TEST_P(PgIndexBackfillTest, Expression) {
  constexpr int kNumRows = 9;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 ((j % i))", kTableName));

  // Index scan to verify contents of index table.
  const std::string query = Format(
      "SELECT j, i, j % i as mod FROM $0 WHERE j % i = 2 ORDER BY i",
      kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
  const auto rows = ASSERT_RESULT((conn_->FetchRows<int32_t, int32_t, int32_t>(query)));
  ASSERT_EQ(rows, (decltype(rows){{14, 4, 2}, {18, 8, 2}}));
}

// Make sure that unique indexes work when index backfill is enabled.
TEST_P(PgIndexBackfillTest, Unique) {
  constexpr int kNumRows = 3;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));
  // Add row that would make j not unique.
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (99, 11)",
      kTableName,
      kNumRows));

  // Create unique index without failure.
  ASSERT_OK(conn_->ExecuteFormat("CREATE UNIQUE INDEX ON $0 (i ASC)", kTableName));
  // Index scan to verify contents of index table.
  const auto query = Format("SELECT * FROM $0 ORDER BY i", kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
  ASSERT_OK(conn_->FetchMatrix(query, 4, 2));

  // Create unique index with failure.
  auto status = conn_->ExecuteFormat("CREATE UNIQUE INDEX ON $0 (j ASC)", kTableName);
  ASSERT_NOK(status);
  const auto msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("duplicate key value violates unique constraint") != std::string::npos)
      << status;
}

// Make sure that indexes created in postgres nested DDL work and skip backfill (optimization).
TEST_P(PgIndexBackfillTest, NestedDdl) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  constexpr int kNumRows = 3;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, j int, UNIQUE (j))", kTableName));

  // Make sure that the index create was not multi-stage.
  const std::string table_id = ASSERT_RESULT(GetTableIdByTableName(
      client.get(), kDatabaseName, kTableName));
  std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
  Synchronizer sync;
  ASSERT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
  ASSERT_OK(sync.Wait());
  ASSERT_EQ(table_info->schema.version(), 1);

  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));

  // Add row that violates unique constraint on j.
  Status status = conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (99, 11)",
      kTableName,
      kNumRows);
  ASSERT_NOK(status);
  const std::string msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("duplicate key value") != std::string::npos) << status;
}

// Make sure that drop index works when index backfill is enabled (skips online schema migration for
// now)
TEST_P(PgIndexBackfillTest, Drop) {
  constexpr int kNumRows = 5;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));

  // Create index.
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));

  // Drop index.
  ASSERT_OK(conn_->ExecuteFormat("DROP INDEX $0", kIndexName));

  // Ensure index is not used for scan.
  const std::string query = Format(
      "SELECT * FROM $0 ORDER BY i",
      kTableName);
  ASSERT_FALSE(ASSERT_RESULT(conn_->HasIndexScan(query)));
}

// Make sure deletes to nonexistent rows look like noops to clients.  This may seem too obvious to
// necessitate a test, but logic for backfill is special in that it wants nonexistent index deletes
// to be applied for the backfill process to use them.  This test guards against that logic being
// implemented incorrectly.
TEST_P(PgIndexBackfillTest, NonexistentDelete) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int PRIMARY KEY)", kTableName));

  // Delete to nonexistent row should return no rows.
  const auto values = ASSERT_RESULT(conn_->FetchRows<int32_t>(
      Format("DELETE FROM $0 WHERE i = 1 RETURNING i", kTableName)));
  ASSERT_TRUE(values.empty());
}

// Make sure that index backfill on large tables backfills all data.
TEST_P(PgIndexBackfillTest, Large) {
  constexpr int kNumRows = 10000;
  TestLargeBackfill(kNumRows);
  auto expected_calls = cluster_->num_tablet_servers() * kTabletsPerServer;
  auto actual_calls = ASSERT_RESULT(TotalBackfillRpcCalls(cluster_.get()));
  ASSERT_GE(actual_calls, expected_calls);
}

// Cousin of TestIndexBackfill#insertsWhileCreatingIndex java test.
Status PgIndexBackfillTest::TestInsertsWhileCreatingIndex(bool expect_missing_row) {
  RETURN_NOT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  TestThreadHolder thread_holder;
  constexpr int kNumThreads = 4;
  std::array<int, kNumThreads> counts;
  counts.fill(0);
  CountDownLatch latch(kNumThreads);
  // TODO(jason): no longer expect schema version mismatch errors after closing issue #3979.
  const std::vector<std::string> allowed_msgs{
    "expired or aborted by a conflict",
    "Resource unavailable",
    "schema version mismatch",
    "Transaction aborted",
    "Transaction was recently aborted",
  };
  for (int thread_idx = 0; thread_idx < kNumThreads; thread_idx++) {
    thread_holder.AddThreadFunctor([this, thread_idx, &latch, &counts, &allowed_msgs,
                                    &stop = thread_holder.stop_flag()] {
          LOG(INFO) << "Begin writer thread " << thread_idx;
          auto conn = ASSERT_RESULT(Connect());
          latch.CountDown();
          while (!stop.load(std::memory_order_acquire)) {
            const int i = counts[thread_idx] * kNumThreads + thread_idx;
            Status s = conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, i);
            if (s.ok()) {
              counts[thread_idx]++;
            } else {
              // Ignore transient errors that likely occur when changing index permissions.
              ASSERT_TRUE(s.IsNetworkError()) << s;
              std::string msg = s.message().ToBuffer();
              ASSERT_TRUE(std::find_if(
                  std::begin(allowed_msgs),
                  std::end(allowed_msgs),
                  [&msg] (const std::string allowed_msg) {
                    return msg.find(allowed_msg) != std::string::npos;
                  }) != std::end(allowed_msgs))
                << s;
              LOG(INFO) << "transient error on i=" << i << ", msg: " << msg;
            }
          }
        });
  }

  latch.Wait();
  RETURN_NOT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 (i ASC)", kTableName));
  thread_holder.Stop();

  LOG(INFO) << "Check counts";
  const auto table_count = VERIFY_RESULT(conn_->FetchRow<PGUint64>(
      Format("SELECT count(*) FROM $0", kTableName)));
  const auto index_count = VERIFY_RESULT(conn_->FetchRow<PGUint64>(
      Format("WITH w AS (SELECT * FROM $0 ORDER BY i) SELECT count(*) FROM w", kTableName)));

  LOG(INFO) << "Table has " << table_count << " rows";
  LOG(INFO) << "Index has " << index_count << " rows";
  if (expect_missing_row) {
    SCHECK_NE(table_count, index_count, IllegalState, "row count should mismatch");
  } else {
    SCHECK_EQ(table_count, index_count, IllegalState, "row count should match");
  }

  LOG(INFO) << "Check individual elements";
  bool found_missing_row = false;
  for (int thread_idx = 0; thread_idx < kNumThreads; thread_idx++) {
    for (int n = 0; n < counts[thread_idx]; n++) {
      int i = n * kNumThreads + thread_idx;
      // Point index scan.
      auto values = VERIFY_RESULT(conn_->FetchRows<int32_t>(Format(
          "SELECT * FROM $0 WHERE i = $1", kTableName, i)));
      if (values.size() == 1) {
        SCHECK_EQ(values[0], i, IllegalState, "found corruption");
      } else {
        SCHECK_EQ(values.size(), 0, IllegalState, "unexpected number of rows");
        // Prefer LOG(ERROR) over ADD_FAILURE() since it fits in one line so is easier to read.
        LOG(ERROR) << "Index is missing element " << i;
        found_missing_row = true;
      }
    }
  }
  if (expect_missing_row && !found_missing_row) {
    return STATUS(IllegalState, "index should be missing a row");
  }
  return Status::OK();
}

class PgIndexBackfillTestEnableWait : public PgIndexBackfillTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.insert(
        options->extra_tserver_flags.end(),
        {
          "--ysql_yb_disable_wait_for_backends_catalog_version=false",
          "--ysql_yb_index_state_flags_update_delay=0",
        });
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillTestEnableWait, ::testing::Bool());

TEST_P(PgIndexBackfillTestEnableWait, InsertsWhileCreatingIndexEnableWait) {
  ASSERT_OK(TestInsertsWhileCreatingIndex(false /* expect_missing_row */));
}

class PgIndexBackfillTestDisableWait : public PgIndexBackfillTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.insert(
        options->extra_tserver_flags.end(),
        {
          "--ysql_yb_disable_wait_for_backends_catalog_version=true",
          "--ysql_yb_index_state_flags_update_delay=0",
        });
  }
};

// TODO(bkolagani): Rework the tests for table-locks enabled case. GHI#26795
INSTANTIATE_TEST_CASE_P(, PgIndexBackfillTestDisableWait, ::testing::Values(false));

TEST_P(PgIndexBackfillTestDisableWait,
       YB_DISABLE_TEST_IN_TSAN(InsertsWhileCreatingIndexDisableWait)) {
  constexpr auto kNumTries = 5;

  for (int i = 0; i < kNumTries; ++i) {
    Status s = TestInsertsWhileCreatingIndex(true /* expect_missing_row */);
    if (s.ok()) {
      return;
    }
    ASSERT_TRUE(s.IsIllegalState()) << s;
    ASSERT_STR_CONTAINS(s.message().ToBuffer(), "row count should mismatch");
    ASSERT_OK(conn_->ExecuteFormat("DROP TABLE $0", kTableName));
  }
  FAIL() << "Did not get row count mismatch in " << kNumTries << " tries";
}

class PgIndexBackfillTestChunking : public PgIndexBackfillTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
        Format("--TEST_backfill_paging_size=$0", kBatchSize));
    options->extra_tserver_flags.push_back(
        Format("--backfill_index_write_batch_size=$0", kBatchSize));
    options->extra_tserver_flags.push_back(
        Format("--ysql_prefetch_limit=$0", kPrefetchSize));
  }
  const int kBatchSize = 200;
  const int kPrefetchSize = 128;
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillTestChunking, ::testing::Bool());

// Set batch size and prefetch limit such that:
// Each tablet requires multiple RPC calls from the master to complete backfill.
//     Also, set the ysql_prefetch_size small to ensure that each of these
//     `BACKFILL INDEX` calls will fetch data from the tserver at least 2 times.
// Fetch metrics to ensure that there have been > num_tablets rpc's.
TEST_P(
    PgIndexBackfillTestChunking, BackfillInChunks) {
  constexpr int kNumRows = 10000;
  TestLargeBackfill(kNumRows);

  const size_t effective_batch_size =
      static_cast<size_t>(kPrefetchSize * ceil(1.0 * kBatchSize / kPrefetchSize));
  const size_t min_expected_calls =
      static_cast<size_t>(ceil(1.0 * kNumRows / effective_batch_size));
  auto actual_calls = ASSERT_RESULT(TotalBackfillRpcCalls(cluster_.get()));
  LOG(INFO) << "Had " << actual_calls << " backfill rpc calls. "
            << "Expected at least " << kNumRows << "/" << effective_batch_size << " = "
            << min_expected_calls;
  ASSERT_GE(actual_calls, min_expected_calls);
}

class PgIndexBackfillTestThrottled : public PgIndexBackfillTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        Format("--ysql_index_backfill_rpc_timeout_ms=$0", kBackfillRpcDeadlineLargeMs));

    options->extra_tserver_flags.push_back("--ysql_prefetch_limit=100");
    options->extra_tserver_flags.push_back("--backfill_index_write_batch_size=100");
    options->extra_tserver_flags.push_back(
        Format("--backfill_index_rate_rows_per_sec=$0", kBackfillRateRowsPerSec));
    options->extra_tserver_flags.push_back(
        Format("--num_concurrent_backfills_allowed=$0", kNumConcurrentBackfills));
  }

 protected:
  const int kBackfillRateRowsPerSec = 100;
  const int kNumConcurrentBackfills = 1;
  const int kBackfillRpcDeadlineLargeMs = 10 * 60 * 1000;
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillTestThrottled, ::testing::Bool());

// Set the backfill batch size and backfill rate
// Check that the time taken to backfill is no less than what is expected.
TEST_P(PgIndexBackfillTestThrottled, ThrottledBackfill) {
  constexpr int kNumRows = 10000;
  auto start_time = CoarseMonoClock::Now();
  TestLargeBackfill(kNumRows);
  auto end_time = CoarseMonoClock::Now();
  auto expected_time = MonoDelta::FromSeconds(
      kNumRows * 1.0 /
      (cluster_->num_tablet_servers() * kNumConcurrentBackfills * kBackfillRateRowsPerSec));
  ASSERT_GE(MonoDelta{end_time - start_time}, expected_time);

  // Expect only 1 call per tablet
  const size_t expected_calls = cluster_->num_tablet_servers() * kTabletsPerServer;
  auto actual_calls = ASSERT_RESULT(TotalBackfillRpcCalls(cluster_.get()));
  ASSERT_EQ(actual_calls, expected_calls);

  auto avg_rpc_latency_usec = ASSERT_RESULT(AvgBackfillRpcLatencyInMicros(cluster_.get()));
  LOG(INFO) << "Avg backfill latency was " << avg_rpc_latency_usec << " us";
  ASSERT_LE(avg_rpc_latency_usec, kBackfillRpcDeadlineLargeMs * 1000);
}

class PgIndexBackfillTestDeadlines : public PgIndexBackfillTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--ysql_disable_index_backfill=false");
    options->extra_master_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));
    options->extra_master_flags.push_back(
        Format("--ysql_index_backfill_rpc_timeout_ms=$0", kBackfillRpcDeadlineSmallMs));
    options->extra_master_flags.push_back(
        Format("--backfill_index_timeout_grace_margin_ms=$0", kBackfillRpcDeadlineSmallMs / 2));

    options->extra_tserver_flags.push_back("--ysql_disable_index_backfill=false");
    options->extra_tserver_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));
    options->extra_tserver_flags.push_back("--ysql_prefetch_limit=100");
    options->extra_tserver_flags.push_back("--backfill_index_write_batch_size=100");
    options->extra_tserver_flags.push_back(
        Format("--backfill_index_rate_rows_per_sec=$0", kBackfillRateRowsPerSec));
    options->extra_tserver_flags.push_back(
        Format("--num_concurrent_backfills_allowed=$0", kNumConcurrentBackfills));
  }

 protected:
  const int kBackfillRpcDeadlineSmallMs = 10000;
  const int kBackfillRateRowsPerSec = 100;
  const int kNumConcurrentBackfills = 1;
  const int kTabletsPerServer = 1;
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillTestDeadlines, ::testing::Bool());

// Set the backfill batch size, backfill rate and a low timeout for backfill rpc.
// Ensure that the backfill is completed. And that the avg rpc latency is
// below what is set as the timeout.
TEST_P(PgIndexBackfillTestDeadlines, BackfillRespectsDeadline) {
  constexpr int kNumRows = 10000;
  TestLargeBackfill(kNumRows);

  const size_t num_tablets = cluster_->num_tablet_servers() * kTabletsPerServer;
  const size_t min_expected_calls = static_cast<size_t>(
      ceil(kNumRows / (kBackfillRpcDeadlineSmallMs * kBackfillRateRowsPerSec * 0.001)));
  ASSERT_GT(min_expected_calls, num_tablets);
  auto actual_calls = ASSERT_RESULT(TotalBackfillRpcCalls(cluster_.get()));
  ASSERT_GE(actual_calls, num_tablets);
  ASSERT_GE(actual_calls, min_expected_calls);

  auto avg_rpc_latency_usec = ASSERT_RESULT(AvgBackfillRpcLatencyInMicros(cluster_.get()));
  LOG(INFO) << "Avg backfill latency was " << avg_rpc_latency_usec << " us";
  ASSERT_LE(avg_rpc_latency_usec, kBackfillRpcDeadlineSmallMs * 1000);
}

// Make sure that CREATE INDEX NONCONCURRENTLY doesn't use backfill.
TEST_P(PgIndexBackfillTest, Nonconcurrent) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  const std::string table_id = ASSERT_RESULT(GetTableIdByTableName(
      client.get(), kDatabaseName, kTableName));

  // To determine whether the index uses backfill or not, look at the table schema version before
  // and after.  We can't look at the DocDB index permissions because
  // - if backfill is skipped, index_permissions is unset, and the default value is
  //   INDEX_PERM_READ_WRITE_AND_DELETE
  // - if backfill is used, index_permissions is INDEX_PERM_READ_WRITE_AND_DELETE
  // - GetTableSchemaById offers no way to see whether the default value for index permissions is
  //   set
  std::shared_ptr<client::YBTableInfo> info = std::make_shared<client::YBTableInfo>();
  {
    Synchronizer sync;
    ASSERT_OK(client->GetTableSchemaById(table_id, info, sync.AsStatusCallback()));
    ASSERT_OK(sync.Wait());
  }
  ASSERT_EQ(info->schema.version(), 0);

  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE INDEX NONCONCURRENTLY $0 ON $1 (i)",
      kIndexName,
      kTableName));

  // If the index used backfill, it would have incremented the table schema version by three:
  // - add index info with INDEX_PERM_DELETE_ONLY
  // - update to INDEX_PERM_DO_BACKFILL
  // - update to INDEX_PERM_READ_WRITE_AND_DELETE
  // If the index did not use backfill, it would have incremented the table schema version by one:
  // - add index info with no DocDB permission (default INDEX_PERM_READ_WRITE_AND_DELETE)
  // Expect that it did not use backfill.
  {
    Synchronizer sync;
    ASSERT_OK(client->GetTableSchemaById(table_id, info, sync.AsStatusCallback()));
    ASSERT_OK(sync.Wait());
  }
  ASSERT_EQ(info->schema.version(), 1);
}

class PgIndexBackfillTestSimultaneously : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        Format("--ysql_yb_index_state_flags_update_delay=$0",
               kIndexStateFlagsUpdateDelay.ToMilliseconds()));
  }
 protected:
#ifdef NDEBUG // release build; see issue #6238
  const MonoDelta kIndexStateFlagsUpdateDelay = 5s;
#else // NDEBUG
  const MonoDelta kIndexStateFlagsUpdateDelay = 1s;
#endif // NDEBUG
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillTestSimultaneously, ::testing::Bool());

// Test simultaneous CREATE INDEX.
TEST_P(PgIndexBackfillTestSimultaneously, CreateIndexSimultaneously) {
  const std::string query = Format("SELECT * FROM $0 WHERE i = $1", kTableName, 7);
  constexpr int kNumRows = 10;
  constexpr int kNumThreads = 5;
  int expected_schema_version = 0;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))",
      kTableName,
      kNumRows));

  std::array<Status, kNumThreads> statuses;
  for (int i = 0; i < kNumThreads; ++i) {
    thread_holder_.AddThreadFunctor([i, this, &statuses] {
      LOG(INFO) << "Begin thread " << i;
      // TODO (#19975): Enable read committed isolation
      PGConn create_conn = ASSERT_RESULT(SetDefaultTransactionIsolation(
          ConnectToDB(kDatabaseName), IsolationLevel::SNAPSHOT_ISOLATION));
      ASSERT_OK(create_conn.Execute("SET yb_force_early_ddl_serialization=false"));
      statuses[i] = MoveStatus(create_conn.ExecuteFormat(
          "CREATE INDEX $0 ON $1 (i)",
          kIndexName, kTableName));
    });
  }
  thread_holder_.JoinAll();

  LOG(INFO) << "Inspecting statuses";
  int num_ok = 0;
  ASSERT_EQ(statuses.size(), kNumThreads);
  for (const auto& status : statuses) {
    if (status.ok()) {
      num_ok++;
      LOG(INFO) << "got ok status";
      // Success index creations do three schema changes:
      // - add index with INDEX_PERM_WRITE_AND_DELETE
      // - transition to INDEX_PERM_DO_BACKFILL, then
      // - transition to success INDEX_PERM_READ_WRITE_AND_DELETE
      expected_schema_version += 3;
    } else {
      ASSERT_TRUE(status.IsNetworkError()) << status;
      const std::string msg = status.message().ToBuffer();
      const std::string relation_already_exists_msg = Format(
          "relation \"$0\" already exists", kIndexName);
      const auto allowed_msgs = {
        "Catalog Version Mismatch"sv,
        SerializeAccessErrorMessageSubstring(),
        "Restart read required"sv,
        "Transaction aborted"sv,
        "Transaction metadata missing"sv,
        "Unknown transaction, could be recently aborted"sv,
        std::string_view(relation_already_exists_msg),
      };
      ASSERT_TRUE(HasSubstring(msg, allowed_msgs)) << status;
      LOG(INFO) << "ignoring conflict error: " << status.message().ToBuffer();
      if (msg.find("Restart read required") == std::string::npos
          && msg.find(relation_already_exists_msg) == std::string::npos) {
        // Failed index creations do two schema changes:
        // - add index with INDEX_PERM_WRITE_AND_DELETE
        // - remove index because of DDL transaction rollback ("Table transaction failed, deleting")
        expected_schema_version += 2;
      } else {
        // If the DocDB index was never created in the first place, it incurs no schema changes.
      }
    }
  }
  ASSERT_EQ(num_ok, 1) << "only one CREATE INDEX should succeed";

  LOG(INFO) << "Checking postgres schema";
  {
    // Check number of indexes.
    ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<std::string>(
                Format("SELECT indexname FROM pg_indexes WHERE tablename = '$0'", kTableName))),
              kIndexName);

    // Check whether index is public using index scan.
    ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
  }
  LOG(INFO) << "Checking DocDB schema";
  std::vector<TableId> orphaned_docdb_index_ids;
  {
    auto client = ASSERT_RESULT(cluster_->CreateClient());
    const std::string table_id = ASSERT_RESULT(GetTableIdByTableName(
        client.get(), kDatabaseName, kTableName));
    std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
    Synchronizer sync;
    ASSERT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
    ASSERT_OK(sync.Wait());

    // Check number of DocDB indexes.  Normally, failed indexes should be cleaned up ("Table
    // transaction failed, deleting"), but in the event of an unexpected issue, they may not be.
    // (Not necessarily a fatal issue because the postgres schema is good.)
    auto num_docdb_indexes = table_info->index_map.size();
    if (num_docdb_indexes > 1) {
      LOG(INFO) << "found " << num_docdb_indexes << " DocDB indexes";
      // These failed indexes not getting rolled back mean one less schema change each.  Therefore,
      // adjust the expected schema version.
      auto num_failed_docdb_indexes = num_docdb_indexes - 1;
      expected_schema_version -= num_failed_docdb_indexes;
    }

    // Check index permissions.  Also collect orphaned DocDB indexes.
    int num_rwd = 0;
    for (const auto& pair : table_info->index_map) {
      VLOG(1) << "table id: " << pair.first;
      IndexPermissions perm = pair.second.index_permissions();
      if (perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE) {
        num_rwd++;
      } else {
        ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_WRITE_AND_DELETE);
        orphaned_docdb_index_ids.emplace_back(pair.first);
      }
    }
    ASSERT_EQ(num_rwd, 1)
        << "found " << num_rwd << " fully created (readable) DocDB indexes: expected " << 1;

    // Check schema version.
    ASSERT_EQ(table_info->schema.version(), expected_schema_version)
        << "got indexed table schema version " << table_info->schema.version()
        << ": expected " << expected_schema_version;
    // At least one index must have tried to create but gotten aborted, resulting in +1 or +2
    // catalog version bump.  The 2 below is for the successfully created index.
    ASSERT_GT(expected_schema_version, 2);
  }

  LOG(INFO) << "Checking if index still works";
  {
    ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
    ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<int32_t>(query)), 7);
  }
}

// Make sure that backfill works in a tablegroup.
TEST_P(PgIndexBackfillTest, Tablegroup) {
  const std::string kTablegroupName = "test_tgroup";
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupName));

  TestSimpleBackfill(Format("TABLEGROUP $0", kTablegroupName));
}

// Test that retain_delete_markers is properly set after index backfill.
TEST_P(PgIndexBackfillTest, RetainDeleteMarkers) {
  TestRetainDeleteMarkers(kDatabaseName);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/19731.
TEST_P(PgIndexBackfillTest, RetainDeleteMarkersRecovery) {
  TestRetainDeleteMarkersRecovery(kDatabaseName, false /* use_multiple_requests */);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/19731.
TEST_P(PgIndexBackfillTest, RetainDeleteMarkersRecoveryViaSeveralRequests) {
  TestRetainDeleteMarkersRecovery(kDatabaseName, true /* use_multiple_requests */);
}

// Override the index backfill test to do alter slowly.
class PgIndexBackfillAlterSlowly : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--TEST_alter_schema_delay_ms=10000");
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillAlterSlowly, ::testing::Bool());

// Test whether IsCreateTableDone works when creating an index with backfill enabled.  See issue
// #6234.
TEST_P(PgIndexBackfillAlterSlowly, IsCreateTableDone) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName));
}

// Override the index backfill test to have different HBA config:
// 1. if any user tries to access the authdb database, enforce md5 auth
// 2. if the postgres user tries to access the yugabyte database, allow it
// 3. if the yugabyte user tries to access the yugabyte database, allow it
// 4. otherwise, disallow it
class PgIndexBackfillAuth : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(Format(
        "--ysql_hba_conf="
        "host $0 all all md5,"
        "host $1 postgres all trust,"
        "host $1 yugabyte all trust",
        kAuthDbName,
        kDatabaseName));
  }

  const std::string kAuthDbName = "authdb";
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillAuth, ::testing::Bool());

// Test backfill on clusters where the yugabyte role has authentication enabled.
TEST_P(PgIndexBackfillAuth, Auth) {
  LOG(INFO) << "create " << this->kAuthDbName << " database";
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0", this->kAuthDbName));

  LOG(INFO) << "backfill table on " << this->kAuthDbName << " database";
  {
    auto auth_conn = ASSERT_RESULT(PGConnBuilder({
        .host = pg_ts->bind_host(),
        .port = pg_ts->ysql_port(),
        .dbname = this->kAuthDbName,
        .user = "yugabyte",
        .password = "yugabyte"
    }).Connect());
    ASSERT_OK(auth_conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
    ASSERT_OK(auth_conn.ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName));
  }
}

// Override the index backfill test to have HBA config with local trust:
// 1. if any user tries to connect over ip, trust
// 2. if any user tries to connect over unix-domain socket, trust
class PgIndexBackfillLocalTrust : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(Format(
        "--ysql_hba_conf="
        "host $0 all all trust,"
        "local $0 all trust",
        kDatabaseName));
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillLocalTrust, ::testing::Bool());

// Make sure backfill works when there exists user-defined HBA configuration with "local".
// This is for issue (#7705).
TEST_P(PgIndexBackfillLocalTrust, LocalTrustSimple) {
  TestSimpleBackfill();
}

// Override the index backfill test to disable transparent retries on cache version mismatch.
class PgIndexBackfillNoRetry : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
        "--TEST_ysql_disable_transparent_cache_refresh_retry=true");
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillNoRetry, ::testing::Bool());

TEST_P(PgIndexBackfillNoRetry, DropNoRetry) {
  constexpr int kNumRows = 5;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, j int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), generate_series(11, 10 + $1))",
      kTableName,
      kNumRows));

  // Create index.
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));

  // Update the table cache entry for the indexed table.
  ASSERT_OK(conn_->FetchFormat("SELECT * FROM $0", kTableName));

  // Drop index.
  ASSERT_OK(conn_->ExecuteFormat("DROP INDEX $0", kIndexName));

  // Ensure that there is no schema version mismatch for the indexed table.  This is because the
  // above `DROP INDEX` should have invalidated the corresponding table cache entry.  (There also
  // should be no catalog version mismatch because it is updated for the same session after DDL.)
  ASSERT_OK(conn_->FetchFormat("SELECT * FROM $0", kTableName));
}

class PgIndexBackfillGinStress : public PgIndexBackfillTest {
 public:
  int GetNumMasters() const override {
    return 1;
  }

  int GetNumTabletServers() const override {
    return 1;
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--index_backfill_rpc_max_retries=0");
    options->extra_master_flags.push_back("--replication_factor=1");
    options->extra_tserver_flags.push_back("--enable_automatic_tablet_splitting=false");
    options->extra_tserver_flags.push_back("--ysql_num_tablets=1");
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillGinStress, ::testing::Bool());

TEST_P(PgIndexBackfillGinStress, YB_LINUX_RELEASE_ONLY_TEST(GinStress)) {
  // Note: too high numbers error with issue #13825 or #21114.
  constexpr auto kNumIndexRowsPerTableRow = 10000;
  constexpr auto kNumRows = 1000;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (a int[])", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(R"#(
      INSERT INTO $0
          SELECT (SELECT ARRAY(SELECT floor(random() * 100000) FROM generate_series(1, $1)))
          FROM generate_series(1, $2)
      )#",
      kTableName, kNumIndexRowsPerTableRow, kNumRows));
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX $0 ON $1 USING ybgin (a)", kIndexName, kTableName));
}

// Override the index backfill test to have slower backfill-related operations
class PgIndexBackfillSlow : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(Format(
        "--TEST_slowdown_backfill_alter_table_rpcs_ms=$0",
        kBackfillAlterTableDelay.ToMilliseconds()));
    options->extra_tserver_flags.push_back(Format(
        "--ysql_yb_index_state_flags_update_delay=$0",
        kIndexStateFlagsUpdateDelay.ToMilliseconds()));
    options->extra_tserver_flags.push_back(Format(
        "--TEST_slowdown_backfill_by_ms=$0",
        kBackfillDelay.ToMilliseconds()));
  }

 protected:
  // gflag delay times.
  const MonoDelta kBackfillAlterTableDelay = 0s;
  const MonoDelta kBackfillDelay = RegularBuildVsSanitizers(3s, 7s);
  const MonoDelta kIndexStateFlagsUpdateDelay = RegularBuildVsDebugVsSanitizers(3s, 5s, 7s);
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillSlow, ::testing::Bool());

class PgIndexBackfillBlockDoBackfill : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--TEST_block_do_backfill=true");
  }

 protected:
  Status WaitForBackfillSafeTime(const client::YBTableName& table_name) {
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    const std::string table_id = VERIFY_RESULT(
        GetTableIdByTableName(client.get(), table_name.namespace_name(), table_name.table_name()));
    RETURN_NOT_OK(WaitForBackfillSafeTimeOn(
        cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>(), table_id));
    return Status::OK();
  }

  // Helper: delete and re-insert the same indexed value after backfill safe time.
  // When use_single_txn is true, the DELETE and INSERT happen in a single transaction
  // (same commit HT, different write_ids). Otherwise they are separate statements
  // (different HTs).
  void TestDeleteAndInsertSameValue(bool use_single_txn);
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillBlockDoBackfill, ::testing::Bool());

class PgIndexBackfillBlockIndisready : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--ysql_yb_test_block_index_phase=indisready");
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillBlockIndisready, ::testing::Bool());

class PgIndexBackfillBlockIndisreadyAndDoBackfill : public PgIndexBackfillBlockDoBackfill {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillBlockDoBackfill::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--ysql_yb_test_block_index_phase=indisready");
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillBlockIndisreadyAndDoBackfill, ::testing::Bool());

// Override the index backfill test to have delays for testing snapshot too old.
class PgIndexBackfillSnapshotTooOld : public PgIndexBackfillBlockDoBackfill {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillBlockDoBackfill::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--ysql_yb_index_state_flags_update_delay=0");
    options->extra_tserver_flags.push_back(Format(
        "--timestamp_history_retention_interval_sec=$0", kHistoryRetentionInterval.ToSeconds()));
  }

 protected:
  const MonoDelta kHistoryRetentionInterval = 3s;
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillSnapshotTooOld, ::testing::Bool());

// Make sure that index backfill doesn't care about snapshot too old.  Force a situation where the
// indexed table scan for backfill would occur after the committed history cutoff.  A compaction is
// needed to update this committed history cutoff, and the retention period needs to be low enough
// so that the cutoff is ahead of backfill's safe read time.  See issue #6333.
TEST_P(PgIndexBackfillSnapshotTooOld, SnapshotTooOld) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  constexpr int kTimeoutSec = 3;

  // (Make it one tablet for simplicity.)
  LOG(INFO) << "Create table...";
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (c char) SPLIT INTO 1 TABLETS", kTableName));

  LOG(INFO) << "Get table id for indexed table...";
  const std::string table_id = ASSERT_RESULT(GetTableIdByTableName(
      client.get(), kDatabaseName, kTableName));

  // Insert something so that reading it would trigger snapshot too old.
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ('s')", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    LOG(INFO) << "Create index...";
    Status s = conn_->ExecuteFormat("CREATE INDEX $0 ON $1 (c)", kIndexName, kTableName);
    if (!s.ok()) {
      // We are doomed to fail the test.  Before that, let's see if it turns out to be "snapshot too
      // old" or some other unexpected error.
      ASSERT_TRUE(s.IsNetworkError()) << "got unexpected error: " << s;
      ASSERT_TRUE(s.message().ToBuffer().find("Snapshot too old") != std::string::npos)
          << "got unexpected error: " << s;
      // It is "snapshot too old".  Fail now.
      FAIL() << "got snapshot too old: " << s;
    }
  });
  thread_holder_.AddThreadFunctor([this, &client, &table_id] {
    LOG(INFO) << "Begin compact thread";
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    LOG(INFO) << "Sleep past history retention...";
    SleepFor(kHistoryRetentionInterval);

    LOG(INFO) << "Flush and compact indexed table...";
    ASSERT_OK(client->FlushTables(
        {table_id},
        false /* add_indexes */,
        kTimeoutSec,
        false /* is_compaction */));
    ASSERT_OK(client->FlushTables(
        {table_id},
        false /* add_indexes */,
        kTimeoutSec,
        true /* is_compaction */));

    LOG(INFO) << "Unblock backfill...";
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });
  thread_holder_.JoinAll();
}

// Make sure that read time (and write time) for backfill works.  Simulate the following:
//   Session A                                    Session B
//   --------------------------                   ---------------------------------
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//                                                UPDATE a few rows of the indexed table
//     - do the actual backfill
//   - indisvalid
// The backfill should use the values before update when writing to the index.  The updates should
// write and delete to the index because of permissions.  Since backfill writes with an ancient
// timestamp, the updates should appear to have happened after the backfill.
TEST_P(PgIndexBackfillBlockDoBackfill, ReadTime) {
  constexpr auto kNumRows = 100;
  constexpr auto kDeltaInCols = 10;
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (i int, j int, PRIMARY KEY (i ASC))", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT g, g + $1 FROM generate_series(1, $2) g",
      kTableName, kDeltaInCols, kNumRows));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    constexpr auto kNumSplits = 10;
    ASSERT_OK(create_conn.ExecuteFormat(
        "CREATE INDEX $0 ON $1 (j ASC) $2", kIndexName, kTableName,
        GenerateSplitClause(kNumRows, kNumSplits)));
    LOG(INFO) << "Done create thread";
  });
  std::set<int> updated_keys;
  constexpr auto kNewDeltaInCols = 200;
  thread_holder_.AddThreadFunctor([this, &updated_keys, kNewDeltaInCols] {
    LOG(INFO) << "Begin write thread";
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    LOG(INFO) << "Updating rows";
    while (updated_keys.size() < 10) {
      unsigned int seed = SeedRandom();
      updated_keys.insert((rand_r(&seed) % kNumRows) + 1);
    }
    for (auto key : updated_keys) {
      ASSERT_OK(conn_->ExecuteFormat(
        "UPDATE $0 SET j = i + $1 WHERE i = $2", kTableName, kNewDeltaInCols, key));
    }
    LOG(INFO) << "Updated rows: " << AsString(updated_keys);

    // It should still be in the backfill stage.
    ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(
        kIndexName, IndexStateFlags{IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady})));

    LOG(INFO) << "resume backfill";
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });
  thread_holder_.JoinAll();

  // Index scan to verify contents of index table.
  for (int key : updated_keys) {
    std::string query = Format("SELECT * FROM $0 WHERE j = $1", kTableName, key + kNewDeltaInCols);
    ASSERT_OK(WaitForIndexScan(query));
    const auto row = ASSERT_RESULT((conn_->FetchRow<int32_t, int32_t>(query)));
    // Make sure that the update is visible.
    ASSERT_EQ(row, (decltype(row){key, key + kNewDeltaInCols}));
  }

  // Make sure that the updated index keys have indeed been deleted.
  for (int key : updated_keys) {
    std::string query = Format("SELECT * FROM $0 WHERE j = $1", kTableName, key + kDeltaInCols);
    ASSERT_OK(WaitForIndexScan(query));
    const auto rows = ASSERT_RESULT((conn_->FetchRows<int32_t, int32_t>(query)));
    ASSERT_EQ(rows.size(), 0);
  }
}

// Make sure that updates at each stage of multi-stage CREATE INDEX work.  Simulate the following:
//   Session A                                    Session B
//   --------------------------                   ---------------------------------
//   CREATE INDEX
//   - indislive
//                                                UPDATE a row of the indexed table
//   - indisready
//                                                UPDATE a row of the indexed table
//   - indisvalid
//                                                UPDATE a row of the indexed table
// Updates should succeed and get written to the index.
TEST_P(PgIndexBackfillBlockIndisready, YB_DISABLE_TEST_IN_TSAN(Permissions)) {
  const CoarseDuration kThreadWaitTime = 60s;
  const std::array<std::tuple<IndexStateFlags, int, std::string>, 3> infos = {
    std::make_tuple(IndexStateFlags{IndexStateFlag::kIndIsLive}, 2, "postbackfill"),
    std::make_tuple(
        IndexStateFlags{IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady}, 3, "none"),
    std::make_tuple(
        IndexStateFlags{
          IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady, IndexStateFlag::kIndIsValid},
        4,
        "none"),
  };
  std::atomic<int> updates(0);

  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (i int, j int, PRIMARY KEY (i ASC))", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(0, 5), generate_series(10, 15))", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(create_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName));
  });
  thread_holder_.AddThreadFunctor([this, &infos, &updates] {
    LOG(INFO) << "Begin write thread";
    for (const auto& tup : infos) {
      const IndexStateFlags& index_state_flags = std::get<0>(tup);
      int key = std::get<1>(tup);
      const auto& label = std::get<2>(tup);

      ASSERT_OK(WaitForIndexStateFlags(index_state_flags));
      LOG(INFO) << "running UPDATE on i = " << key;
      ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET j = j + 100 WHERE i = $1", kTableName, key));
      LOG(INFO) << "done running UPDATE on i = " << key;

      // Unblock state change (if any).
      ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(kIndexName, index_state_flags)));
      ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", label));
      updates++;
    }
  });
  thread_holder_.WaitAndStop(kThreadWaitTime);

  ASSERT_EQ(updates.load(std::memory_order_acquire), infos.size());

  for (const auto& tup : infos) {
    int key = std::get<1>(tup);

    // Verify contents of index table.
    LOG(INFO) << "verifying i = " << key;
    const std::string query = Format(
        "WITH j_idx AS (SELECT * FROM $0 ORDER BY j) SELECT j FROM j_idx WHERE i = $1",
        kTableName,
        key);
    ASSERT_OK(WaitForIndexScan(query));
    // Make sure that the update is visible.
    ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<int32_t>(query)), key + 110);
  }
}

// Make sure that writes during CREATE UNIQUE INDEX don't cause unique duplicate row errors to be
// thrown.  Simulate the following:
//   Session A                                    Session B
//   --------------------------                   ---------------------------------
//                                                INSERT row(s) to the indexed table
//   CREATE UNIQUE INDEX
//                                                INSERT row(s) to the indexed table
//   - indislive
//                                                INSERT row(s) to the indexed table
//   - indisready
//                                                INSERT row(s) to the indexed table
//   - backfill
//                                                INSERT row(s) to the indexed table
//   - indisvalid
//                                                INSERT row(s) to the indexed table
// Particularly pay attention to the insert between indisready and backfill.  The insert
// should cause a write to go to the index.  Backfill should choose a read time after this write, so
// it should try to backfill this same row.  Rather than conflicting when we see the row already
// exists in the index during backfill, check whether the rows match, and don't error if they do.
TEST_P(PgIndexBackfillSlow, CreateUniqueIndexWithOnlineWrites) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  // Start a thread that continuously inserts distinct values.  The hope is that this would cause
  // inserts to happen at all permissions.
  thread_holder_.AddThreadFunctor([this, &stop = thread_holder_.stop_flag()] {
    LOG(INFO) << "Begin write thread";
    PGConn insert_conn = ASSERT_RESULT(Connect());
    int i = 0;
    while (!stop.load(std::memory_order_acquire)) {
      Status status = insert_conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, ++i);
      if (!status.ok()) {
        // Ignore transient errors that likely occur when changing index permissions.
        // TODO(jason): no longer expect schema version mismatch errors after closing issue #3979.
        ASSERT_TRUE(status.IsNetworkError()) << status;
        std::string msg = status.message().ToBuffer();
        const std::vector<std::string> allowed_msgs{
          "Errors occurred while reaching out to the tablet servers",
          "Resource unavailable",
          "schema version mismatch",
          "Transaction aborted",
          "expired or aborted by a conflict",
          "Transaction was recently aborted",
        };
        ASSERT_TRUE(std::find_if(
            std::begin(allowed_msgs),
            std::end(allowed_msgs),
            [&msg] (const std::string allowed_msg) {
              return msg.find(allowed_msg) != std::string::npos;
            }) != std::end(allowed_msgs))
          << status;
        LOG(WARNING) << "ignoring transient error: " << status.message().ToBuffer();
      }
    }
  });

  // Create unique index (should not complain about duplicate row).
  LOG(INFO) << "Create unique index...";
  ASSERT_OK(conn_->ExecuteFormat("CREATE UNIQUE INDEX ON $0 (i ASC)", kTableName));

  thread_holder_.Stop();
}

// Simulate the following:
//   Session A                                    Session B
//   ------------------------------------         -------------------------------------------
//   CREATE TABLE (i, j, PRIMARY KEY (i))
//                                                INSERT (1, 'a')
//   CREATE UNIQUE INDEX (j)
//   - DELETE_ONLY perm
//                                                DELETE (1, 'a')
//                                                (delete (1, 'a') to index)
//                                                INSERT (2, 'a')
//   - WRITE_DELETE perm
//   - BACKFILL perm
//     - get safe time for read
//                                                INSERT (3, 'a')
//                                                (insert (3, 'a') to index)
//     - do the actual backfill
//                                                (insert (2, 'a') to index--detect conflict)
//   - READ_WRITE_DELETE perm
// This test is for issue #6208.
TEST_P(PgIndexBackfillBlockIndisreadyAndDoBackfill, CreateUniqueIndexWriteAfterSafeTime) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, j char, PRIMARY KEY (i))", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 'a')", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    LOG(INFO) << "Creating index...";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    Status s = create_conn.ExecuteFormat(
        "CREATE UNIQUE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName);
    ASSERT_NOK(s);
    ASSERT_TRUE(s.IsNetworkError());
    ASSERT_TRUE(s.message().ToBuffer().find("duplicate key value") != std::string::npos) << s;
  });
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin write thread";
    {
      const IndexStateFlags index_state_flags{IndexStateFlag::kIndIsLive};

      LOG(INFO) << "Wait for indislive index state flag";
      ASSERT_OK(WaitForIndexStateFlags(index_state_flags));

      LOG(INFO) << "Do delete and insert";
      ASSERT_OK(conn_->ExecuteFormat("DELETE FROM $0 WHERE i = 1", kTableName));
      ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (2, 'a')", kTableName));

      LOG(INFO) << "Check we're not yet at indisready index state flag";
      ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(kIndexName, index_state_flags)));
    }

    // Unblock CREATE INDEX waiting to set indisready.  The next blocking point is by master's
    // TEST_block_do_backfill.
    ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));

    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    LOG(INFO) << "Do insert between safe time and backfill";
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (3, 'a')", kTableName));

    // Unblock CREATE INDEX waiting to do backfill.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });
  thread_holder_.JoinAll();

  // Check.
  {
    CoarseBackoffWaiter waiter(CoarseMonoClock::Now() + 10s, CoarseMonoClock::Duration::max());
    while (true) {
      auto result = conn_->FetchRow<PGUint64>(Format("SELECT count(*) FROM $0", kTableName));
      if (result.ok()) {
        ASSERT_EQ(*result, 2);
        break;
      }
      auto& s = result.status();
      ASSERT_TRUE(s.IsNetworkError()) << s;
      ASSERT_TRUE(s.message().ToBuffer().find("schema version mismatch") != std::string::npos) << s;
      ASSERT_TRUE(waiter.Wait());
    }
  }
}

// Simulate the following:
//   Session A                                    Session B
//   ------------------------------------         -------------------------------------------
//   CREATE TABLE (i, j, PRIMARY KEY (i))
//                                                INSERT (1, 'a')
//   CREATE UNIQUE INDEX (j)
//   - indislive
//   - indisready
//   - backfill stage
//     - get safe time for read
//                                                DELETE (1, 'a')
//                                                (delete (1, 'a') to index)
//     - do the actual backfill
//       (insert (1, 'a') to index)
//   - indisvalid
// This test is for issue #6811.  Remember, backfilled rows get written with write time = safe time,
// so they should have an MVCC timestamp lower than that of the deletion.  If deletes to the index
// aren't written, then this test will always fail because the backfilled row has no delete to cover
// it.  If deletes to the index aren't retained, then this test will fail if compactions get rid of
// the delete before the backfilled row gets written.
TEST_P(PgIndexBackfillBlockDoBackfill, RetainDeletes) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, j char, PRIMARY KEY (i))", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 'a')", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    LOG(INFO) << "Creating index";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(create_conn.ExecuteFormat(
        "CREATE UNIQUE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName));
  });
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin write thread";
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    LOG(INFO) << "Deleting row";
    ASSERT_OK(conn_->ExecuteFormat("DELETE FROM $0 WHERE i = 1", kTableName));

    // It should still be in the backfill stage.
    ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(
        kIndexName, IndexStateFlags{IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady})));

    // Unblock CREATE INDEX waiting to do backfill.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });
  thread_holder_.JoinAll();

  // Check.
  auto result = conn_->FetchRow<PGUint64>(Format(
      "SELECT count(*) FROM $0 WHERE j = 'a'", kTableName));
  if (result.ok()) {
    ASSERT_EQ(*result, 0);
  } else {
    auto& s = result.status();
    ASSERT_TRUE(s.IsNetworkError()) << "unexpected status: " << s;
    const std::string msg = s.message().ToBuffer();
    if (msg.find("Given ybctid is not associated with any row in table") == std::string::npos) {
      FAIL() << "unexpected status: " << s;
    }
    FAIL() << "delete to index was not present by the time backfill happened: " << s;
  }
}

TEST_P(PgIndexBackfillBlockDoBackfill, IndexScanVisibility) {
  ExternalTabletServer* diff_ts = cluster_->tablet_server(1);
  // Make sure default tserver is 0.  At the time of writing, this is set in
  // PgWrapperTestBase::SetUp.
  ASSERT_NE(pg_ts, diff_ts);

  LOG(INFO) << "Create connection to run CREATE INDEX";
  PGConn create_index_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  LOG(INFO) << "Create connection to the same tablet server as the one running CREATE INDEX";
  PGConn same_ts_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  LOG(INFO) << "Create connection to a different tablet server from the one running CREATE INDEX";
  PGConn diff_ts_conn = ASSERT_RESULT(PGConnBuilder({
    .host = diff_ts->bind_host(),
    .port = diff_ts->ysql_port(),
    .dbname = kDatabaseName
  }).Connect());

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName));

  thread_holder_.AddThreadFunctor([this, &same_ts_conn, &diff_ts_conn] {
    LOG(INFO) << "Begin select thread";
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    LOG(INFO) << "Load DocDB table/index schemas to pggate cache for the other connections";
    ASSERT_RESULT(same_ts_conn.FetchFormat("SELECT * FROM $0 WHERE i = 2", kTableName));
    ASSERT_RESULT(diff_ts_conn.FetchFormat("SELECT * FROM $0 WHERE i = 2", kTableName));

    // Unblock DoBackfill.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });

  LOG(INFO) << "Create index...";
  ASSERT_OK(create_index_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName));
  ASSERT_TRUE(thread_holder_.stop_flag())
      << "select thread did not finish by the time CREATE INDEX ended";
  CoarseTimePoint start_time = CoarseMonoClock::Now();

  LOG(INFO) << "Check for index scan...";
  const std::string query = Format("SELECT * FROM $0 WHERE i = 2", kTableName);
  // The session that ran CREATE INDEX should immediately be ready for index scan.
  ASSERT_TRUE(ASSERT_RESULT(create_index_conn.HasIndexScan(query)));
  // Eventually, the other sessions should see the index as public.  They may take some time because
  // they don't know about the latest catalog update until
  // 1. master sends catalog version through heartbeat to tserver
  // 2. tserver shares catalog version to postgres through shared memory
  // Another avenue to learn that the index is public is to send a request to tserver and get a
  // schema version mismatch on the indexed table.  Since HasIndexScan uses EXPLAIN, it doesn't hit
  // tserver, so postgres will be unaware until catalog version is updated in shared memory.  Expect
  // 0s-1s since default heartbeat period is 1s (see flag heartbeat_interval_ms).
  ASSERT_OK(WaitFor(
      [&query, &same_ts_conn, &diff_ts_conn]() -> Result<bool> {
        bool same_ts_has_index_scan = VERIFY_RESULT(same_ts_conn.HasIndexScan(query));
        bool diff_ts_has_index_scan = VERIFY_RESULT(diff_ts_conn.HasIndexScan(query));
        LOG(INFO) << "same_ts_has_index_scan: " << same_ts_has_index_scan
                  << ", "
                  << "diff_ts_has_index_scan: " << diff_ts_has_index_scan;
        return same_ts_has_index_scan && diff_ts_has_index_scan;
      },
      30s,
      "Wait for IndexScan"));
  LOG(INFO) << "It took " << yb::ToString(CoarseMonoClock::Now() - start_time)
            << " for other sessions to notice that the index became public";
}

TEST_P(PgIndexBackfillTest, SimulateEmptyIndexesForStackOverflow) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_simulate_empty_indexes_during_backfill", "true"));

  LOG(INFO) << "Create connection to run CREATE INDEX";
  PGConn create_index_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  LOG(INFO) << "Create index...";
  // The failed BackfillChunk/BackfillTable will not update the
  // backfill_error_message in IndexInfoPB for kIndexName. Thus the following
  // CREATE INDEX won't bubble up an error from the backfill job failing.
  ASSERT_OK(create_index_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName));

  cluster_->AssertNoCrashes();
}

// Override to have smaller backfill deadline.
class PgIndexBackfillClientDeadline : public PgIndexBackfillBlockDoBackfill {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillBlockDoBackfill::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--backfill_index_client_rpc_timeout_ms=3000");
  }
};

// https://github.com/yugabyte/yugabyte-db/issues/28849
TEST_P(PgIndexBackfillTest, MultipleIndexesFirstOneInvalid) {
  auto conn = CHECK_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE table foo(id int, id2 int)"));
  // Insert values so that id contain duplicate values, id2 contais unique values.
  ASSERT_OK(conn.Execute("INSERT INTO foo values (1, 1), (2, 2)"));
  ASSERT_OK(conn.Execute("INSERT INTO foo values (1, 3), (2, 4)"));
  // Do CONCURRENTLY to trigger the multi-stage index creation.
  auto status = conn.Execute("CREATE UNIQUE INDEX CONCURRENTLY id_idx ON foo(id)");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "duplicate key value violates unique constraint");

  // Before fixing 28849, this moved the permission of id_idx from
  // INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING to INDEX_PERM_DELETE_ONLY_WHILE_REMOVING
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX CONCURRENTLY id_idx2 ON foo(id2)"));

  // Before fixing 28849, this moved the permission of id_idx from
  // INDEX_PERM_DELETE_ONLY_WHILE_REMOVING to INDEX_PERM_INDEX_UNUSED, INDEX_PERM_INDEX_UNUSED
  // caused the docdb table for id_idx to be deleted.
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX CONCURRENTLY id_idx3 ON foo(id2)"));

  // As a result, this INSERT failed with OBJECT_NOT_FOUND error.
  ASSERT_OK(conn.Execute("INSERT INTO foo VALUES (5, 5)"));
  status = conn.Execute("INSERT INTO foo VALUES (5, 6)");
  // Although id_idx is in an invalid state, it still enforces that new values inserted
  // must be unique according to id_idx, id_idx2 and id_idx3. We have already inserted
  // a new value 5 for id, a second insertion of 5 for id causes id_idx to detect
  // unique constraint violation.
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "duplicate key value violates unique constraint");

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto count_indexes_fn = [&client]() -> int {
    auto tables = CHECK_RESULT(client->ListTables());
    int count = 0;
    for (const auto& table : tables) {
      if (table.namespace_name() != kDatabaseName)
        continue;
      const auto& table_name = table.table_name();
      if (table_name == "id_idx" || table_name == "id_idx2" || table_name == "id_idx3") {
        count++;
      }
    }
    return count;
  };

  // Make sure that the indexes exists in DocDB metadata.
  ASSERT_EQ(count_indexes_fn(), 3);

  // Make sure drop index works.
  ASSERT_OK(conn.Execute("DROP INDEX id_idx"));
  ASSERT_OK(conn.Execute("DROP INDEX id_idx2"));
  ASSERT_OK(conn.Execute("DROP INDEX id_idx3"));

  // Make sure that the index is gone.
  // Check postgres metadata.
  auto value = ASSERT_RESULT(conn_->FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_class WHERE relname like 'id_idx%'"));
  ASSERT_EQ(value, 0);

  // Check DocDB metadata.
  ASSERT_EQ(count_indexes_fn(), 0);
}

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillClientDeadline, ::testing::Bool());

// Make sure that the postgres timeout when waiting for backfill to finish causes the index to not
// become public.  Simulate the following:
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//   - (timeout)
TEST_P(PgIndexBackfillClientDeadline, WaitBackfillTimeout) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  Status status = conn_->ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName);
  ASSERT_TRUE(HasClientTimedOut(status)) << status;

  // Make sure that the index is not public.
  ASSERT_FALSE(ASSERT_RESULT(conn_->HasIndexScan(Format(
      "SELECT * FROM $0 WHERE i = 1",
      kTableName))));
}

// Make sure that you can still drop an index that failed to fully create.
TEST_P(PgIndexBackfillClientDeadline, DropAfterFail) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  Status status = conn_->ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName);
  ASSERT_TRUE(HasClientTimedOut(status)) << status;

  // Unblock DoBackfill.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));

  // Make sure that the index exists in DocDB metadata.
  auto tables = ASSERT_RESULT(client->ListTables());
  bool found = false;
  for (const auto& table : tables) {
    if (table.namespace_name() == kDatabaseName && table.table_name() == kIndexName) {
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);

  ASSERT_OK(conn_->ExecuteFormat("DROP INDEX $0", kIndexName));

  // Make sure that the index is gone.
  // Check postgres metadata.
  auto value = ASSERT_RESULT(conn_->FetchRow<PGUint64>(
      Format("SELECT COUNT(*) FROM pg_class WHERE relname = '$0'", kIndexName)));
  ASSERT_EQ(value, 0);
  // Check DocDB metadata.
  tables = ASSERT_RESULT(client->ListTables());
  for (const auto& table : tables) {
    ASSERT_FALSE(table.namespace_name() == kDatabaseName && table.table_name() == kIndexName);
  }
}

// Override to have a 30s BackfillIndex client timeout.
class PgIndexBackfillFastClientTimeout : public PgIndexBackfillBlockDoBackfill {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillBlockDoBackfill::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--backfill_index_client_rpc_timeout_ms=30000");
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillFastClientTimeout, ::testing::Bool());

// Make sure that DROP INDEX during backfill is handled well.  Simulate the following:
//   Session A                                    Session B
//   --------------------------                   ----------------------
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//                                                DROP INDEX
TEST_P(PgIndexBackfillFastClientTimeout, DropWhileBackfilling) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    // We don't want the DROP INDEX to face a serialization error when acquiring the FOR UPDATE lock
    // on the catalog version row.
    ASSERT_OK(create_conn.Execute("SET yb_force_early_ddl_serialization=false"));
    Status status = create_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName);
    // Expect timeout because
    // DROP INDEX is currently not online and removes the index info from the indexed table
    // ==> the WaitUntilIndexPermissionsAtLeast will keep failing and retrying GetTableSchema on the
    // index.
    ASSERT_TRUE(HasClientTimedOut(status)) << status;
  });
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin drop thread";
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    LOG(INFO) << "Drop index";
    ASSERT_OK(conn_->ExecuteFormat("DROP INDEX $0", kIndexName));

    // Unblock CREATE INDEX waiting to do backfill.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });
  thread_holder_.JoinAll();
}

// Override the index backfill test class to have a default client admin timeout one second smaller
// than backfill delay.  Also, ensure client backfill timeout is high, and set num_tablets to 1 to
// make the test finish more quickly.
class PgIndexBackfillFastDefaultClientTimeout : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(Format(
        "--TEST_slowdown_backfill_by_ms=$0",
        kBackfillDelay.ToMilliseconds()));
    options->extra_tserver_flags.push_back(Format(
        "--yb_client_admin_operation_timeout_sec=$0", (kBackfillDelay - 1s).ToSeconds()));
    options->extra_tserver_flags.push_back("--backfill_index_client_rpc_timeout_ms=60000"); // 1m
    options->extra_tserver_flags.push_back("--ysql_num_tablets=1");
  }
 protected:
  const MonoDelta kBackfillDelay = RegularBuildVsSanitizers(7s, 14s);
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillFastDefaultClientTimeout, ::testing::Bool());

// Simply create table and index.  The CREATE INDEX should not timeout during backfill because the
// BackfillIndex request from postgres should use the backfill_index_client_rpc_timeout_ms timeout
// (default 60m) rather than the small yb_client_admin_operation_timeout_sec.
TEST_P(PgIndexBackfillFastDefaultClientTimeout, LowerDefaultClientTimeout) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  // This should not time out.
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName));
}

// Override the index backfill fast client timeout test class to have more than one master.
class PgIndexBackfillMultiMaster : public PgIndexBackfillFastClientTimeout {
 public:
  int GetNumMasters() const override { return 3; }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillMultiMaster, ::testing::Bool());

// Make sure that master leader change during backfill causes the index backfill to continue and
// doesn't cause any weird hangups or other issues.  Simulate the following:
//   Session A                                    Session B
//   --------------------------                   ----------------------
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//                                                master leader stepdown
TEST_P(PgIndexBackfillMultiMaster, MasterLeaderStepdown) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    // The CREATE INDEX should get master leader change during backfill. We expect
    // that the new master will continue the backfill, and it should succeed.
    ASSERT_OK(create_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName));
    ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(
        kIndexName, IndexStateFlags{
                        IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady,
                        IndexStateFlag::kIndIsValid})));
  });
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin master leader stepdown thread";
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    LOG(INFO) << "Doing master leader stepdown";
    tserver::TabletServerErrorPB::Code error_code;
    ASSERT_OK(cluster_->StepDownMasterLeader(&error_code));

    // It should still be in the backfill stage.
    ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(
        kIndexName, IndexStateFlags{IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady})));

    // Unblock DoBackfill.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });
  thread_holder_.JoinAll();
}

// Override the index backfill test class to use colocated tables.
class PgIndexBackfillColocated : public PgIndexBackfillTest {
 public:
  void SetUp() override {
    LibPqTestBase::SetUp();

    PGConn conn_init = ASSERT_RESULT(Connect());
    ASSERT_OK(conn_init.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", kColoDbName));

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kColoDbName)));
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillColocated, ::testing::Bool());

// Make sure that backfill works when colocation is on.
TEST_P(PgIndexBackfillColocated, ColocatedSimple) {
  TestSimpleBackfill();
}

// Make sure that backfill works when there are multiple colocated tables.
TEST_P(PgIndexBackfillColocated, ColocatedMultipleTables) {
  // Create two tables with the index on the second table.
  const std::string kOtherTable = "yyy";
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kOtherTable));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (100)", kOtherTable));

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (200)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (300)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 (i ASC)", kTableName));

  // Index scan to verify contents of index table.
  const std::string query = Format("SELECT COUNT(*) FROM $0 WHERE i > 0", kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
  auto count = ASSERT_RESULT(conn_->FetchRow<PGUint64>(query));
  ASSERT_EQ(count, 2);
}

// Test that retain_delete_markers is properly set after index backfill for a colocated table.
TEST_P(PgIndexBackfillColocated, ColocatedRetainDeleteMarkers) {
  TestRetainDeleteMarkers(kColoDbName);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/19731.
TEST_P(PgIndexBackfillColocated, ColocatedRetainDeleteMarkersRecovery) {
  TestRetainDeleteMarkersRecovery(kColoDbName, false /* use_multiple_requests */);
}
TEST_P(PgIndexBackfillColocated, ColocatedRetainDeleteMarkersRecoveryViaSeveralRequests) {
  TestRetainDeleteMarkersRecovery(kColoDbName, true /* use_multiple_requests */);
}

// Verify in-progress CREATE INDEX command's entry in pg_stat_progress_create_index.
TEST_P(PgIndexBackfillBlockIndisreadyAndDoBackfill, PgStatProgressCreateIndexPhase) {
  constexpr int kNumRows = 10;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))", kTableName, kNumRows));

  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    auto create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(create_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));
  });

  ASSERT_OK(WaitForIndexProgressOutput(kPhase, kPhaseInitializing));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));
  ASSERT_OK(WaitForIndexProgressOutput(kPhase, kPhaseBackfilling));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  thread_holder_.Stop();
}

// Verify in-progress CREATE INDEX command's entries are only partially visible to users that
// do not have the appropriate role membership.
TEST_P(PgIndexBackfillBlockDoBackfill, PgStatProgressCreateIndexPermissions) {
  constexpr int kNumRows = 10;
  constexpr auto kUserOne = "user1";
  constexpr auto kUserTwo = "user2";

  ASSERT_OK(conn_->ExecuteFormat("CREATE USER $0", kUserOne));
  ASSERT_OK(conn_->ExecuteFormat("CREATE USER $0", kUserTwo));
  ASSERT_OK(conn_->ExecuteFormat("GRANT CREATE ON SCHEMA public TO $0", kUserOne));

  auto user_one_read_conn = ASSERT_RESULT(ConnectToDBAsUser(kDatabaseName, kUserOne));
  auto user_two_read_conn = ASSERT_RESULT(ConnectToDBAsUser(kDatabaseName, kUserTwo));

  ASSERT_OK(user_one_read_conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(user_one_read_conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))", kTableName, kNumRows));

  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    auto user_one_create_index_conn = ASSERT_RESULT(ConnectToDBAsUser(kDatabaseName, kUserOne));
    ASSERT_OK(user_one_create_index_conn.ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName));
  });

  ASSERT_OK(WaitForIndexProgressOutput(kPhase, kPhaseBackfilling));
  // Assert that the new user that isn't a superuser but the owner can see the values
  // for the selected columns.
  constexpr auto query = "SELECT relid, command, phase, tuples_done, tuples_total"
      " FROM pg_stat_progress_create_index";
  auto fetch_nulls = [](PGConn* conn) -> Result<std::tuple<bool, bool, bool, bool, bool>> {
    const auto values = VERIFY_RESULT((conn->FetchRow<
        std::optional<PGOid>, std::optional<std::string>, std::optional<std::string>,
        std::optional<int64_t>, std::optional<int64_t>>(query)));
    return std::apply(
        [](const auto&... args) { return std::make_tuple(!args.has_value()...); },
        values);
  };
  auto nulls = ASSERT_RESULT(fetch_nulls(&user_one_read_conn));
  auto expected_nulls =
      decltype(nulls){false, false, false, false, false};
  // Assert that superuser can see the values for the selected columns.
  ASSERT_EQ(nulls, expected_nulls);
  nulls = ASSERT_RESULT(fetch_nulls(conn_.get()));
  ASSERT_EQ(nulls, expected_nulls);
  // Assert that the new user that isn't a superuser or the owner cannot see the values
  // for the selected columns.
  nulls = ASSERT_RESULT(fetch_nulls(&user_two_read_conn));
  expected_nulls =
      decltype(nulls){true, true, true, true, true};
  ASSERT_EQ(nulls, expected_nulls);
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  thread_holder_.Stop();
}

// Verify in-progress CREATE INDEX command's entry in pg_stat_progress_create_index is only
// visible to the local node.
TEST_P(PgIndexBackfillBlockDoBackfill, PgStatProgressCreateIndexMultiNode) {
  constexpr int kNumRows = 10;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))", kTableName, kNumRows));

  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    auto create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(create_conn.ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName));
  });

  // Verify that entry is visible to local node (if it isn't this WaitFor will time-out).
  ASSERT_OK(WaitForIndexProgressOutput(kPhase, kPhaseBackfilling));
  // Connect to a different node.
  pg_ts = cluster_->tablet_server(1);
  auto different_node_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  // Verify that the entry is not visible on this node.
  auto res = ASSERT_RESULT(different_node_conn.Fetch(
      "SELECT * FROM pg_stat_progress_create_index"));
  ASSERT_EQ(PQntuples(res.get()), 0);
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  thread_holder_.Stop();
}

// Verify in-progress CREATE INDEX command's entry's "tuples_done" field in
// pg_stat_progress_create_index is stable and returns the same values for multiple
// calls within the same transaction.
TEST_P(PgIndexBackfillBlockDoBackfill, PgStatProgressCreateIndexCheckVolatility) {
  constexpr int kNumRows = 10;

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))", kTableName, kNumRows));

  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    auto create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(create_conn.ExecuteFormat(
        "CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));
  });

  ASSERT_OK(WaitForIndexProgressOutput(kPhase, kPhaseBackfilling));
  ASSERT_OK(conn_->ExecuteFormat("BEGIN"));
  // Get number of tuples done.
  constexpr auto index_progress_query =
      "SELECT phase, tuples_done FROM pg_stat_progress_create_index";
  // Assert that the number of tuples done is 0 (as we have blocked the backfill).
  auto res = ASSERT_RESULT(conn_->Fetch(index_progress_query));
  ASSERT_EQ(ASSERT_RESULT(GetValue<PGUint64>(res.get(), 0, 1)), 0);
  // Unblock backfill to change the actual number of tuples done.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  thread_holder_.JoinAll();
  // Assert that the phase is still "backfilling" and that the number of tuples done is still 0
  // within this txn.
  res = ASSERT_RESULT(conn_->Fetch(index_progress_query));
  ASSERT_EQ(ASSERT_RESULT(GetValue<std::string>(res.get(), 0, 0)), kPhaseBackfilling);
  ASSERT_EQ(ASSERT_RESULT(GetValue<PGUint64>(res.get(), 0, 1)), 0);
  ASSERT_OK(conn_->ExecuteFormat("COMMIT"));
}

// Verify in-progress CREATE INDEX commands' entries in pg_stat_progress_create_index
// for concurrent gin, partial, include indexes and non-concurrent indexes.
TEST_P(PgIndexBackfillTest,
       YB_DISABLE_TEST_IN_TSAN(PgStatProgressCreateIndexCheckIndexTypes)) {
  constexpr int64_t kNumRows = 100;
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, t text, v tsvector)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1), 'a', to_tsvector('simple', 'filler'))",
      kTableName, kNumRows));
  ASSERT_OK(conn_->ExecuteFormat("ANALYZE $0", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(conn_->FetchRow<float>(
        Format("SELECT reltuples FROM pg_class WHERE relname='$0'", kTableName))),
      kNumRows);

  const std::array<std::string, 4> create_index_stmts = {
      Format("CREATE INDEX CONCURRENTLY ON $0 USING gin (v)", kTableName),
      Format("CREATE INDEX CONCURRENTLY ON $0 (i) WHERE i < 50", kTableName),
      Format("CREATE INDEX CONCURRENTLY ON $0 (i) INCLUDE (t)", kTableName),
      Format("CREATE INDEX NONCONCURRENTLY ON $0 (i)", kTableName)
  };
  constexpr auto cols = "datname, phase, command, tuples_total, tuples_done";

  for (auto& stmt : create_index_stmts) {
    ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "postbackfill"));
    thread_holder_.AddThreadFunctor([this, &stmt] {
      LOG(INFO) << "Begin create thread";
      auto create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
      ASSERT_OK(create_conn.Execute(stmt));
    });
    bool nonconcurrently = false;
    if (stmt.find("NONCONCURRENTLY") != std::string::npos) {
      nonconcurrently = true;
    }
    ASSERT_OK((WaitForIndexProgressOutput(
        cols,
        std::make_tuple(kDatabaseName, kPhaseBackfilling,
                        nonconcurrently ? kCommandNonconcurrently : kCommandConcurrently,
                        kNumRows, kNumRows))));
    ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));
    thread_holder_.JoinAll();
  }
}

// Verify in-progress CREATE INDEX commands' entries in pg_stat_progress_create_index
// for partitioned indexes.
TEST_P(PgIndexBackfillTest,
       YB_DISABLE_TEST_IN_TSAN(PgStatProgressCreateIndexPartitioned)) {
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "backfill"));
  constexpr int64_t kNumPartitions = 3;
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int) PARTITION BY RANGE(i)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
    "CREATE TABLE $0_1 PARTITION OF $0 FOR VALUES FROM (1) TO (31)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0_2 PARTITION OF $0 FOR VALUES FROM (31) TO (61)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0_3 PARTITION OF $0 DEFAULT", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (generate_series(1, 90))", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("ANALYZE $0", kTableName));

  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    auto create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    // Note: concurrent create index on partitioned tables is not supported - we build
    // the index non-concurrently.
    ASSERT_OK(create_conn.ExecuteFormat(
        "CREATE INDEX NONCONCURRENTLY $0 ON $1 (i ASC)", kIndexName, kTableName));
  });

  constexpr auto cols =
      "datname, command, phase, tuples_done, tuples_total, partitions_total, partitions_done";
  // Verify entries for the partitioned indexes.
  for (int64_t i = 0; i < kNumPartitions; ++i) {
    ASSERT_OK((WaitForIndexProgressOutput(
        cols,
        std::make_tuple(kDatabaseName, kCommandNonconcurrently, kPhaseInitializing,
                        static_cast<int64_t>(0), static_cast<int64_t>(30), kNumPartitions, i))));
    ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "postbackfill"));
    ASSERT_OK(WaitForIndexProgressOutput(
        cols,
        std::make_tuple(kDatabaseName, kCommandNonconcurrently, kPhaseBackfilling,
                        static_cast<int64_t>(30), static_cast<int64_t>(30), kNumPartitions, i)));
    ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "backfill"));
  }
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));
  thread_holder_.JoinAll();
}

// Verify in-progress CREATE INDEX command's entry in pg_stat_progress_create_index
// for an index created in a different database.
TEST_P(PgIndexBackfillBlockDoBackfill, PgStatProgressCreateIndexDifferentDatabase) {
  constexpr int64_t kNumRows = 10;
  const auto kTestDatabaseName = "test_db"s;

  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0", kTestDatabaseName));
  auto new_db_conn = ASSERT_RESULT(ConnectToDB(kTestDatabaseName));
  ASSERT_OK(new_db_conn.ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  ASSERT_OK(new_db_conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1, $1))", kTableName, kNumRows));
  ASSERT_OK(new_db_conn.ExecuteFormat("ANALYZE $0", kTableName));
  ASSERT_EQ(
      ASSERT_RESULT(new_db_conn.FetchRow<float>(
        Format("SELECT reltuples FROM pg_class WHERE relname='$0'", kTableName))),
      kNumRows);

  thread_holder_.AddThreadFunctor([&new_db_conn] {
    LOG(INFO) << "Begin create thread";
    ASSERT_OK(new_db_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i ASC)", kIndexName, kTableName));
  });

  constexpr auto cols = "datname, phase, command, tuples_total, tuples_done";
  ASSERT_OK(WaitForIndexProgressOutput(
      cols,
      std::make_tuple(kTestDatabaseName, kPhaseBackfilling, kCommandConcurrently, kNumRows,
                      static_cast<int64_t>(0))));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  thread_holder_.Stop();
}

// Simulate the following:
//   Session A                                    Session B
//   ------------------------------------         -------------------------------------------
//   CREATE TABLE (a, b, PRIMARY KEY (a))
//   INSERT (1, 1)
//   CREATE UNIQUE INDEX (b)
//   - indislive
//   - indisready
//   - backfill stage
//     - get safe time for read
//                                                DELETE (1, 1) -- possibly as a txn batch
//                                                INSERT (3, 1) -- possibly as a txn batch
//     - do the actual backfill
//       (sees (1, 1) from before safe time)
//       (sees (3, 1) from after safe time)
//       (both have b=1 -> duplicate key error)
// This test verifies that backfill correctly detects duplicate values when:
// 1. A row with value 'b=1' exists before the safe time
// 2. That row is deleted after the safe time
// 3. A new row with the same value 'b=1' is inserted after the safe time
// The backfill will see both rows and should fail with a duplicate key error.
void PgIndexBackfillBlockDoBackfill::TestDeleteAndInsertSameValue(bool use_single_txn) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (a int PRIMARY KEY, b int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 1)", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    LOG(INFO) << "Creating unique index...";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(create_conn.ExecuteFormat(
        "CREATE UNIQUE INDEX $0 ON $1 (b ASC)", kIndexName, kTableName));
  });
  thread_holder_.AddThreadFunctor([this, use_single_txn] {
    LOG(INFO) << "Begin write thread";
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    LOG(INFO) << "Delete row (1, 1) and insert row (3, 1)"
              << (use_single_txn ? " in a single transaction" : " as separate statements");
    if (use_single_txn) {
      ASSERT_OK(conn_->Execute("BEGIN"));
    }
    ASSERT_OK(conn_->ExecuteFormat("DELETE FROM $0 WHERE a = 1", kTableName));
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (3, 1)", kTableName));
    if (use_single_txn) {
      ASSERT_OK(conn_->Execute("COMMIT"));
    }

    // It should still be in the backfill stage.
    ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(
        kIndexName, IndexStateFlags{IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady})));

    // Unblock CREATE INDEX waiting to do backfill.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });
  thread_holder_.JoinAll();

  // Verify the table has only the new row.
  auto result = conn_->FetchRow<int32_t>(Format(
      "SELECT a FROM $0 WHERE b = 1", kTableName));
  ASSERT_OK(result);
  ASSERT_EQ(*result, 3);
}

// DELETE and INSERT as separate statements (different HTs).
TEST_P(PgIndexBackfillBlockDoBackfill, DeleteAndInsertSameValueSeparateStmts) {
  TestDeleteAndInsertSameValue(/* use_single_txn= */ false);
}

// Table starts with multiple rows having the same indexed value b=1:
//   (1, 1), (2, 1), (3, 1)
// After backfill safe time, we delete one of them (3, 1), but the remaining
// rows (1, 1) and (2, 1) still have duplicate b=1. The CREATE UNIQUE INDEX
// should fail because the backfill encounters the duplicate values.
TEST_P(PgIndexBackfillBlockDoBackfill, DuplicatesExistBeforeBackfill) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (a int PRIMARY KEY, b int)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 1), (2, 1), (3, 1)", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    auto status = create_conn.ExecuteFormat(
        "CREATE UNIQUE INDEX $0 ON $1 (b ASC)", kIndexName, kTableName);
    // The CREATE INDEX should fail due to duplicate key on b=1.
    ASSERT_NOK(status);
    ASSERT_TRUE(status.message().ToBuffer().find("duplicate") != std::string::npos)
        << "Expected duplicate key error, got: " << status;
  });
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin write thread";
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    // Delete one of the duplicates, but two remain.
    LOG(INFO) << "Delete row (3, 1)";
    ASSERT_OK(conn_->ExecuteFormat("DELETE FROM $0 WHERE a = 3", kTableName));

    // It should still be in the backfill stage.
    ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(
        kIndexName, IndexStateFlags{IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady})));

    // Unblock CREATE INDEX waiting to do backfill.
    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });
  thread_holder_.JoinAll();
}

// Override to use YSQL backends manager.
class PgIndexBackfillBackendsManager : public PgIndexBackfillBlockDoBackfill {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillBlockDoBackfill::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
        "--ysql_yb_disable_wait_for_backends_catalog_version=false");
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillBackendsManager, ::testing::Bool());

// Make sure transaction is not aborted by getting safe time.  Simulate the following:
//   Session A                                    Session B
//   --------------------------                   ---------------------------------
//   CREATE INDEX
//   - indislive
//   - indisready
//                                                BEGIN
//                                                UPDATE a row of the indexed table
//   - backfill
//     - get safe time for read
//                                                COMMIT
//     - do the actual backfill
//   - indisvalid
// TODO(#19000): enable for TSAN.
TEST_P(PgIndexBackfillBackendsManager, YB_DISABLE_TEST_IN_TSAN(NoAbortTxn)) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int PRIMARY KEY, j int) SPLIT INTO 1 TABLETS",
                                 kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 2), (3, 4)", kTableName));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "backfill"));

  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(create_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (j ASC)", kIndexName, kTableName));
  });
  ASSERT_OK(WaitForIndexStateFlags(
      IndexStateFlags{IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady}));
  // Reset connection to eliminate cache/heartbeat-delay issues of indislive=t, indisready=t.
  conn_->Reset();

  LOG(INFO) << "Begin txn";
  ASSERT_OK(conn_->Execute("BEGIN"));
  ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET j = 5 WHERE i = 3", kTableName));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));
  ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));
  ASSERT_OK(conn_->Execute("COMMIT"));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  thread_holder_.Stop();

  LOG(INFO) << "Validate data";
  const std::string query = Format("SELECT * FROM $0 ORDER BY j", kTableName);
  ASSERT_OK(WaitForIndexScan(query));
  auto rows = ASSERT_RESULT((conn_->FetchRows<int32_t, int32_t>(query)));
  ASSERT_EQ(rows, (decltype(rows){{1, 2}, {3, 5}}));
}

class PgIndexBackfillReadCommittedBlockIndislive : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--yb_enable_read_committed_isolation=true");
    options->extra_tserver_flags.push_back("--ysql_yb_test_block_index_phase=indislive");
  }
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillReadCommittedBlockIndislive, ::testing::Bool());

// Test for https://github.com/yugabyte/yugabyte-db/issues/24313
// Verify that concurrent updates do not leave phantom entries in the index
// Phantom entries were created because inplace index update function did not check if the index
// was ready. The index build procedure wait until "old" transactions (those who don't know about
// the index) are completed before proceeding to "ready" state because update by old transaction may
// prevent proper index update. This happens as follows ("new" transaction thinks the index is
// ready, "old" transaction thinks the index does not exist):
// - "new" transaction changes value in indexed column of record R from 'a' to 'b'.
//   Index key is changed from 'a' to 'b'. Index is being built, the 'a' key may not exist yet, in
//   this case new key is created.
// - "old" transaction changes value in indexed column of record R from 'b' to 'c'.
//   Index key is not updated, it remains 'b'.
// - "new" transaction changes value in indexed column of record R from 'c' to 'd'.
//   The transaction attempts to change the index key from 'c' to 'd'. The key 'c' does not exist
//   in the index, so new key is created. Now index has two keys, 'b' and 'd' pointing to the same
//   record R.
// If index readiness is not checked, any transaction aware of the index acts as "new".
TEST_P(PgIndexBackfillReadCommittedBlockIndislive, PhantomIdxEntry) {
  constexpr int kNumRows = 10;
  const IndexStateFlags index_live_flags{IndexStateFlag::kIndIsLive};
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int, t text)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (generate_series(1, $1), 'a')",
                                 kTableName, kNumRows));
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create index thread";
    auto create_idx_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(create_idx_conn.ExecuteFormat("CREATE INDEX $0 ON $1(t)", kIndexName, kTableName));
    LOG(INFO) << "Create index thread has been completed";
  });
  // There's no reliable indicator that index build has stopped before 'indislive' phase, just give
  // the index creation thread some extra time.
  SleepFor(RegularBuildVsSanitizers(5s, 60s));
  auto other_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_FALSE(ASSERT_RESULT(IsAtTargetIndexStateFlags(kIndexName, IndexStateFlags{})));
  LOG(INFO) << "Begin older txn";
  ASSERT_OK(other_conn.Execute("BEGIN"));
  const std::string query = Format("SELECT t FROM $0 WHERE i = $1", kTableName, 1);
  auto rows = ASSERT_RESULT((other_conn.FetchRows<std::string>(query)));
  ASSERT_EQ(rows, (decltype(rows){{"a"}}));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "indisready"));
  ASSERT_OK(WaitForIndexStateFlags(index_live_flags, kIndexName));
  LOG(INFO) << "Update record by newer txn";
  ASSERT_OK(LoggedWaitFor(
      [this]() -> Result<bool> {
        auto s = conn_->ExecuteFormat("UPDATE $0 SET t = 'b' WHERE i = $1", kTableName, 2);
        if (s.ok()) {
          return true;
        }
        SCHECK_STR_CONTAINS(s.message().ToBuffer(), "schema version mismatch");
        return false;
      },
      RegularBuildVsSanitizers(10s, 30s),
      "wait for DML to succeed, ignoring potential schema version mismatch"));
  LOG(INFO) << "Update record by older txn";
  ASSERT_OK(other_conn.ExecuteFormat("UPDATE $0 SET t = 'c' WHERE i = $1", kTableName, 2));
  ASSERT_OK(other_conn.Execute("COMMIT"));
  LOG(INFO) << "Update record by newer txn again";
  ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET t = 'd' WHERE i = $1", kTableName, 2));
  ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(kIndexName, index_live_flags)));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));
  thread_holder_.Stop();
  const std::string count_query = Format("SELECT count(t) FROM $0 WHERE t = 'b'", kTableName);
  ASSERT_OK(WaitForIndexScan(count_query));
  LOG(INFO) << "Check for phantom record";
  auto idx_count = ASSERT_RESULT((conn_->FetchRow<PGUint64>(count_query)));
  ASSERT_EQ(idx_count, 0);
}

class PgIndexBackfillReadCommittedBlockIndisliveBlockDoBackfill :
  public PgIndexBackfillReadCommittedBlockIndislive {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillReadCommittedBlockIndislive::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--TEST_block_do_backfill=true");
  }
};

// TODO(bkolagani): The test relies on transparent query layer retries to succeed. Until
// GHI#24877 is addressed, retries are disabled when table locking feature in on. Run the
// test with table locking enabled after enabling back statement retries.
INSTANTIATE_TEST_CASE_P(,
                        PgIndexBackfillReadCommittedBlockIndisliveBlockDoBackfill,
                        ::testing::Values(false));

// Make sure backends wait for catalog version waits on the correct version and ignores the backend
// running the CREATE INDEX.
//
// Buggy behavior:
// 1. A: CREATE INDEX
// 1. B: other catver bumps
// 1. B: BEGIN
// 1. A: indislive
// 1. A: indisready
// 1. A: backfill get safe time (since this happens before the DELETE, the to-be-deleted row is
//       backfilled)
// 1. B: DELETE (since this happens with zero index permissions, no DELETE is sent to the index)
// 1. B: COMMIT
//
// Correct behavior:
// 1. A: CREATE INDEX
// 1. B: other catver bumps
// 1. B: BEGIN
// 1. A: indislive
// 1. B: DELETE
// 1. B: COMMIT
// 1. A: indisready
// 1. A: backfill get safe time
TEST_P(PgIndexBackfillReadCommittedBlockIndisliveBlockDoBackfill, CatVerBumps) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int PRIMARY KEY)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName));

  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create index thread";
    auto create_idx_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    // We don't want the catalog version increments to conflict with the FOR UPDATE lock on the
    // catalog version row.
    ASSERT_OK(create_idx_conn.Execute("SET yb_force_early_ddl_serialization=false"));
    ASSERT_OK(create_idx_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName));
    LOG(INFO) << "End create index thread";
  });

  ASSERT_OK(EnsureClientCreated());
  const std::string table_id = ASSERT_RESULT(GetTableIdByTableName(
      client_.get(), kDatabaseName, kTableName));
  std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
  ASSERT_OK(LoggedWaitFor(
      [this]() {
        return client_->TableExists(client::YBTableName(
            YQL_DATABASE_PGSQL, kDatabaseName, kIndexName));
      },
      30s,
      "wait for index to exist"));

  // Need 2 catalog version bumps to make the following DML connection at a catalog version >= the
  // CREATE INDEX's local catalog version, at least until backfill happens.
  ASSERT_OK(BumpCatalogVersion(2, conn_.get()));
  // Ensure we get the new catalog version by resetting connection.  This also clears the cached
  // table schema version.
  conn_->Reset();
  ASSERT_OK(conn_->Execute("BEGIN"));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));
  // If tservers have table schema version 3 but master has table schema version 2, that causes
  // schema version mismatch for DMLs.  Avoid that situation by not doing DMLs until the table
  // schema version stabilizes to 3.
  // 0 -> 1: add index to table
  // 1 -> 2: revert that because committing indislive fails on conflict with pg_yb_catalog_table
  // bumping catalog version when above BumpCatalogVersion also bumps it
  // 2 -> 3: add index to table (retry)
  ASSERT_OK(LoggedWaitFor(
      [this, table_info, &table_id]() -> Result<bool> {
        Synchronizer sync;
        RETURN_NOT_OK(client_->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
        RETURN_NOT_OK(sync.Wait());
        constexpr uint32_t kTargetVersion = 3;
        SCHECK_LE(table_info->schema.version(), kTargetVersion,
                  IllegalState,
                  "Unexpected schema version");
        return table_info->schema.version() == kTargetVersion;
      },
      60s,
      "wait for table to register index"));
  // Wait for index permission state changes to possibly go through (for the buggy case to fail).
  SleepFor(RegularBuildVsDebugVsSanitizers(5s, 30s, 60s));
  // It is unexpected for the index to be in backfill mode, let alone having committed indisready
  // permission.
  LOG_IF(WARNING,
         ASSERT_RESULT(IsAtTargetIndexStateFlags(kIndexName,
                                                 IndexStateFlags{IndexStateFlag::kIndIsLive,
                                                                 IndexStateFlag::kIndIsReady})))
      << "incorrectly advanced to indisready permission";
  ASSERT_OK(conn_->ExecuteFormat("DELETE FROM $0", kTableName));
  ASSERT_OK(conn_->Execute("COMMIT"));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  thread_holder_.JoinAll();

  const std::string query = Format("/*+IndexOnlyScan($0 $1)*/ SELECT count(*) FROM $0 WHERE i = 1",
                                   kTableName, kIndexName);
  ASSERT_OK(WaitForIndexScan(query));
  ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<PGUint64>(query)), 0);
}

class PgIndexBackfill1kRowsPerSec : public PgIndexBackfillTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
        Format("--backfill_index_write_batch_size=$0", kBackfillRateRowsPerSec));
    options->extra_tserver_flags.push_back(
        Format("--backfill_index_rate_rows_per_sec=$0", kBackfillRateRowsPerSec));
  }

 protected:
  static constexpr auto kBackfillRateRowsPerSec = 1000;
  static constexpr auto kNumRows = 10000;
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfill1kRowsPerSec, ::testing::Bool());

// Test for GH Issue (#25250):
TEST_P(PgIndexBackfill1kRowsPerSec, ConcurrentDelete) {
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (k int, v int, PRIMARY KEY (k ASC)) split at values "
      "(($1))", kTableName, kNumRows+1));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 (k, v) SELECT i, i FROM generate_series(1, $1) AS i;", kTableName, kNumRows));

  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create index thread";
    auto create_idx_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    ASSERT_OK(create_idx_conn.ExecuteFormat(
        "CREATE INDEX $0 ON $1 (v) SPLIT INTO 10 TABLETS", kIndexName, kTableName));
    LOG(INFO) << "End create index thread";
  });

  ASSERT_OK(EnsureClientCreated());
  ASSERT_OK(WaitFor(
      [this]() {
        return client_->TableExists(client::YBTableName(
            YQL_DATABASE_PGSQL, kDatabaseName, kIndexName));
      },
      30s,
      "wait for index to exist"));
  ASSERT_OK(WaitForIndexProgressOutput(kPhase, kPhaseBackfilling));
  LOG(INFO) << "Index backfill has started";

  // Perform a DELETE half way into the backfill. This way the DELETE happens after a backfill
  // write time is chosen, but before the row 9999 is backfilled.
  SleepFor(MonoDelta::FromSeconds(kNumRows / (2 * kBackfillRateRowsPerSec)));
  ASSERT_OK(conn_->ExecuteFormat("DELETE FROM $0 WHERE k=$1", kTableName, kNumRows));
  LOG(INFO) << "Deleted row";

  // Ensure that we haven't yet finished backfilling the last row.
  const std::string& index_progress_query =
    "SELECT phase, tuples_done FROM pg_stat_progress_create_index";
  auto res = ASSERT_RESULT(conn_->Fetch(index_progress_query));
  ASSERT_EQ(ASSERT_RESULT(GetValue<std::string>(res.get(), 0, 0)), kPhaseBackfilling);
  auto tuples_backfilled = ASSERT_RESULT(GetValue<PGUint64>(res.get(), 0, 1));
  ASSERT_LT(tuples_backfilled, kNumRows);

  // Use an index only scan to ensure that the row with key kNumRows is absent in the index.
  // If the backfilled index entry for this key has a write time higher than the DELETE above,
  // the row would be present in the index and show up in an index only scan.
  const std::string query = Format("/*+IndexOnlyScan($0 $1) */ SELECT count(*) FROM $0 WHERE v=$2",
                                   kTableName, kIndexName, kNumRows);
  ASSERT_OK(WaitForIndexScan(query));
  ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<PGUint64>(query)), 0);
  thread_holder_.JoinAll();
}

class PgIndexBackfillReadBeforeConcurrentUpdate : public PgIndexBackfillBlockDoBackfill {
 public:
  void SetUp() override {
    PgIndexBackfillBlockDoBackfill::SetUp();

    ASSERT_OK(conn_->ExecuteFormat(
        "CREATE TABLE $0 (i int UNIQUE, j int)", kTableName));
    ASSERT_OK(conn_->ExecuteFormat(
        "INSERT INTO $0 (SELECT i, i FROM generate_series(1, 5) AS i)", kTableName));
  }

  void CreateIndexSimultaneously(
      const std::string& index_name, const std::string& index_definition) {
    thread_holder_.AddThreadFunctor([this, index_name, index_definition] {
      LOG(INFO) << "Begin create index " << index_name;
      PGConn index_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
      ASSERT_OK(index_conn.Execute(index_definition));
      LOG(INFO) << "Done create index " << index_name;
    });
  }

  void CreateExpressionIndexSimultaneously(const std::string& index_name) {
    auto index_definition = Format(
        "CREATE INDEX $0 ON $1 ((j * j) ASC)", index_name, kTableName);
    CreateIndexSimultaneously(index_name, index_definition);
  }

  void CreatePartialIndexSimultaneously(const std::string& index_name) {
    auto index_definition = Format(
        "CREATE INDEX $0 ON $1 (j ASC) WHERE j > 2", index_name, kTableName);
    CreateIndexSimultaneously(index_name, index_definition);
  }
 protected:
  const CoarseDuration kThreadWaitTime = 60s;
  const IndexStateFlags index_state_flags =
      IndexStateFlags {IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady};
};

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillReadBeforeConcurrentUpdate, ::testing::Bool());

// Simulate the following:
//   Session A                                    Session B
//   ------------------------------------         -------------------------------------------
//   CREATE TABLE (i UNIQUE, j)
//   INSERT (i, i) for i in [1..5]
//
//   CREATE INDEX ( f(j) ) -- expr index
//   - indislive=true
//   - indisready=true
//   - backfill stage
//     - get safe time for read
//                                                UPDATE j = j + 100 WHERE i = 1
//                                                (DELETE (1, i=1) from index)
//                                                (INSERT (10201, i=1) to index)
//     - do the actual backfill
//       (insert (1, i=1) to index)
//   - indisvalid=true
TEST_P(PgIndexBackfillReadBeforeConcurrentUpdate, ExpressionIndex) {
  const string kExprIndex = "idx_expr_j";
  std::atomic<int> updates(0);

  // Initiate creation of the expression index that will block after acquiring backfill safe time.
  CreateExpressionIndexSimultaneously(kExprIndex);

  thread_holder_.AddThreadFunctor([this, &updates, &kExprIndex] {
    LOG(INFO) << "Begin write thread";
    ASSERT_OK(WaitForIndexStateFlags(index_state_flags, kExprIndex));
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    // DELETE (j=1) from the index
    // INSERT (j=10201) into the index
    LOG(INFO) << "running UPDATE on i = 1";
    ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET j = j + 100 WHERE i = 1", kTableName));
    updates++;
  });

  LOG(INFO) << "Unblock backfill...";
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));

  thread_holder_.WaitAndStop(kThreadWaitTime);
  ASSERT_EQ(updates.load(std::memory_order_acquire), 1);

  ASSERT_OK(CheckIndexConsistency(kExprIndex));
}

// A similar test to the ExpressionIndex above, but for partial indexes.
TEST_P(PgIndexBackfillReadBeforeConcurrentUpdate, PartialIndex) {
  const string kPartialIndex = "idx_partial_j";
  std::atomic<int> updates(0);
  int key = 2;

  // Initiate creation of the partial index that will block after acquiring backfill safe time.
  CreatePartialIndexSimultaneously(kPartialIndex);

  thread_holder_.AddThreadFunctor([this, &key, &updates, &kPartialIndex] {
    LOG(INFO) << "Begin write thread";
    ASSERT_OK(WaitForIndexStateFlags(index_state_flags, kPartialIndex));
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    // Case 1: INSERT (j=102) into the index
    LOG(INFO) << "running UPDATE on i = " << key;
    ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET j = j + 100 WHERE i = $1", kTableName, key));
    key++;
    updates++;

    // Case 2: INSERT (j=103) into the index
    //         DELETE (j=3) from the index
    LOG(INFO) << "running UPDATE on i = " << key;
    ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET j = j + 100 WHERE i = $1", kTableName, key));
    key++;
    updates++;

    // Case 3: DELETE (j=4) from the index
    LOG(INFO) << "running UPDATE on i = " << key;
    ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET j = j - 100 WHERE i = $1", kTableName, key));
    key++;
    updates++;
  });

  LOG(INFO) << "Unblock backfill...";
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));

  thread_holder_.WaitAndStop(kThreadWaitTime);
  ASSERT_EQ(updates.load(std::memory_order_acquire), 3);

  ASSERT_OK(CheckIndexConsistency(kPartialIndex));
}

class PgIndexBackfillIgnoreApplyTest : public PgIndexBackfillTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);

    options->extra_master_flags.push_back("--TEST_colocation_ids=1000,3000,2000");

    options->extra_tserver_flags.push_back("--TEST_transaction_ignore_applying_probability=1.0");
  }
};

TEST_P(PgIndexBackfillIgnoreApplyTest, Backward) {
  const std::string kDbName = "colodb";
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0 with COLOCATION = true", kDbName));
  auto conn = ASSERT_RESULT(ConnectToDB(kDbName));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t2 (k INT, PRIMARY KEY (k ASC))"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test (k INT, v INT, PRIMARY KEY (k ASC))"));
  ASSERT_OK(conn.Execute("INSERT INTO t2 VALUES (48)"));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (11, 99)"));
  ASSERT_OK(conn.CommitTransaction());

  ASSERT_OK(conn.ExecuteFormat("CREATE UNIQUE INDEX idx ON test (v ASC)"));
}

INSTANTIATE_TEST_CASE_P(, PgIndexBackfillIgnoreApplyTest, ::testing::Bool());

} // namespace yb::pgwrapper

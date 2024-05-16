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
#include "yb/master/master_error.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
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

} // namespace

YB_DEFINE_ENUM(IndexStateFlag, (kIndIsLive)(kIndIsReady)(kIndIsValid));
typedef EnumBitSet<IndexStateFlag> IndexStateFlags;

class PgIndexBackfillTest : public LibPqTestBase {
 public:
  void SetUp() override {
    LibPqTestBase::SetUp();

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--ysql_disable_index_backfill=false");
    options->extra_master_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));
    options->extra_tserver_flags.push_back("--ysql_disable_index_backfill=false");
    options->extra_tserver_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));
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

  const int kTabletsPerServer = 8;

  std::unique_ptr<PGConn> conn_;
  TestThreadHolder thread_holder_;

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
        30s,
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
TEST_F(PgIndexBackfillTest, Simple) {
  TestSimpleBackfill();
}

TEST_F(PgIndexBackfillTest, WaitForSplitsToComplete) {
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
TEST_F(PgIndexBackfillTest, Partial) {
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
TEST_F(PgIndexBackfillTest, Expression) {
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
TEST_F(PgIndexBackfillTest, Unique) {
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
TEST_F(PgIndexBackfillTest, NestedDdl) {
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
TEST_F(PgIndexBackfillTest, Drop) {
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
TEST_F(PgIndexBackfillTest, NonexistentDelete) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int PRIMARY KEY)", kTableName));

  // Delete to nonexistent row should return no rows.
  const auto values = ASSERT_RESULT(conn_->FetchRows<int32_t>(
      Format("DELETE FROM $0 WHERE i = 1 RETURNING i", kTableName)));
  ASSERT_TRUE(values.empty());
}

// Make sure that index backfill on large tables backfills all data.
TEST_F(PgIndexBackfillTest, Large) {
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

TEST_F_EX(PgIndexBackfillTest, InsertsWhileCreatingIndexEnableWait, PgIndexBackfillTestEnableWait) {
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

TEST_F_EX(
    PgIndexBackfillTest,
    InsertsWhileCreatingIndexDisableWait,
    PgIndexBackfillTestDisableWait) {
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

// Set batch size and prefetch limit such that:
// Each tablet requires multiple RPC calls from the master to complete backfill.
//     Also, set the ysql_prefetch_size small to ensure that each of these
//     `BACKFILL INDEX` calls will fetch data from the tserver at least 2 times.
// Fetch metrics to ensure that there have been > num_tablets rpc's.
TEST_F_EX(
    PgIndexBackfillTest, BackfillInChunks, PgIndexBackfillTestChunking) {
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

// Set the backfill batch size and backfill rate
// Check that the time taken to backfill is no less than what is expected.
TEST_F_EX(
    PgIndexBackfillTest, ThrottledBackfill, PgIndexBackfillTestThrottled) {
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

// Set the backfill batch size, backfill rate and a low timeout for backfill rpc.
// Ensure that the backfill is completed. And that the avg rpc latency is
// below what is set as the timeout.
TEST_F_EX(
    PgIndexBackfillTest,
    BackfillRespectsDeadline,
    PgIndexBackfillTestDeadlines) {
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
TEST_F(PgIndexBackfillTest, Nonconcurrent) {
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

  // If the index used backfill, it would have incremented the table schema version by two or three:
  // - add index info with INDEX_PERM_DELETE_ONLY
  // - update to INDEX_PERM_DO_BACKFILL (as part of issue #6218)
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

// Test simultaneous CREATE INDEX.
TEST_F_EX(PgIndexBackfillTest,
          CreateIndexSimultaneously,
          PgIndexBackfillTestSimultaneously) {
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
      // Success index creations do two schema changes:
      // - add index with INDEX_PERM_WRITE_AND_DELETE
      // - transition to success INDEX_PERM_READ_WRITE_AND_DELETE
      // TODO(jason): change this when closing #6218 because DO_BACKFILL permission will add another
      // schema version.
      expected_schema_version += 2;
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
TEST_F(PgIndexBackfillTest, Tablegroup) {
  const std::string kTablegroupName = "test_tgroup";
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupName));

  TestSimpleBackfill(Format("TABLEGROUP $0", kTablegroupName));
}

// Test that retain_delete_markers is properly set after index backfill.
TEST_F(PgIndexBackfillTest, RetainDeleteMarkers) {
  TestRetainDeleteMarkers(kDatabaseName);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/19731.
TEST_F(PgIndexBackfillTest, RetainDeleteMarkersRecovery) {
  TestRetainDeleteMarkersRecovery(kDatabaseName, false /* use_multiple_requests */);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/19731.
TEST_F(PgIndexBackfillTest, RetainDeleteMarkersRecoveryViaSeveralRequests) {
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

// Test whether IsCreateTableDone works when creating an index with backfill enabled.  See issue
// #6234.
TEST_F_EX(PgIndexBackfillTest,
          IsCreateTableDone,
          PgIndexBackfillAlterSlowly) {
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

// Test backfill on clusters where the yugabyte role has authentication enabled.
TEST_F_EX(PgIndexBackfillTest,
          Auth,
          PgIndexBackfillAuth) {
  LOG(INFO) << "create " << this->kAuthDbName << " database";
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0", this->kAuthDbName));

  LOG(INFO) << "backfill table on " << this->kAuthDbName << " database";
  {
    auto auth_conn = ASSERT_RESULT(PGConnBuilder({
        .host = pg_ts->bind_host(),
        .port = pg_ts->pgsql_rpc_port(),
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

// Make sure backfill works when there exists user-defined HBA configuration with "local".
// This is for issue (#7705).
TEST_F_EX(PgIndexBackfillTest,
          LocalTrustSimple,
          PgIndexBackfillLocalTrust) {
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

TEST_F_EX(PgIndexBackfillTest,
          DropNoRetry,
          PgIndexBackfillNoRetry) {
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

TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_EXCEPT_RELEASE(GinStress),
          PgIndexBackfillGinStress) {
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
};

class PgIndexBackfillBlockIndisready : public PgIndexBackfillTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--ysql_yb_test_block_index_phase=indisready");
  }
};

class PgIndexBackfillBlockIndisreadyAndDoBackfill : public PgIndexBackfillBlockDoBackfill {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillBlockDoBackfill::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--ysql_yb_test_block_index_phase=indisready");
  }
};

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

// Make sure that index backfill doesn't care about snapshot too old.  Force a situation where the
// indexed table scan for backfill would occur after the committed history cutoff.  A compaction is
// needed to update this committed history cutoff, and the retention period needs to be low enough
// so that the cutoff is ahead of backfill's safe read time.  See issue #6333.
TEST_F_EX(PgIndexBackfillTest,
          SnapshotTooOld,
          PgIndexBackfillSnapshotTooOld) {
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
//                                                UPDATE a row of the indexed table
//     - do the actual backfill
//   - indisvalid
// The backfill should use the values before update when writing to the index.  The update should
// write and delete to the index because of permissions.  Since backfill writes with an ancient
// timestamp, the update should appear to have happened after the backfill.
TEST_F_EX(PgIndexBackfillTest,
          ReadTime,
          PgIndexBackfillBlockDoBackfill) {
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
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin write thread";
    ASSERT_OK(WaitForBackfillSafeTime(kYBTableName));

    LOG(INFO) << "Updating row";
    ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET j = j + 100 WHERE i = 3", kTableName));
    LOG(INFO) << "Done updating row";

    // It should still be in the backfill stage.
    ASSERT_TRUE(ASSERT_RESULT(IsAtTargetIndexStateFlags(
        kIndexName, IndexStateFlags{IndexStateFlag::kIndIsLive, IndexStateFlag::kIndIsReady})));

    ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  });
  thread_holder_.JoinAll();

  // Index scan to verify contents of index table.
  const std::string query = Format("SELECT * FROM $0 WHERE j = 113", kTableName);
  ASSERT_OK(WaitForIndexScan(query));
  const auto row = ASSERT_RESULT((conn_->FetchRow<int32_t, int32_t>(query)));
  // Make sure that the update is visible.
  ASSERT_EQ(row, (decltype(row){3, 113}));
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
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(Permissions),
          PgIndexBackfillBlockIndisready) {
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
TEST_F_EX(PgIndexBackfillTest,
          CreateUniqueIndexWithOnlineWrites,
          PgIndexBackfillSlow) {
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
TEST_F_EX(PgIndexBackfillTest,
          CreateUniqueIndexWriteAfterSafeTime,
          PgIndexBackfillBlockIndisreadyAndDoBackfill) {
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
TEST_F_EX(PgIndexBackfillTest,
          RetainDeletes,
          PgIndexBackfillBlockDoBackfill) {
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

TEST_F_EX(PgIndexBackfillTest,
          IndexScanVisibility,
          PgIndexBackfillBlockDoBackfill) {
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
    .port = diff_ts->pgsql_rpc_port(),
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

// Override to have smaller backfill deadline.
class PgIndexBackfillClientDeadline : public PgIndexBackfillBlockDoBackfill {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillBlockDoBackfill::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--backfill_index_client_rpc_timeout_ms=3000");
  }
};

// Make sure that the postgres timeout when waiting for backfill to finish causes the index to not
// become public.  Simulate the following:
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//   - (timeout)
TEST_F_EX(PgIndexBackfillTest,
          WaitBackfillTimeout,
          PgIndexBackfillClientDeadline) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  Status status = conn_->ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName);
  ASSERT_TRUE(HasClientTimedOut(status)) << status;

  // Make sure that the index is not public.
  ASSERT_FALSE(ASSERT_RESULT(conn_->HasIndexScan(Format(
      "SELECT * FROM $0 WHERE i = 1",
      kTableName))));
}

// Make sure that you can still drop an index that failed to fully create.
TEST_F_EX(PgIndexBackfillTest,
          DropAfterFail,
          PgIndexBackfillClientDeadline) {
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

// Make sure that DROP INDEX during backfill is handled well.  Simulate the following:
//   Session A                                    Session B
//   --------------------------                   ----------------------
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//                                                DROP INDEX
TEST_F_EX(PgIndexBackfillTest,
          DropWhileBackfilling,
          PgIndexBackfillFastClientTimeout) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
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

// Simply create table and index.  The CREATE INDEX should not timeout during backfill because the
// BackfillIndex request from postgres should use the backfill_index_client_rpc_timeout_ms timeout
// (default 60m) rather than the small yb_client_admin_operation_timeout_sec.
TEST_F_EX(PgIndexBackfillTest,
          LowerDefaultClientTimeout,
          PgIndexBackfillFastDefaultClientTimeout) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  // This should not time out.
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 (i)", kTableName));
}

// Override the index backfill fast client timeout test class to have more than one master.
class PgIndexBackfillMultiMaster : public PgIndexBackfillFastClientTimeout {
 public:
  int GetNumMasters() const override { return 3; }
};

// Make sure that master leader change during backfill causes the index to not become public and
// doesn't cause any weird hangups or other issues.  Simulate the following:
//   Session A                                    Session B
//   --------------------------                   ----------------------
//   CREATE INDEX
//   - indislive
//   - indisready
//   - backfill
//     - get safe time for read
//                                                master leader stepdown
// TODO(jason): update this test when handling master leader changes during backfill (issue #6218).
TEST_F_EX(PgIndexBackfillTest,
          MasterLeaderStepdown,
          PgIndexBackfillMultiMaster) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));

  // conn_ should be used by at most one thread for thread safety.
  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin create thread";
    PGConn create_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    // The CREATE INDEX should get master leader change during backfill so that its
    // WaitUntilIndexPermissionsAtLeast call starts querying the new leader.  Since the new leader
    // will be inactive at the WRITE_AND_DELETE docdb permission, it will wait until the deadline,
    // which is set to 30s.
    Status status = create_conn.ExecuteFormat("CREATE INDEX $0 ON $1 (i)", kIndexName, kTableName);
    ASSERT_TRUE(HasClientTimedOut(status)) << status;
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

// Make sure that backfill works when colocation is on.
TEST_F_EX(PgIndexBackfillTest,
          ColocatedSimple,
          PgIndexBackfillColocated) {
  TestSimpleBackfill();
}

// Make sure that backfill works when there are multiple colocated tables.
TEST_F_EX(PgIndexBackfillTest,
          ColocatedMultipleTables,
          PgIndexBackfillColocated) {
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
TEST_F_EX(PgIndexBackfillTest,
          ColocatedRetainDeleteMarkers,
          PgIndexBackfillColocated) {
  TestRetainDeleteMarkers(kColoDbName);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/19731.
TEST_F_EX(PgIndexBackfillTest,
          ColocatedRetainDeleteMarkersRecovery,
          PgIndexBackfillColocated) {
  TestRetainDeleteMarkersRecovery(kColoDbName, false /* use_multiple_requests */);
}
TEST_F_EX(PgIndexBackfillTest,
          ColocatedRetainDeleteMarkersRecoveryViaSeveralRequests,
          PgIndexBackfillColocated) {
  TestRetainDeleteMarkersRecovery(kColoDbName, true /* use_multiple_requests */);
}

// Verify in-progress CREATE INDEX command's entry in pg_stat_progress_create_index.
TEST_F_EX(PgIndexBackfillTest,
          PgStatProgressCreateIndexPhase,
          PgIndexBackfillBlockIndisreadyAndDoBackfill) {
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
TEST_F_EX(PgIndexBackfillTest,
          PgStatProgressCreateIndexPermissions,
          PgIndexBackfillBlockDoBackfill) {
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
TEST_F_EX(PgIndexBackfillTest,
          PgStatProgressCreateIndexMultiNode,
          PgIndexBackfillBlockDoBackfill) {
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
TEST_F_EX(PgIndexBackfillTest,
          PgStatProgressCreateIndexCheckVolatility,
          PgIndexBackfillBlockDoBackfill) {
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
TEST_F(PgIndexBackfillTest,
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
TEST_F(PgIndexBackfillTest,
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
TEST_F_EX(PgIndexBackfillTest,
          PgStatProgressCreateIndexDifferentDatabase,
          PgIndexBackfillBlockDoBackfill) {
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

// Override to use YSQL backends manager.
class PgIndexBackfillBackendsManager : public PgIndexBackfillBlockDoBackfill {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgIndexBackfillBlockDoBackfill::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
        "--ysql_yb_disable_wait_for_backends_catalog_version=false");
  }
};

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
TEST_F_EX(PgIndexBackfillTest,
          YB_DISABLE_TEST_IN_TSAN(NoAbortTxn),
          PgIndexBackfillBackendsManager) {
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

} // namespace yb::pgwrapper

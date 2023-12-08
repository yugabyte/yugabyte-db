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

#include "yb/integration-tests/cql_test_base.h"

#include "yb/client/table_info.h"

#include "yb/docdb/deadline_info.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster_validator.h"
#include "yb/integration-tests/mini_cluster_utils.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/write_query.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

DECLARE_bool(allow_index_table_read_write);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(cql_prepare_child_threshold_ms);
DECLARE_bool(disable_index_backfill);
DECLARE_int32(rpc_workers_limit);
DECLARE_int64(transaction_abort_check_interval_ms);
DECLARE_uint64(transaction_manager_workers_limit);
DECLARE_bool(transactions_poll_check_aborted);

DECLARE_bool(TEST_disable_proactive_txn_cleanup_on_abort);
DECLARE_int32(TEST_fetch_next_delay_ms);
DECLARE_uint64(TEST_inject_txn_get_status_delay_ms);
DECLARE_bool(TEST_writequery_stuck_from_callback_leak);
DECLARE_bool(TEST_simulate_cannot_enable_compactions);

namespace yb {

class CqlIndexTest : public CqlTestBase<MiniCluster> {
 public:
  virtual ~CqlIndexTest() = default;

  void TestTxnCleanup(size_t max_remaining_txns_per_tablet);
  void TestConcurrentModify2Columns(const std::string& expr);
};

YB_STRONGLY_TYPED_BOOL(UniqueIndex);

Status CreateIndexedTable(
    CassandraSession* session, UniqueIndex unique_index = UniqueIndex::kFalse) {
  RETURN_NOT_OK(
      session->ExecuteQuery("CREATE TABLE IF NOT EXISTS t (key INT PRIMARY KEY, value INT) WITH "
                            "transactions = { 'enabled' : true }"));
  return session->ExecuteQuery(
      Format("CREATE $0 INDEX IF NOT EXISTS idx ON T (value)", unique_index ? "UNIQUE" : ""));
}

TEST_F(CqlIndexTest, Simple) {
  constexpr int kKey = 1;
  constexpr int kValue = 2;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(CreateIndexedTable(&session));

  ASSERT_OK(session.ExecuteQuery("INSERT INTO t (key, value) VALUES (1, 2)"));
  auto result = ASSERT_RESULT(session.ExecuteWithResult("SELECT * FROM t WHERE value = 2"));
  auto iter = result.CreateIterator();
  ASSERT_TRUE(iter.Next());
  auto row = iter.Row();
  ASSERT_EQ(row.Value(0).As<cass_int32_t>(), kKey);
  ASSERT_EQ(row.Value(1).As<cass_int32_t>(), kValue);
  ASSERT_FALSE(iter.Next());
}

TEST_F(CqlIndexTest, EmptyIndex) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_index_backfill) = false;
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  WARN_NOT_OK(session.ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, value INT) WITH transactions = { 'enabled' : true }"),
      "Create table failed.");
  auto future = session.ExecuteGetFuture(
      "CREATE INDEX idx ON T (value) WITH transactions = { 'enabled' : true }");

  constexpr auto kNamespace = "test";
  const client::YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "t");
  const client::YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "idx");

  ASSERT_OK(future.Wait());
  auto perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  CHECK_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
  auto index = ASSERT_RESULT(client_->GetYBTableInfo(index_table_name));
  ASSERT_FALSE(index.schema.table_properties().retain_delete_markers());
}

TEST_F(CqlIndexTest, EmptyIndexRecovery) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_cannot_enable_compactions) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_index_backfill) = false;
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  WARN_NOT_OK(session.ExecuteQuery(
      "CREATE TABLE ttt(key INT PRIMARY KEY, value INT) WITH transactions = { 'enabled' : true }"),
      "Create table failed.");
  auto future = session.ExecuteGetFuture(
      "CREATE INDEX ttt_idx ON ttt(value) WITH transactions = { 'enabled' : true }");

  constexpr auto kNamespace = "test";
  const client::YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "ttt");
  const client::YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "ttt_idx");

  ASSERT_OK(future.Wait());
  {
    auto perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
    CHECK_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
    auto index = ASSERT_RESULT(client_->GetYBTableInfo(index_table_name));
    ASSERT_TRUE(index.schema.table_properties().retain_delete_markers());
  }

  // Restart and make sure retain delete markers value has been fixed (is unset now).
  ASSERT_OK(RestartCluster());
  LOG(INFO) << "Cluster restarted, checking table properties";
  {
    auto index = ASSERT_RESULT(client_->GetYBTableInfo(index_table_name));
    ASSERT_FALSE(index.schema.table_properties().retain_delete_markers());
  }
}

TEST_F(CqlIndexTest, MultipleIndex) {
  FLAGS_disable_index_backfill = false;
  auto session1 = ASSERT_RESULT(EstablishSession(driver_.get()));
  auto session2 = ASSERT_RESULT(EstablishSession(driver_.get()));

  WARN_NOT_OK(session1.ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, value INT) WITH transactions = { 'enabled' : true }"),
          "Create table failed.");
  auto future1 = session1.ExecuteGetFuture("CREATE INDEX idx1 ON T (value)");
  auto future2 = session2.ExecuteGetFuture("CREATE INDEX idx2 ON T (value)");

  constexpr auto kNamespace = "test";
  const client::YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "t");
  const client::YBTableName index_table_name1(YQL_DATABASE_CQL, kNamespace, "idx1");
  const client::YBTableName index_table_name2(YQL_DATABASE_CQL, kNamespace, "idx2");

  LOG(INFO) << "Waiting for idx1 got " << future1.Wait();
  auto perm1 = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name1, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  CHECK_EQ(perm1, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
  LOG(INFO) << "Waiting for idx2 got " << future2.Wait();
  auto perm2 = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name2, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  CHECK_EQ(perm2, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
}

class CqlIndexSmallWorkersTest : public CqlIndexTest {
 public:
  void SetUp() override {
    FLAGS_rpc_workers_limit = 4;
    FLAGS_transaction_manager_workers_limit = 4;
    CqlIndexTest::SetUp();
  }
};

TEST_F_EX(CqlIndexTest, ConcurrentIndexUpdate, CqlIndexSmallWorkersTest) {
  constexpr int kThreads = 8;
  constexpr cass_int32_t kKeys = kThreads / 2;
  constexpr cass_int32_t kValues = kKeys;
  constexpr int kNumInserts = kThreads * 5;

  FLAGS_client_read_write_timeout_ms = 10000 * kTimeMultiplier;
  SetAtomicFlag(1000, &FLAGS_TEST_inject_txn_get_status_delay_ms);
  FLAGS_transaction_abort_check_interval_ms = 100000;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(LoggedWaitFor(
      [&session]() { return CreateIndexedTable(&session).ok(); }, 60s, "create table", 12s));
  constexpr auto kNamespace = "test";
  const client::YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "t");
  const client::YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "idx");
  ASSERT_OK(LoggedWaitFor(
      [this, table_name, index_table_name]() {
        auto result = client_->WaitUntilIndexPermissionsAtLeast(
            table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
        return result.ok() && *result == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE;
      },
      90s,
      "wait for create index to complete",
      12s));

  TestThreadHolder thread_holder;
  std::atomic<int> inserts(0);
  for (int i = 0; i != kThreads; ++i) {
    thread_holder.AddThreadFunctor([this, &inserts, &stop = thread_holder.stop_flag()] {
      auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
      auto prepared = ASSERT_RESULT(session.Prepare("INSERT INTO t (key, value) VALUES (?, ?)"));
      while (!stop.load(std::memory_order_acquire)) {
        auto stmt = prepared.Bind();
        stmt.Bind(0, RandomUniformInt<cass_int32_t>(1, kKeys));
        stmt.Bind(1, RandomUniformInt<cass_int32_t>(1, kValues));
        auto status = session.Execute(stmt);
        if (status.ok()) {
          ++inserts;
        } else {
          LOG(INFO) << "Insert failed: " << status;
        }
      }
    });
  }

  while (!thread_holder.stop_flag().load(std::memory_order_acquire)) {
    auto num_inserts = inserts.load(std::memory_order_acquire);
    if (num_inserts >= kNumInserts) {
      break;
    }
    YB_LOG_EVERY_N_SECS(INFO, 5) << "Num inserts " << num_inserts << " of " << kNumInserts;
    std::this_thread::sleep_for(100ms);
  }

  thread_holder.Stop();

  SetAtomicFlag(0, &FLAGS_TEST_inject_txn_get_status_delay_ms);
}

int64_t GetFailedBatchLockNum(MiniCluster* cluster)  {
  int64_t failed_batch_lock = 0;
  auto list = ListTabletPeers(cluster, ListPeersFilter::kAll);
  for (const auto& peer : list) {
    if (peer->tablet()->metadata()->table_name() == "t") {
      auto metrics = peer->tablet()->metrics();
      if (metrics) {
        failed_batch_lock += metrics->failed_batch_lock->value();
      }
    }
  }
  return failed_batch_lock;
}

TEST_F(CqlIndexTest, WriteQueryStuckAndUpdateOnSameKey) {
  FLAGS_TEST_writequery_stuck_from_callback_leak = true;
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t(id INT PRIMARY KEY, s TEXT) WITH transactions = { 'enabled' : true };"));
  ASSERT_OK(session.ExecuteQuery("Create index idx on t(id) WHERE id < 0;"));
  std::this_thread::sleep_for(5000ms);
  // Trigger callback leak, which means a WriteQuery never get destroyed.
  ASSERT_NOK(session.ExecuteQuery("INSERT INTO t(id, s) values(-1, 'test');"));
  // Validate that the stuck WriteQuery object block the followup update on same key
  // due to batch lock fail.
  FLAGS_client_read_write_timeout_ms =
      narrow_cast<uint32_t>(kCassandraTimeOut.ToMilliseconds());
  int64_t failed_batch_lock = GetFailedBatchLockNum(cluster_.get());
  ASSERT_NOK(session.ExecuteQuery("UPDATE t SET s = 'txn' WHERE id = -1;"));
  std::this_thread::sleep_for(1000ms);
  ASSERT_EQ(failed_batch_lock+1, GetFailedBatchLockNum(cluster_.get()));
  SetAtomicFlag(false, &FLAGS_TEST_writequery_stuck_from_callback_leak);
}

TEST_F(CqlIndexTest, WriteQueryStuckAndVerifyTxnCleanup) {
  FLAGS_TEST_writequery_stuck_from_callback_leak = true;
  const size_t kNumTxns = 100;
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t(id INT PRIMARY KEY, s TEXT) WITH transactions = { 'enabled' : true };"));
  ASSERT_OK(session.ExecuteQuery("Create index idx on t(id) WHERE id < 0;"));
  std::this_thread::sleep_for(5000ms);
  // Trigger callback leak, which means a WriteQuery never get destroyed.
  ASSERT_NOK(session.ExecuteQuery("INSERT INTO t(id, s) values(-1, 'test');"));
  // Verify the stuck WriteQuery object doesn't block transaction clean up.
  ASSERT_OK(session.ExecuteQuery("INSERT INTO t(id, s) values(0, 'test');"));
  for (size_t i = 0; i < kNumTxns; i++) {
    ASSERT_OK(session.ExecuteQuery(Format("START TRANSACTION;"
                                          " UPDATE t SET s = 'txn$i' WHERE id = 0;"
                                          "COMMIT;", i)));
  }
  std::this_thread::sleep_for(5000ms);
  size_t total_txns = 0;
  auto list = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  for (const auto& peer : list) {
    if (peer->tablet()->metadata()->table_name() == "t") {
      auto* participant = peer->tablet()->transaction_participant();
      if (participant) {
        total_txns += participant->TEST_GetNumRunningTransactions();
      }
    }
  }
  ASSERT_EQ(0, total_txns);
  SetAtomicFlag(false, &FLAGS_TEST_writequery_stuck_from_callback_leak);
}

TEST_F(CqlIndexTest, TestSaturatedWorkers) {
  /*
   * (#11258) We set a very short timeout to force failure if child transaction
   * is not created quickly enough.

   * TODO: when switching to a fully asynchronous model, this failure will disappear.
   */
  FLAGS_cql_prepare_child_threshold_ms = 1;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 INT, v2 INT) WITH "
      "transactions = { 'enabled' : true }"));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE INDEX i1 ON t(key, v1) WITH "
      "transactions = { 'enabled' : true }"));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE INDEX i2 ON t(key, v2) WITH "
      "transactions = { 'enabled' : true }"));

  constexpr int kKeys = 10000;
  std::string expr = "BEGIN TRANSACTION ";
  for (int i = 0; i < kKeys; i++) {
    expr += Format("INSERT INTO t (key, v1, v2) VALUES ($0, $1, $2); ", i, i, i);
  }
  expr += "END TRANSACTION;";

  // We should expect to see timed out error
  auto status = session.ExecuteQuery(expr);
  ASSERT_FALSE(status.ok());
  ASSERT_NE(status.message().ToBuffer().find("Timed out waiting for prepare child status"),
            std::string::npos) << status;
}

YB_STRONGLY_TYPED_BOOL(CheckReady);

void CleanFutures(std::deque<CassandraFuture>* futures, CheckReady check_ready) {
  while (!futures->empty() && (!check_ready || futures->front().Ready())) {
    auto status = futures->front().Wait();
    if (!status.ok() && !status.IsTimedOut()) {
      auto msg = status.message().ToBuffer();
      if (msg.find("Duplicate value disallowed by unique index") == std::string::npos) {
        ASSERT_OK(status);
      }
    }
    futures->pop_front();
  }
}

void CqlIndexTest::TestTxnCleanup(size_t max_remaining_txns_per_tablet) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(CreateIndexedTable(&session, UniqueIndex::kTrue));
  std::deque<CassandraFuture> futures;

  auto prepared = ASSERT_RESULT(session.Prepare("INSERT INTO t (key, value) VALUES (?, ?)"));

  for (int i = 0; i != RegularBuildVsSanitizers(100, 30); ++i) {
    ASSERT_NO_FATALS(CleanFutures(&futures, CheckReady::kTrue));

    auto stmt = prepared.Bind();
    stmt.Bind(0, i);
    stmt.Bind(1, RandomUniformInt<cass_int32_t>(1, 10));
    futures.push_back(session.ExecuteGetFuture(stmt));
  }

  ASSERT_NO_FATALS(CleanFutures(&futures, CheckReady::kFalse));

  AssertRunningTransactionsCountLessOrEqualTo(cluster_.get(), max_remaining_txns_per_tablet);
}

// Test proactive aborted transactions cleanup.
TEST_F(CqlIndexTest, TxnCleanup) {
  FLAGS_transactions_poll_check_aborted = false;

  TestTxnCleanup(/* max_remaining_txns_per_tablet= */ 5);
}

// Test poll based aborted transactions cleanup.
TEST_F(CqlIndexTest, TxnPollCleanup) {
  FLAGS_TEST_disable_proactive_txn_cleanup_on_abort = true;
  FLAGS_transaction_abort_check_interval_ms = 1000;

  TestTxnCleanup(/* max_remaining_txns_per_tablet= */ 0);
}

void CqlIndexTest::TestConcurrentModify2Columns(const std::string& expr) {
  FLAGS_allow_index_table_read_write = true;

  constexpr int kKeys = RegularBuildVsSanitizers(50, 10);

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 INT, v2 INT) WITH "
      "transactions = { 'enabled' : true }"));
  ASSERT_OK(session.ExecuteQuery("CREATE INDEX v1_idx ON t (v1)"));

  // Wait for the table alterations to complete.
  std::this_thread::sleep_for(5000ms);

  auto prepared1 = ASSERT_RESULT(session.Prepare(Format(expr, "v1")));
  auto prepared2 = ASSERT_RESULT(session.Prepare(Format(expr, "v2")));

  std::vector<CassandraFuture> futures;

  for (int i = 0; i != kKeys; ++i) {
    for (auto* prepared : {&prepared1, &prepared2}) {
      auto statement = prepared->Bind();
      statement.Bind(0, i);
      statement.Bind(1, i);
      futures.push_back(session.ExecuteGetFuture(statement));
    }
  }

  int good = 0;
  int bad = 0;
  for (auto& future : futures) {
    auto status = future.Wait();
    if (status.ok()) {
      ++good;
    } else {
      ASSERT_TRUE(status.IsTimedOut()) << status;
      ++bad;
    }
  }

  ASSERT_GE(good, bad * 4);

  std::vector<bool> present(kKeys);

  int read_keys = 0;
  auto result = ASSERT_RESULT(session.ExecuteWithResult("SELECT * FROM v1_idx"));
  auto iter = result.CreateIterator();
  while (iter.Next()) {
    auto row = iter.Row();
    auto key = row.Value(0).As<int32_t>();
    auto value = row.Value(1);
    LOG(INFO) << key << ", " << value.ToString();

    ASSERT_FALSE(present[key]) << key;
    present[key] = true;
    ++read_keys;
    ASSERT_FALSE(value.IsNull());
    ASSERT_EQ(key, value.As<int32_t>());
  }

  ASSERT_EQ(read_keys, kKeys);
}

TEST_F(CqlIndexTest, ConcurrentInsert2Columns) {
  TestConcurrentModify2Columns("INSERT INTO t ($0, key) VALUES (?, ?)");
}

TEST_F(CqlIndexTest, ConcurrentUpdate2Columns) {
  TestConcurrentModify2Columns("UPDATE t SET $0 = ? WHERE key = ?");
}

TEST_F(CqlIndexTest, SlowIndexResponse) {
  constexpr auto kNumKeys = NonTsanVsTsan(3000, 1500);
  constexpr auto kWriteBatchSize = NonTsanVsTsan(100, 30);
  constexpr auto kNumWriteThreads = 8;
  constexpr auto kFixedCategory = 0;
  constexpr auto kTestReadDelayMs = 1;

  FLAGS_client_read_write_timeout_ms = 100000 * kTimeMultiplier;

  {
    auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

    ASSERT_OK(session.ExecuteQuery(
        "CREATE TABLE test_table (key INT PRIMARY KEY, category INT, value INT) WITH "
        "transactions = { 'enabled' : true }"));
    ASSERT_OK(session.ExecuteQuery(
        "CREATE INDEX category_idx ON test_table (category) WITH TABLETS = 1"));

    std::atomic<int> num_rows = 0;

    auto writer = [&num_rows, kFixedCategory, this]() -> void {
      auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
      auto prepared = ASSERT_RESULT(
          session.Prepare("INSERT INTO test_table (key, category, value) VALUES (?, ?, ?)"));
      while (num_rows < kNumKeys) {
        CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
        for (int i = 0; i < kWriteBatchSize; ++i) {
          const auto prev_num_rows = num_rows.fetch_add(1);
          if (prev_num_rows == kNumKeys) {
            num_rows.fetch_sub(1);
            break;
          }
          auto stmt = prepared.Bind();
          stmt.Bind(0, prev_num_rows);
          stmt.Bind(1, kFixedCategory);
          stmt.Bind(2, prev_num_rows);
          batch.Add(&stmt);
        }

        ASSERT_OK(session.ExecuteBatch(batch));
        YB_LOG_EVERY_N_SECS(INFO, 5) << "Inserted " << num_rows << " rows";
      }
    };

    TestThreadHolder writers;
    for (int i = 0; i < kNumWriteThreads; ++i) {
      writers.AddThreadFunctor(writer);
    }
    writers.JoinAll();

    LOG(INFO) << "Inserted " << num_rows << " rows";

    NO_PENDING_FATALS();
  }

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Restart cluster so MetaCache is cleared.
  ASSERT_OK(RestartCluster());

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  LOG(INFO) << "Running SELECT";

  // We want index tablet scan to take >= FLAGS_client_read_write_timeout_ms in order to reproduce
  // scenario where MetaCache is trying to lookup tablets based on keys returned by index after
  // hitting deadline.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = kNumKeys * kTestReadDelayMs;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fetch_next_delay_ms) = kTestReadDelayMs;

  CountDownLatch latch(1);
  TestThreadHolder select_thread;
  select_thread.AddThreadFunctor([&session, &latch, kFixedCategory]{
    Stopwatch sw;
    sw.start();
    auto result = session.ExecuteWithResult(
        Format("SELECT * FROM test_table WHERE category = $0", kFixedCategory));
    sw.stop();
    latch.CountDown();
    ASSERT_NOK(result) << "Expected SELECT to fail due to time out, but got: " << [&result]() {
      auto iter = result->CreateIterator();
      return iter.Next() ? AsString(iter.Row().Value(0).As<int64>()) : "<none>";
    }();
    ASSERT_GE(sw.elapsed().wall_millis(), FLAGS_client_read_write_timeout_ms)
        << "SELECT failed too early";
  });

  const auto timeout_limit_ms =
      FLAGS_client_read_write_timeout_ms +
      narrow_cast<int32_t>(kTestReadDelayMs * docdb::kDeadlineCheckGranularity * 1.1);
  const auto latch_done = latch.WaitFor(timeout_limit_ms * 1ms);
  LOG(INFO) << "Latch done: " << latch_done;
  ASSERT_TRUE(latch_done) << "SELECT hasn't completed within " << timeout_limit_ms << " ms";

  select_thread.JoinAll();
}

class CqlIndexExternalMiniClusterTest : public CqlTestBase<ExternalMiniCluster> {
 protected:
  void TestRetainDeleteMarkersRecovery(bool use_multiple_requests) {
    ASSERT_OK(EnsureClientCreated());
    auto validator =
        CqlRetainDeleteMarkersValidator{ cluster_.get(), client_.get(), driver_.get() };
    validator.TestRecovery(use_multiple_requests);
  }

 private:
  class CqlRetainDeleteMarkersValidator final : public itest::RetainDeleteMarkersValidator {
    using Base = itest::RetainDeleteMarkersValidator;

   public:
    CqlRetainDeleteMarkersValidator(
        ExternalMiniCluster* cluster, client::YBClient* client, CppCassandraDriver* driver)
        : Base(cluster, client, kCqlTestKeyspace), driver_(*CHECK_NOTNULL(driver)) {
    }

   private:
    Status RestartCluster() override {
      RETURN_NOT_OK(Base::RestartCluster());
      session_ = VERIFY_RESULT(EstablishSession(&driver_));
      return Status::OK();
    }

    Status CreateIndex(const std::string &index_name, const std::string &table_name) override {
      return session_.ExecuteQueryFormat(
          "CREATE INDEX $0 ON $1(value) WITH transactions = { 'enabled' : true }",
          index_name, table_name);
    }

    Status CreateTable(const std::string &table_name) override {
      return session_.ExecuteQueryFormat(
          "CREATE TABLE $0 (key INT PRIMARY KEY, value INT) "
          "WITH transactions = { 'enabled' : true }", table_name);
    }

    CppCassandraDriver& driver_;
    CassandraSession session_;
  };
};

// Test for https://github.com/yugabyte/yugabyte-db/issues/19731.
TEST_F(CqlIndexExternalMiniClusterTest, RetainDeleteMarkersRecovery) {
  TestRetainDeleteMarkersRecovery(false /* use_multiple_requests */);
}

// Test for https://github.com/yugabyte/yugabyte-db/issues/19731.
TEST_F(CqlIndexExternalMiniClusterTest, RetainDeleteMarkersRecoveryViaSeveralRequests) {
  TestRetainDeleteMarkersRecovery(true /* use_multiple_requests */);
}

} // namespace yb

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

#include "yb/client/table_info.h"

#include "yb/consensus/raft_consensus.h"

#include "yb/docdb/primitive_value.h"

#include "yb/integration-tests/cql_test_base.h"

#include "yb/master/mini_master.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/random_util.h"
#include "yb/util/range.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

DECLARE_bool(cleanup_intents_sst_files);
DECLARE_bool(TEST_timeout_non_leader_master_rpcs);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_int64(cql_processors_limit);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(timestamp_history_retention_interval_sec);

DECLARE_int32(partitions_vtable_cache_refresh_secs);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_bool(disable_truncate_table);

namespace yb {

using client::YBTableInfo;
using client::YBTableName;

class CqlTest : public CqlTestBase<MiniCluster> {
 public:
  virtual ~CqlTest() = default;
};

TEST_F(CqlTest, ProcessorsLimit) {
  constexpr int kSessions = 10;
  FLAGS_cql_processors_limit = 1;

  std::vector<CassandraSession> sessions;
  bool has_failures = false;
  for (int i = 0; i != kSessions; ++i) {
    auto session = EstablishSession(driver_.get());
    if (!session.ok()) {
      LOG(INFO) << "Establish session failure: " << session.status();
      ASSERT_TRUE(session.status().IsServiceUnavailable());
      has_failures = true;
    } else {
      sessions.push_back(std::move(*session));
    }
  }

  ASSERT_TRUE(has_failures);
}

// Execute delete in parallel to transactional update of the same row.
TEST_F(CqlTest, ConcurrentDeleteRowAndUpdateColumn) {
  constexpr int kIterations = 70;
  auto session1 = ASSERT_RESULT(EstablishSession(driver_.get()));
  auto session2 = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session1.ExecuteQuery(
      "CREATE TABLE t (i INT PRIMARY KEY, j INT) WITH transactions = { 'enabled' : true }"));
  auto insert_prepared = ASSERT_RESULT(session1.Prepare("INSERT INTO t (i, j) VALUES (?, ?)"));
  for (int key = 1; key <= 2 * kIterations; ++key) {
    auto stmt = insert_prepared.Bind();
    stmt.Bind(0, key);
    stmt.Bind(1, key * 10);
    ASSERT_OK(session1.Execute(stmt));
  }
  auto update_prepared = ASSERT_RESULT(session1.Prepare(
      "BEGIN TRANSACTION "
      "  UPDATE t SET j = j + 1 WHERE i = ?;"
      "  UPDATE t SET j = j + 1 WHERE i = ?;"
      "END TRANSACTION;"));
  auto delete_prepared = ASSERT_RESULT(session1.Prepare("DELETE FROM t WHERE i = ?"));
  std::vector<CassandraFuture> futures;
  for (int i = 0; i < kIterations; ++i) {
    int k1 = i * 2 + 1;
    int k2 = i * 2 + 2;

    auto update_stmt = update_prepared.Bind();
    update_stmt.Bind(0, k1);
    update_stmt.Bind(1, k2);
    futures.push_back(session1.ExecuteGetFuture(update_stmt));
  }

  for (int i = 0; i < kIterations; ++i) {
    int k2 = i * 2 + 2;

    auto delete_stmt = delete_prepared.Bind();
    delete_stmt.Bind(0, k2);
    futures.push_back(session1.ExecuteGetFuture(delete_stmt));
  }

  for (auto& future : futures) {
    ASSERT_OK(future.Wait());
  }

  auto result = ASSERT_RESULT(session1.ExecuteWithResult("SELECT * FROM t"));
  auto iterator = result.CreateIterator();
  int num_rows = 0;
  int num_even = 0;
  while (iterator.Next()) {
    ++num_rows;
    auto row = iterator.Row();
    auto key = row.Value(0).As<int>();
    auto value = row.Value(1).As<int>();
    if ((key & 1) == 0) {
      LOG(ERROR) << "Even key: " << key;
      ++num_even;
    }
    ASSERT_EQ(value, key * 10 + 1);
    LOG(INFO) << "Row: " << key << " => " << value;
  }
  ASSERT_EQ(num_rows, kIterations);
  ASSERT_EQ(num_even, 0);
}

TEST_F(CqlTest, TestUpdateListIndexAfterOverwrite) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  auto cql = [&](const std::string query) { ASSERT_OK(session.ExecuteQuery(query)); };
  cql("CREATE TABLE test(h INT, v LIST<INT>, PRIMARY KEY(h))");
  cql("INSERT INTO test (h, v) VALUES (1, [1, 2, 3])");

  auto select = [&]() -> Result<string> {
    auto result = VERIFY_RESULT(session.ExecuteWithResult("SELECT * FROM test"));
    auto iter = result.CreateIterator();
    DFATAL_OR_RETURN_ERROR_IF(!iter.Next(), STATUS(NotFound, "Did not find result in test table."));
    auto row = iter.Row();
    auto key = row.Value(0).As<int>();
    EXPECT_EQ(key, 1);
    return row.Value(1).ToString();
  };

  cql("UPDATE test SET v = [4, 5, 6] where h = 1");
  cql("UPDATE test SET v[0] = 7 WHERE h = 1");
  auto res1 = ASSERT_RESULT(select());
  EXPECT_EQ(res1, "[7, 5, 6]");

  cql("INSERT INTO test (h, v) VALUES (1, [10, 11, 12])");
  cql("UPDATE test SET v[0] = 8 WHERE h = 1");
  auto res2 = ASSERT_RESULT(select());
  EXPECT_EQ(res2, "[8, 11, 12]");
}

TEST_F(CqlTest, Timeout) {
  FLAGS_client_read_write_timeout_ms = 5000 * kTimeMultiplier;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (i INT PRIMARY KEY, j INT) WITH transactions = { 'enabled' : true }"));

  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  for (const auto& peer : peers) {
    peer->raft_consensus()->TEST_DelayUpdate(100ms);
  }

  auto prepared =
      ASSERT_RESULT(session.Prepare("BEGIN TRANSACTION "
                                    "  INSERT INTO t (i, j) VALUES (?, ?);"
                                    "END TRANSACTION;"));
  struct Request {
    CassandraFuture future;
    CoarseTimePoint start_time;
  };
  std::deque<Request> requests;
  constexpr int kOps = 50;
  constexpr int kKey = 42;
  int executed_ops = 0;
  for (;;) {
    while (!requests.empty() && requests.front().future.Ready()) {
      WARN_NOT_OK(requests.front().future.Wait(), "Insert failed");
      auto passed = CoarseMonoClock::now() - requests.front().start_time;
      ASSERT_LE(passed, FLAGS_client_read_write_timeout_ms * 1ms + 2s * kTimeMultiplier);
      requests.pop_front();
    }
    if (executed_ops >= kOps) {
      if (requests.empty()) {
        break;
      }
      std::this_thread::sleep_for(100ms);
      continue;
    }

    auto stmt = prepared.Bind();
    stmt.Bind(0, kKey);
    stmt.Bind(1, ++executed_ops);
    requests.push_back(Request{
        .future = session.ExecuteGetFuture(stmt),
        .start_time = CoarseMonoClock::now(),
    });
  }
}

TEST_F(CqlTest, RecreateTableWithInserts) {
  const auto kNumKeys = 4;
  const auto kNumIters = 2;
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  for (int i = 0; i != kNumIters; ++i) {
    SCOPED_TRACE(Format("Iteration: $0", i));
    ASSERT_OK(session.ExecuteQuery(
        "CREATE TABLE t (k INT PRIMARY KEY, v INT) WITH transactions = { 'enabled' : true }"));
    std::string expr = "BEGIN TRANSACTION ";
    for (int key = 0; key != kNumKeys; ++key) {
      expr += "INSERT INTO t (k, v) VALUES (?, ?); ";
    }
    expr += "END TRANSACTION;";
    auto prepared = ASSERT_RESULT(session.Prepare(expr));
    auto stmt = prepared.Bind();
    size_t idx = 0;
    for (int key = 0; key != kNumKeys; ++key) {
      stmt.Bind(idx++, RandomUniformInt<int32_t>(-1000, 1000));
      stmt.Bind(idx++, -key);
    }
    ASSERT_OK(session.Execute(stmt));
    ASSERT_OK(session.ExecuteQuery("DROP TABLE t"));
  }
}

class CqlThreeMastersTest : public CqlTest {
 public:
  void SetUp() override {
    FLAGS_partitions_vtable_cache_refresh_secs = 0;
    CqlTest::SetUp();
  }

  int num_masters() override { return 3; }
};

Status CheckNumAddressesInYqlPartitionsTable(CassandraSession* session, int expected_num_addrs) {
  const int kReplicaAddressesIndex = 5;
  auto result = VERIFY_RESULT(session->ExecuteWithResult("SELECT * FROM system.partitions"));
  auto iterator = result.CreateIterator();
  while (iterator.Next()) {
    auto replica_addresses = iterator.Row().Value(kReplicaAddressesIndex).ToString();
    ssize_t num_addrs = 0;
    if (replica_addresses.size() > std::strlen("{}")) {
      num_addrs = std::count(replica_addresses.begin(), replica_addresses.end(), ',') + 1;
    }

    EXPECT_EQ(num_addrs, expected_num_addrs);
  }
  return Status::OK();
}

TEST_F_EX(CqlTest, HostnameResolutionFailureInYqlPartitionsTable, CqlThreeMastersTest) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(CheckNumAddressesInYqlPartitionsTable(&session, 3));

  // TEST_RpcAddress is 1-indexed.
  string hostname =
      server::TEST_RpcAddress(cluster_->LeaderMasterIdx() + 1, server::Private::kFalse);

  // Fail resolution of the old leader master's hostname.
  TEST_SetFailToFastResolveAddress(hostname);

  // Shutdown the master leader, and wait for new leader to get elected.
  ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Shutdown();
  ASSERT_RESULT(cluster_->GetLeaderMiniMaster());

  // Assert that a new call will succeed, but will be missing the shutdown master address.
  ASSERT_OK(CheckNumAddressesInYqlPartitionsTable(&session, 2));

  TEST_SetFailToFastResolveAddress("");
}

TEST_F_EX(CqlTest, NonRespondingMaster, CqlThreeMastersTest) {
  FLAGS_TEST_timeout_non_leader_master_rpcs = true;
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE t1 (i INT PRIMARY KEY, j INT)"));
  ASSERT_OK(session.ExecuteQuery("INSERT INTO t1 (i, j) VALUES (1, 1)"));
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE t2 (i INT PRIMARY KEY, j INT)"));

  LOG(INFO) << "Prepare";
  auto prepared = ASSERT_RESULT(session.Prepare("INSERT INTO t2 (i, j) VALUES (?, ?)"));
  LOG(INFO) << "Step down";
  auto peer = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->tablet_peer();
  ASSERT_OK(StepDown(peer, std::string(), ForceStepDown::kTrue));
  LOG(INFO) << "Insert";
  FLAGS_client_read_write_timeout_ms = 5000;
  bool has_ok = false;
  for (int i = 0; i != 3; ++i) {
    auto stmt = prepared.Bind();
    stmt.Bind(0, i);
    stmt.Bind(1, 1);
    auto status = session.Execute(stmt);
    if (status.ok()) {
      has_ok = true;
      break;
    }
    ASSERT_NE(status.message().ToBuffer().find("timed out"), std::string::npos) << status;
  }
  ASSERT_TRUE(has_ok);
}

TEST_F(CqlTest, TestTruncateTable) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  auto cql = [&](const std::string query) { ASSERT_OK(session.ExecuteQuery(query)); };
  cql("CREATE TABLE users(userid INT PRIMARY KEY, fullname TEXT)");
  cql("INSERT INTO users(userid,fullname) values (1, 'yb');");
  cql("TRUNCATE TABLE users");
  auto result = ASSERT_RESULT(session.ExecuteWithResult("SELECT count(*) FROM users"));
  auto iterator = result.CreateIterator();
  iterator.Next();
  auto count = iterator.Row().Value(0).As<int64>();
  LOG(INFO) << "Count : " << count;
  EXPECT_EQ(count, 0);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_truncate_table) = true;
  ASSERT_NOK(session.ExecuteQuery("TRUNCATE TABLE users"));
}

TEST_F(CqlTest, CompactDeleteMarkers) {
  FLAGS_timestamp_history_retention_interval_sec = 0;
  FLAGS_history_cutoff_propagation_interval_ms = 1;
  FLAGS_TEST_transaction_ignore_applying_probability = 1.0;
  FLAGS_cleanup_intents_sst_files = false;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (i INT PRIMARY KEY) WITH transactions = { 'enabled' : true }"));
  ASSERT_OK(session.ExecuteQuery(
      "BEGIN TRANSACTION "
      "  INSERT INTO t (i) VALUES (42);"
      "END TRANSACTION;"));
  ASSERT_OK(session.ExecuteQuery("DELETE FROM t WHERE i = 42"));
  ASSERT_OK(cluster_->FlushTablets());
  FLAGS_TEST_transaction_ignore_applying_probability = 0.0;
  ASSERT_OK(WaitFor([this] {
    auto list = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    for (const auto& peer : list) {
      auto* participant = peer->tablet()->transaction_participant();
      if (!participant) {
        continue;
      }
      if (participant->MinRunningHybridTime() != HybridTime::kMax) {
        return false;
      }
    }
    return true;
  }, 10s, "Transaction complete"));
  ASSERT_OK(cluster_->CompactTablets(docdb::SkipFlush::kTrue));
  auto count_str = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT COUNT(*) FROM t"));
  ASSERT_EQ(count_str, "0");
}

class CqlRF1Test : public CqlTest {
 public:
  int num_tablet_servers() override {
    return 1;
  }
};

namespace {

class CompactionListener : public rocksdb::EventListener {
 public:
  void OnCompactionCompleted(rocksdb::DB* db, const rocksdb::CompactionJobInfo& ci) override {
    LOG(INFO) << "Compaction time: " << ci.stats.elapsed_micros;
  }
};

}

// Check that we correctly update SST file range values, after removing delete markers.
TEST_F_EX(CqlTest, RangeGC, CqlRF1Test) {
  constexpr int kKeys = 20;
  constexpr int kKeptKey = kKeys / 2;
  constexpr int kRangeMultiplier = 10;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE t (h INT, r INT, v INT, PRIMARY KEY ((h), r))"));
  auto insert_prepared = ASSERT_RESULT(session.Prepare("INSERT INTO t (h, r, v) VALUES (?, ?, ?)"));
  for (auto key : Range(kKeys)) {
    auto stmt = insert_prepared.Bind();
    stmt.Bind(0, key);
    stmt.Bind(1, key * kRangeMultiplier);
    stmt.Bind(2, -key);
    ASSERT_OK(session.Execute(stmt));
  }

  ASSERT_OK(cluster_->FlushTablets());

  auto delete_prepared = ASSERT_RESULT(session.Prepare("DELETE FROM t WHERE h = ?"));
  for (auto key : Range(kKeys)) {
    if (key == kKeptKey) {
      continue;
    }
    auto stmt = delete_prepared.Bind();
    stmt.Bind(0, key);
    ASSERT_OK(session.Execute(stmt));
  }

  ASSERT_OK(cluster_->CompactTablets());

  for (auto peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    auto* db = peer->tablet()->TEST_db();
    if (!db) {
      continue;
    }
    auto files = db->GetLiveFilesMetaData();
    for (const auto& file : files) {
      auto value = ASSERT_RESULT(docdb::KeyEntryValue::FullyDecodeFromKey(
          file.smallest.user_values[0].AsSlice()));
      ASSERT_EQ(value.GetInt32(), kKeptKey * kRangeMultiplier);
      value = ASSERT_RESULT(docdb::KeyEntryValue::FullyDecodeFromKey(
          file.largest.user_values[0].AsSlice()));
      ASSERT_EQ(value.GetInt32(), kKeptKey * kRangeMultiplier);
    }
  }
}

// This test fill table with multiple value columns and long (1KB) range value.
// Then performs compaction.
// Test was created to measure compaction performance for the above scenario.
TEST_F_EX(CqlTest, CompactRanges, CqlRF1Test) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
#ifndef NDEBUG
  constexpr int kKeys = 2000;
#else
  constexpr int kKeys = 20000;
#endif
  constexpr int kColumns = 10;
  const std::string kRangeValue = RandomHumanReadableString(1_KB);

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    cluster_->GetTabletManager(i)->TEST_tablet_options()->listeners.push_back(
        std::make_shared<CompactionListener>());
  }
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  std::string expr = "CREATE TABLE t (h INT, r TEXT";
  for (auto column : Range(kColumns)) {
    expr += Format(", v$0 INT", column);
  }
  expr += ", PRIMARY KEY ((h), r))";
  ASSERT_OK(session.ExecuteQuery(expr));

  expr = "INSERT INTO t (h, r";
  for (auto column : Range(kColumns)) {
    expr += Format(", v$0", column);
  }
  expr += ") VALUES (?, ?";
  for (auto column [[maybe_unused]] : Range(kColumns)) {
    expr += ", ?";
  }
  expr += ")";

  auto insert_prepared = ASSERT_RESULT(session.Prepare(expr));
  std::deque<CassandraFuture> futures;
  for (int key = 1; key <= kKeys; ++key) {
    while (!futures.empty() && futures.front().Ready()) {
      ASSERT_OK(futures.front().Wait());
      futures.pop_front();
    }
    if (futures.size() >= 0x40) {
      std::this_thread::sleep_for(10ms);
      continue;
    }
    auto stmt = insert_prepared.Bind();
    int idx = 0;
    stmt.Bind(idx++, key);
    stmt.Bind(idx++, kRangeValue);
    for (auto column : Range(kColumns)) {
      stmt.Bind(idx++, key * column);
    }
    futures.push_back(session.ExecuteGetFuture(stmt));
  }

  for (auto& future : futures) {
    ASSERT_OK(future.Wait());
  }

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());
}

TEST_F(CqlTest, ManyColumns) {
  constexpr int kNumRows = 10;
#ifndef NDEBUG
  constexpr int kColumns = RegularBuildVsSanitizers(100, 10);
#else
  constexpr int kColumns = 1000;
#endif

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  std::string expr = "CREATE TABLE t (id INT PRIMARY KEY";
  for (int i = 1; i <= kColumns; ++i) {
    expr += Format(", c$0 INT", i);
  }
  expr += ") WITH tablets = 1";
  ASSERT_OK(session.ExecuteQuery(expr));
  expr = "UPDATE t SET";
  for (int i = 2;; ++i) { // Don't set first column.
    expr += Format(" c$0 = ?", i);
    if (i == kColumns) {
      break;
    }
    expr += ",";
  }
  expr += " WHERE id = ?";
  auto insert_prepared = ASSERT_RESULT(session.Prepare(expr));

  for (int i = 1; i <= kNumRows; ++i) {
    auto stmt = insert_prepared.Bind();
    int idx = 0;
    for (int c = 2; c <= kColumns; ++c) {
      stmt.Bind(idx++, c);
    }
    stmt.Bind(idx++, i);
    ASSERT_OK(session.Execute(stmt));
  }
  auto start = CoarseMonoClock::Now();
  for (int i = 0; i <= 100; ++i) {
    auto value = ASSERT_RESULT(session.FetchValue<int64_t>("SELECT COUNT(c1) FROM t"));
    if (i == 0) {
      ASSERT_EQ(value, 0);
      start = CoarseMonoClock::Now();
    }
  }
  MonoDelta passed = CoarseMonoClock::Now() - start;
  LOG(INFO) << "Passed: " << passed;
}

TEST_F(CqlTest, AlteredSchemaVersion) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE t1 (i INT PRIMARY KEY, j INT) "));
  ASSERT_OK(session.ExecuteQuery("INSERT INTO t1 (i, j) VALUES (1, 1)"));

  CassandraResult res = ASSERT_RESULT(session.ExecuteWithResult("SELECT * FROM t1 WHERE i = 1"));
  ASSERT_EQ(res.RenderToString(), "1,1");

  // Run ALTER TABLE in another thread.
  struct AltererThread {
    explicit AltererThread(CppCassandraDriver* const driver) : driver_(driver) {
      EXPECT_OK(Thread::Create("cql-test", "AltererThread",
                               &AltererThread::ThreadBody, this, &thread_));
    }

    ~AltererThread() {
      thread_->Join();
    }

   private:
    void ThreadBody() {
      LOG(INFO) << "In thread: ALTER TABLE";
      auto session = ASSERT_RESULT(EstablishSession(driver_));
      ASSERT_OK(session.ExecuteQuery("ALTER TABLE t1 ADD k INT"));
      LOG(INFO) << "In thread: ALTER TABLE - DONE";
    }

    CppCassandraDriver* const driver_;
    scoped_refptr<Thread> thread_;
  } start_thread(driver_.get());

  const YBTableName table_name(YQL_DATABASE_CQL, kCqlTestKeyspace, "t1");
  SchemaVersion old_version = static_cast<SchemaVersion>(-1);

  while (true) {
    YBTableInfo info = ASSERT_RESULT(client_->GetYBTableInfo(table_name));
    LOG(INFO) << "Schema version=" << info.schema.version() << ": " << info.schema.ToString();
    if (info.schema.num_columns() == 2) { // Old schema.
      old_version = info.schema.version();
      std::this_thread::sleep_for(100ms);
    } else { // New schema.
      ASSERT_EQ(3, info.schema.num_columns());
      LOG(INFO) << "new-version=" << info.schema.version() << " old-version=" << old_version;
      // Ensure the new schema version is strictly bigger than the old schema version.
      ASSERT_LT(old_version, info.schema.version());
      break;
    }
  }

  res =  ASSERT_RESULT(session.ExecuteWithResult("SELECT * FROM t1 WHERE i = 1"));
  ASSERT_EQ(res.RenderToString(), "1,1,NULL");

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

}  // namespace yb

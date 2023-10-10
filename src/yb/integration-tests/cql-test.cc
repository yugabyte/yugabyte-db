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

#include "yb/client/client_master_rpc.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table_info.h"

#include "yb/consensus/raft_consensus.h"

#include "yb/dockv/primitive_value.h"

#include "yb/integration-tests/cql_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_ddl.proxy.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/jsonreader.h"
#include "yb/util/random_util.h"
#include "yb/util/range.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"

using std::string;

using namespace std::literals;

DECLARE_bool(cleanup_intents_sst_files);
DECLARE_bool(TEST_timeout_non_leader_master_rpcs);
DECLARE_bool(ycql_enable_packed_row);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_int64(cql_processors_limit);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(TEST_delay_tablet_export_metadata_ms);
DECLARE_int32(timestamp_history_retention_interval_sec);

DECLARE_int32(cql_unprepared_stmts_entries_limit);
DECLARE_int32(partitions_vtable_cache_refresh_secs);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_bool(disable_truncate_table);
DECLARE_bool(cql_always_return_metadata_in_execute_response);
DECLARE_bool(cql_check_table_schema_in_paging_state);
DECLARE_bool(use_cassandra_authentication);
DECLARE_bool(ycql_allow_non_authenticated_password_reset);

namespace yb {

using client::YBTableInfo;
using client::YBTableName;

class CqlTest : public CqlTestBase<MiniCluster> {
 public:
  virtual ~CqlTest() = default;

  void TestAlteredPrepare(bool metadata_in_exec_resp);
  void TestAlteredPrepareWithPaging(bool check_schema_in_paging,
                                    bool metadata_in_exec_resp = false);
  void TestAlteredPrepareForIndexWithPaging(bool check_schema_in_paging,
                                            bool metadata_in_exec_resp = false);
  void TestPrepareWithDropTableWithPaging();
  void TestCQLPreparedStmtStats();
};

TEST_F(CqlTest, ProcessorsLimit) {
  constexpr int kSessions = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cql_processors_limit) = 1;

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

TEST_F(CqlTest, TestDeleteListIndex) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  auto cql = [&](const std::string query) { ASSERT_OK(session.ExecuteQuery(query)); };
  cql("CREATE TABLE test(h INT, v LIST<INT>, PRIMARY KEY(h))");
  cql("INSERT INTO test (h, v) VALUES (1, [1, 2, 3])");

  cql("UPDATE test SET v = [4, 5, 6, 7, 8, 9, 10] where h = 1");
  cql("DELETE v[1], v[4] FROM test WHERE h = 1");
  ASSERT_EQ(ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM test")),
      "1,[4, 6, 7, 9, 10]");

  cql("DELETE v[3] FROM test WHERE h = 1");
  ASSERT_EQ(ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM test")),
      "1,[4, 6, 7, 10]");
}

TEST_F(CqlTest, TestDeleteMapKey) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  auto cql = [&](const std::string query) { ASSERT_OK(session.ExecuteQuery(query)); };
  cql("CREATE TABLE test(h INT, m map<int, varchar>, PRIMARY KEY(h))");
  cql("INSERT INTO test (h, m) VALUES (1,{12:'abcd', 13:'pqrs', 14:'xyzf', 21:'aqpr', 22:'xyab'})");

  cql("DELETE m[13], m[21] FROM test WHERE h = 1");
  ASSERT_EQ(ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM test")),
      "1,{12 => abcd, 14 => xyzf, 22 => xyab}");

  cql("DELETE m[14] FROM test WHERE h = 1");
  ASSERT_EQ(ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM test")),
      "1,{12 => abcd, 22 => xyab}");
}

TEST_F(CqlTest, Timeout) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 5000 * kTimeMultiplier;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (i INT PRIMARY KEY, j INT) WITH transactions = { 'enabled' : true }"));

  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  for (const auto& peer : peers) {
    ASSERT_RESULT(peer->GetRaftConsensus())->TEST_DelayUpdate(100ms);
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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_partitions_vtable_cache_refresh_secs) = 0;
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_timeout_non_leader_master_rpcs) = true;
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 5000;
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_intents_sst_files) = false;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (i INT PRIMARY KEY) WITH transactions = { 'enabled' : true }"));
  ASSERT_OK(session.ExecuteQuery(
      "BEGIN TRANSACTION "
      "  INSERT INTO t (i) VALUES (42);"
      "END TRANSACTION;"));
  ASSERT_OK(session.ExecuteQuery("DELETE FROM t WHERE i = 42"));
  ASSERT_OK(cluster_->FlushTablets());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0.0;
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
    auto* db = peer->tablet()->regular_db();
    if (!db) {
      continue;
    }
    auto files = db->GetLiveFilesMetaData();
    for (const auto& file : files) {
      auto value = ASSERT_RESULT(dockv::KeyEntryValue::FullyDecodeFromKey(
          file.smallest.user_values[0].AsSlice()));
      ASSERT_EQ(value.GetInt32(), kKeptKey * kRangeMultiplier);
      value = ASSERT_RESULT(dockv::KeyEntryValue::FullyDecodeFromKey(
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

  ActivateCompactionTimeLogging(cluster_.get());
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
  for (auto column [[maybe_unused]] : Range(kColumns)) { // NOLINT(whitespace/braces)
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

TEST_F_EX(CqlTest, DocDBKeyMetrics, CqlRF1Test) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 1;
  constexpr int kNumRows = 10;
  ActivateCompactionTimeLogging(cluster_.get());
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  std::string expr = "CREATE TABLE t (i INT, j INT, x INT";
  expr += ", PRIMARY KEY (i, j)) WITH CLUSTERING ORDER BY (j ASC)";
  ASSERT_OK(session.ExecuteQuery(expr));

  // Insert 10 rows, flush, and then select all 10.
  expr = "INSERT INTO t (i, j, x) VALUES (?, ?, ?);";
  auto insert_prepared = ASSERT_RESULT(session.Prepare(expr));
  for (int i = 1; i <= kNumRows; ++i) {
    auto stmt = insert_prepared.Bind();
    stmt.Bind(0, i);
    stmt.Bind(1, i);
    stmt.Bind(2, 123);
    ASSERT_OK(session.Execute(stmt));
  }
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(session.ExecuteQuery("SELECT * FROM t;"));

  // Find the counters for the tablet leader.
  tablet::TabletMetrics* tablet_metrics = nullptr;
  for (auto peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    auto* metrics = peer->tablet()->metrics();
    if (!metrics || metrics->Get(tablet::TabletCounters::kDocDBKeysFound) == 0) {
      continue;
    }
    tablet_metrics = metrics;
  }
  // Ensure we've found the tablet leader, and that we've seen 10 total keys (no obsolete).
  ASSERT_NE(tablet_metrics, nullptr);
  auto expected_total_keys = kNumRows;
  auto expected_obsolete_keys = 0;
  auto expected_obsolete_past_cutoff = 0;
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBKeysFound), expected_total_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFound),
      expected_obsolete_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFoundPastCutoff),
      expected_obsolete_past_cutoff);

  // Delete one row, then flush.
  ASSERT_OK(session.ExecuteQuery("DELETE FROM t WHERE i = 1;"));
  ASSERT_OK(cluster_->FlushTablets());
  expected_total_keys += 1;
  expected_obsolete_keys += 1;

  // Select all rows again. Should still see 10 total, 1 obsolete, but none obsolete past
  // the history cutoff window.
  ASSERT_OK(session.ExecuteQuery("SELECT * FROM t;"));
  expected_total_keys += kNumRows;
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBKeysFound), expected_total_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFound),
      expected_obsolete_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFoundPastCutoff),
      expected_obsolete_past_cutoff);

  // Wait 1 second for history retention to expire. Then try reading again. Expect 1 obsolete.
  std::this_thread::sleep_for(1s);
  ASSERT_OK(session.ExecuteQuery("SELECT * FROM t;"));
  expected_total_keys += kNumRows;
  expected_obsolete_keys += 1;
  expected_obsolete_past_cutoff += 1;
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBKeysFound), expected_total_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFound),
      expected_obsolete_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFoundPastCutoff),
      expected_obsolete_past_cutoff);

  // Set history retention to 900 for the rest of the test to make sure we don't exceed it.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 900;
  // Delete 2 more rows, then get the first row. Should see 3 keys, one of which is obsolete.
  // Obsolete past history cutoff should not change.
  ASSERT_OK(session.ExecuteQuery("DELETE FROM t WHERE i = 1;"));
  ASSERT_OK(session.ExecuteQuery("DELETE FROM t WHERE i = 5;"));
  ASSERT_OK(session.ExecuteQuery("DELETE FROM t WHERE i = 10;"));
  ASSERT_OK(cluster_->FlushTablets());
  expected_total_keys += 3;
  expected_obsolete_keys += 1;
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBKeysFound), expected_total_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFound),
      expected_obsolete_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFoundPastCutoff),
      expected_obsolete_past_cutoff);

  // Do another full range query. Obsolete keys increases by 3, total keys by 10.
  ASSERT_OK(session.ExecuteQuery("SELECT * FROM t;"));
  expected_total_keys += kNumRows;
  expected_obsolete_keys += 3;
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBKeysFound), expected_total_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFound),
      expected_obsolete_keys);
  ASSERT_EQ(
      tablet_metrics->Get(tablet::TabletCounters::kDocDBObsoleteKeysFoundPastCutoff),
      expected_obsolete_past_cutoff);
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

void CqlTest::TestAlteredPrepare(bool metadata_in_exec_resp) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cql_always_return_metadata_in_execute_response) =
      metadata_in_exec_resp;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE t1 (i INT PRIMARY KEY, j INT)"));
  ASSERT_OK(session.ExecuteQuery("INSERT INTO t1 (i, j) VALUES (1, 1)"));

  LOG(INFO) << "Prepare";
  auto sel_prepared = ASSERT_RESULT(session.Prepare("SELECT * FROM t1 WHERE i = 1"));
  auto ins_prepared = ASSERT_RESULT(session.Prepare("INSERT INTO t1 (i, j) VALUES (?, ?)"));

  LOG(INFO) << "Execute prepared - before Alter";
  CassandraResult res =  ASSERT_RESULT(session.ExecuteWithResult(sel_prepared.Bind()));
  ASSERT_EQ(res.RenderToString(), "1,1");

  ASSERT_OK(session.Execute(ins_prepared.Bind().Bind(0, 2).Bind(1, 2)));
  res =  ASSERT_RESULT(session.ExecuteWithResult("SELECT * FROM t1 WHERE i = 2"));
  ASSERT_EQ(res.RenderToString(), "2,2");

  LOG(INFO) << "Alter";
  auto session2 = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session2.ExecuteQuery("ALTER TABLE t1 ADD k INT"));
  ASSERT_OK(session2.ExecuteQuery("INSERT INTO t1 (i, k) VALUES (1, 9)"));

  LOG(INFO) << "Execute prepared - after Alter";
  res =  ASSERT_RESULT(session.ExecuteWithResult(sel_prepared.Bind()));
  if (metadata_in_exec_resp) {
    ASSERT_EQ(res.RenderToString(), "1,1,9");
  } else {
    ASSERT_EQ(res.RenderToString(), "1,1");
  }

  ASSERT_OK(session.Execute(ins_prepared.Bind().Bind(0, 3).Bind(1, 3)));
  res =  ASSERT_RESULT(session.ExecuteWithResult("SELECT * FROM t1 WHERE i = 3"));
  ASSERT_EQ(res.RenderToString(), "3,3,NULL");
}

TEST_F(CqlTest, AlteredPrepare) {
  TestAlteredPrepare(/* metadata_in_exec_resp =*/ false);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, AlteredPrepare_MetadataInExecResp) {
  TestAlteredPrepare(/* metadata_in_exec_resp =*/ true);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, TestCQLPreparedStmtStats) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  EasyCurl curl;
  faststring buf;
  std::vector<Endpoint> addrs;
  CHECK_OK(cql_server_->web_server()->GetBoundAddresses(&addrs));
  CHECK_EQ(addrs.size(), 1);
  LOG(INFO) << "Create Table";
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE t1 (i INT PRIMARY KEY, j INT)"));

  LOG(INFO) << "Prepare";
  auto sel_prepared = ASSERT_RESULT(session.Prepare("SELECT * FROM t1 WHERE i = ?"));
  auto ins_prepared = ASSERT_RESULT(session.Prepare("INSERT INTO t1 (i, j) VALUES (?, ?)"));

  for (int i = 0; i < 10; i++) {
    ASSERT_OK(session.Execute(ins_prepared.Bind().Bind(0, i).Bind(1, i)));
  }

  for (int i = 0; i < 9; i += 2) {
    CassandraResult res =  ASSERT_RESULT(
        session.ExecuteWithResult(sel_prepared.Bind().Bind(0, i)));
    ASSERT_EQ(res.RenderToString(), strings::Substitute("$0,$1", i, i));
  }

  ASSERT_OK(curl.FetchURL(strings::Substitute("http://$0/statements", ToString(addrs[0])), &buf));
  JsonReader r(buf.ToString());
  ASSERT_OK(r.Init());
  std::vector<const rapidjson::Value*> stmt_stats;
  ASSERT_OK(r.ExtractObjectArray(r.root(), "prepared_statements", &stmt_stats));
  ASSERT_EQ(2, stmt_stats.size());

  const rapidjson::Value* insert_stat = stmt_stats[1];
  string insert_query;
  ASSERT_OK(r.ExtractString(insert_stat, "query", &insert_query));
  ASSERT_EQ("INSERT INTO t1 (i, j) VALUES (?, ?)", insert_query);

  int64 insert_num_calls = 0;
  ASSERT_OK(r.ExtractInt64(insert_stat, "calls", &insert_num_calls));
  ASSERT_EQ(10, insert_num_calls);

  const rapidjson::Value* select_stat = stmt_stats[0];
  string select_query;
  ASSERT_OK(r.ExtractString(select_stat, "query", &select_query));
  ASSERT_EQ("SELECT * FROM t1 WHERE i = ?", select_query);

  int64 select_num_calls = 0;
  ASSERT_OK(r.ExtractInt64(select_stat, "calls", &select_num_calls));
  ASSERT_EQ(5, select_num_calls);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, TestCQLUnpreparedStmtStats) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  std::vector<Endpoint> addrs;
  CHECK_OK(cql_server_->web_server()->GetBoundAddresses(&addrs));
  CHECK_EQ(addrs.size(), 1);

  FLAGS_cql_unprepared_stmts_entries_limit = 205;
  const string create_table_stmt = "CREATE TABLE t1 (i INT PRIMARY KEY, j INT)";
  const string insert_stmt_1 = "INSERT INTO t1 (i, j) VALUES (1,11)";
  const string insert_stmt_2 = "INSERT INTO t1 (i, j) VALUES (2,22)";
  const string select_stmt_1 = "SELECT j FROM t1 WHERE i = 1";
  const string select_stmt_2 = "SELECT j FROM t1 WHERE i = 2";

  LOG(INFO) << "Create table";
  ASSERT_OK(session.ExecuteQuery(create_table_stmt));

  LOG(INFO) << "Insert statements";
  ASSERT_OK(session.ExecuteQuery(insert_stmt_1));
  ASSERT_OK(session.ExecuteQuery(insert_stmt_2));

  LOG(INFO) << "Select statements";
  const int num_select_queries = 100;
  for (int i = 0; i < num_select_queries; i++) {
    // We alternately execute the two select queries.
    ASSERT_OK(session.ExecuteQuery(i&1 ? select_stmt_1 : select_stmt_2));
  }

  EasyCurl curl;
  faststring buf;
  ASSERT_OK(curl.FetchURL(strings::Substitute("http://$0/statements", ToString(addrs[0])), &buf));

  JsonReader r(buf.ToString());
  ASSERT_OK(r.Init());
  std::vector<const rapidjson::Value*> stmt_stats;
  ASSERT_OK(r.ExtractObjectArray(r.root(), "unprepared_statements", &stmt_stats));

  string query_text;
  int64 obtained_num_calls = 0;
  for (auto const& stmt_stat : stmt_stats) {
    ASSERT_OK(r.ExtractString(stmt_stat, "query", &query_text));
    if (query_text == create_table_stmt) {
      ASSERT_OK(r.ExtractInt64(stmt_stat, "calls", &obtained_num_calls));
      ASSERT_EQ(1, obtained_num_calls);
    } else if (query_text == insert_stmt_1) {
      ASSERT_OK(r.ExtractInt64(stmt_stat, "calls", &obtained_num_calls));
      ASSERT_EQ(1, obtained_num_calls);
    } else if (query_text == insert_stmt_2) {
      ASSERT_OK(r.ExtractInt64(stmt_stat, "calls", &obtained_num_calls));
      ASSERT_EQ(1, obtained_num_calls);
    } else if (query_text == select_stmt_1) {
      ASSERT_OK(r.ExtractInt64(stmt_stat, "calls", &obtained_num_calls));
      ASSERT_EQ(num_select_queries/2, obtained_num_calls);
    } else if (query_text == select_stmt_2) {
      ASSERT_OK(r.ExtractInt64(stmt_stat, "calls", &obtained_num_calls));
      ASSERT_EQ(num_select_queries/2, obtained_num_calls);
    }
  }

  // reset the counters and verify
  ASSERT_OK(curl.FetchURL(strings::Substitute("http://$0/statements-reset",
                                              ToString(addrs[0])), &buf));
  ASSERT_OK(curl.FetchURL(strings::Substitute("http://$0/statements",
                                              ToString(addrs[0])), &buf));

  JsonReader json_post_reset(buf.ToString());
  ASSERT_OK(json_post_reset.Init());
  std::vector<const rapidjson::Value*> stmt_stats_post_reset;
  ASSERT_OK(json_post_reset.ExtractObjectArray(json_post_reset.root(), "unprepared_statements",
                                               &stmt_stats_post_reset));
  ASSERT_EQ(stmt_stats_post_reset.size(), 0);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

void CqlTest::TestAlteredPrepareWithPaging(bool check_schema_in_paging,
                                           bool metadata_in_exec_resp) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cql_check_table_schema_in_paging_state) =
      check_schema_in_paging;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cql_always_return_metadata_in_execute_response) =
      metadata_in_exec_resp;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t1 (i INT, j INT, PRIMARY KEY(i, j)) WITH CLUSTERING ORDER BY (j ASC)"));

  for (int j = 1; j <= 7; ++j) {
    ASSERT_OK(session.ExecuteQuery(Format("INSERT INTO t1 (i, j) VALUES (0, $0)", j)));
  }

  LOG(INFO) << "Client-1: Prepare";
  const string select_stmt = "SELECT * FROM t1";
  auto prepared = ASSERT_RESULT(session.Prepare(select_stmt));

  LOG(INFO) << "Client-1: Execute-1 prepared (for version 0 - 2 columns)";
  CassandraStatement stmt = prepared.Bind();
  stmt.SetPageSize(3);
  CassandraResult res = ASSERT_RESULT(session.ExecuteWithResult(stmt));
  ASSERT_EQ(res.RenderToString(), "0,1;0,2;0,3");
  stmt.SetPagingState(res);

  LOG(INFO) << "Client-2: Alter: ADD k";
  auto session2 = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session2.ExecuteQuery("ALTER TABLE t1 ADD k INT"));
  ASSERT_OK(session2.ExecuteQuery("INSERT INTO t1 (i, j, k) VALUES (0, 4, 99)"));

  LOG(INFO) << "Client-2: Prepare";
  auto prepared2 = ASSERT_RESULT(session2.Prepare(select_stmt));
  LOG(INFO) << "Client-2: Execute-1 prepared (for version 1 - 3 columns)";
  CassandraStatement stmt2 = prepared2.Bind();
  stmt2.SetPageSize(3);
  res = ASSERT_RESULT(session2.ExecuteWithResult(stmt2));
  ASSERT_EQ(res.RenderToString(), "0,1,NULL;0,2,NULL;0,3,NULL");
  stmt2.SetPagingState(res);

  LOG(INFO) << "Client-1: Execute-2 prepared (for version 0 - 2 columns)";
  if (check_schema_in_paging) {
    Status s = session.Execute(stmt);
    LOG(INFO) << "Expected error: " << s;
    ASSERT_TRUE(s.IsQLError());
    ASSERT_NE(s.message().ToBuffer().find(
        "Wrong Metadata Version: Table has been altered. Execute the query again. "
        "Requested schema version 0, got 1."), std::string::npos) << s;
  } else {
    res = ASSERT_RESULT(session.ExecuteWithResult(stmt));
    if (metadata_in_exec_resp) {
      ASSERT_EQ(res.RenderToString(), "0,4,99;0,5,NULL;0,6,NULL");
    } else {
      // The ral result - (0,4,99) (0,5,NULL) (0,6,NULL) - is interpreted by the driver in
      // accordance with the old schema - as <page-size>*(INT, INT): (0,4) (99,0,) (5,NULL).
      ASSERT_EQ(res.RenderToString(), "0,4;99,0;5,NULL");
    }
  }

  LOG(INFO) << "Client-2: Execute-2 prepared (for version 1 - 3 columns)";
  res = ASSERT_RESULT(session2.ExecuteWithResult(stmt2));
  ASSERT_EQ(res.RenderToString(), "0,4,99;0,5,NULL;0,6,NULL");
}

TEST_F(CqlTest, AlteredPrepareWithPaging) {
  TestAlteredPrepareWithPaging(/* check_schema_in_paging =*/ true);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, AlteredPrepareWithPaging_NoSchemaCheck) {
  TestAlteredPrepareWithPaging(/* check_schema_in_paging =*/ false);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, AlteredPrepareWithPaging_MetadataInExecResp) {
  TestAlteredPrepareWithPaging(/* check_schema_in_paging =*/ false,
                               /* metadata_in_exec_resp =*/ true);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

void CqlTest::TestPrepareWithDropTableWithPaging() {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t1 (i INT, j INT, PRIMARY KEY(i, j)) WITH CLUSTERING ORDER BY (j ASC)"));

  for (int j = 1; j <= 7; ++j) {
    ASSERT_OK(session.ExecuteQuery(Format("INSERT INTO t1 (i, j) VALUES (0, $0)", j)));
  }

  LOG(INFO) << "Client-1: Prepare";
  const string select_stmt = "SELECT * FROM t1";
  auto prepared = ASSERT_RESULT(session.Prepare(select_stmt));

  LOG(INFO) << "Client-1: Execute-1 prepared (for existing table)";
  CassandraStatement stmt = prepared.Bind();
  stmt.SetPageSize(3);
  CassandraResult res = ASSERT_RESULT(session.ExecuteWithResult(stmt));
  ASSERT_EQ(res.RenderToString(), "0,1;0,2;0,3");
  stmt.SetPagingState(res);

  LOG(INFO) << "Client-2: Drop table";
  auto session2 = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session2.ExecuteQuery("DROP TABLE t1"));

  LOG(INFO) << "Client-1: Execute-2 prepared (continue for deleted table)";
  Status s = session.Execute(stmt);
  LOG(INFO) << "Expected error: " << s;
  ASSERT_TRUE(s.IsServiceUnavailable());
  ASSERT_NE(s.message().ToBuffer().find(
      "All hosts in current policy attempted and were either unavailable or failed"),
      std::string::npos) << s;

  LOG(INFO) << "Client-1: Prepare (for deleted table)";
  auto prepare_res = session.Prepare(select_stmt);
  ASSERT_FALSE(prepare_res.ok());
  LOG(INFO) << "Expected error: " << prepare_res.status();
  ASSERT_TRUE(prepare_res.status().IsQLError());
  ASSERT_NE(prepare_res.status().message().ToBuffer().find(
      "Object Not Found"), std::string::npos) << prepare_res.status();

  LOG(INFO) << "Client-2: Prepare (for deleted table)";
  prepare_res = session2.Prepare(select_stmt);
  ASSERT_FALSE(prepare_res.ok());
  LOG(INFO) << "Expected error: " << prepare_res.status();
  ASSERT_TRUE(prepare_res.status().IsQLError());
  ASSERT_NE(prepare_res.status().message().ToBuffer().find(
      "Object Not Found"), std::string::npos) << prepare_res.status();
}

TEST_F(CqlTest, PrepareWithDropTableWithPaging) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cql_check_table_schema_in_paging_state) = true;
  TestPrepareWithDropTableWithPaging();
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, PrepareWithDropTableWithPaging_NoSchemaCheck) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cql_check_table_schema_in_paging_state) = false;
  TestPrepareWithDropTableWithPaging();
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

void CqlTest::TestAlteredPrepareForIndexWithPaging(
    bool check_schema_in_paging, bool metadata_in_exec_resp) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cql_check_table_schema_in_paging_state) = check_schema_in_paging;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cql_always_return_metadata_in_execute_response) =
      metadata_in_exec_resp;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(
      session.ExecuteQuery("CREATE TABLE t1 (i INT, j INT, x INT, v INT, PRIMARY KEY(i, j)) WITH "
                           "TRANSACTIONS = {'enabled': 'true'} AND CLUSTERING ORDER BY (j ASC)"));
  ASSERT_OK(
      session.ExecuteQuery("CREATE INDEX t1_x oN t1 (x)"));

  const client::YBTableName table_name(YQL_DATABASE_CQL, "test", "t1");
  const client::YBTableName index_table_name(YQL_DATABASE_CQL, "test", "t1_x");
  ASSERT_OK(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));

  for (int j = 1; j <= 7; ++j) {
    ASSERT_OK(session.ExecuteQuery(Format("INSERT INTO t1 (i, j, x, v) VALUES (0, $0, 10, 5)", j)));
  }
  ASSERT_OK(session.ExecuteQuery(Format("INSERT INTO t1 (i, j, x, v) VALUES (0, 8, 9, 5)")));

  // [Table only, Index only, Index + Table]
  std::vector<string> queries = {
      "SELECT * FROM t1", "SELECT x, j FROM t1 WHERE x = 10", "SELECT * FROM t1 WHERE x = 10"};
  std::vector<string> expectedFirstPageSession1 = {
      "0,1,10,5;0,2,10,5;0,3,10,5", "10,1;10,2;10,3", "0,1,10,5;0,2,10,5;0,3,10,5"};
  std::vector<string> expectedFirstPageSession2 = {
      "0,1,10,5,NULL;0,2,10,5,NULL;0,3,10,5,NULL", "10,1;10,2;10,3",
      "0,1,10,5,NULL;0,2,10,5,NULL;0,3,10,5,NULL"};
  std::vector<string> expectedSecondPageSession1 = {
      "0,4,10,5,NULL;0,5,10,5,NULL;0,6,10,5,NULL", "10,4;10,5;10,6",
      "0,4,10,5,99;0,5,10,5,NULL;0,6,10,5,NULL"};
  std::vector<string> expectedSecondPageSession2 = {
      "0,4,10,5,99;0,5,10,5,NULL;0,6,10,5,NULL", "10,4;10,5;10,6",
      "0,4,10,5,99;0,5,10,5,NULL;0,6,10,5,NULL"};
  // The returned result is (0,4,10,5,NULL) (0,5,10,5,NULL) (0,6,10,5,NULL).
  // The result is interpreted by the driver in accordance with the old schema in the absence of
  // metadata - as <page-size>*(INT, INT, INT, INT): (0, 4, 10, 5) (NULL, 0, 5, 10) (5, NULL, 0, 6).
  std::vector<string> expectedSecondPageWithoutMetadata = {
      "0,4,10,5;NULL,0,5,10;5,NULL,0,6", "10,4;10,5;10,6", "0,4,10,5;99,0,5,10;5,NULL,0,6"};

  for (size_t i = 0; i < queries.size(); i++) {
    auto& query = queries[i];

    LOG(INFO) << "Query: " << query;
    LOG(INFO) << "Client-1: Prepare";
    auto session1 = ASSERT_RESULT(EstablishSession(driver_.get()));
    auto prepared = ASSERT_RESULT(session1.Prepare(query));

    LOG(INFO) << "Client-1: Execute-1 prepared (for schema version 0: 4 columns)";
    CassandraStatement stmt = prepared.Bind();
    stmt.SetPageSize(3);
    CassandraResult res = ASSERT_RESULT(session1.ExecuteWithResult(stmt));
    ASSERT_EQ(res.RenderToString(), expectedFirstPageSession1[i]);
    stmt.SetPagingState(res);

    LOG(INFO) << "Client-2: Alter: ADD k";
    auto session2 = ASSERT_RESULT(EstablishSession(driver_.get()));
    ASSERT_OK(session2.ExecuteQuery("ALTER TABLE t1 ADD k INT"));
    ASSERT_OK(session2.ExecuteQuery("INSERT INTO t1 (i, j, x, v, k) VALUES (0, 4, 10, 5, 99)"));

    LOG(INFO) << "Client-2: Prepare";
    auto prepared2 = ASSERT_RESULT(session2.Prepare(query));
    LOG(INFO) << "Client-2: Execute-1 prepared (for schema version 1: 5 columns)";
    CassandraStatement stmt2 = prepared2.Bind();
    stmt2.SetPageSize(3);
    res = ASSERT_RESULT(session2.ExecuteWithResult(stmt2));
    ASSERT_EQ(res.RenderToString(), expectedFirstPageSession2[i]);
    stmt2.SetPagingState(res);

    LOG(INFO) << "Client-1: Execute-2 prepared (for schema version 0: 4 columns)";
    if (check_schema_in_paging) {
      Status s = session1.Execute(stmt);
      LOG(INFO) << "Expcted error: " << s;
      ASSERT_TRUE(s.IsQLError());
      ASSERT_NE(
          s.message().ToBuffer().find(
              "Wrong Metadata Version: Table has been altered. Execute the query again. "),
          std::string::npos)
          << s;
    } else {
      res = ASSERT_RESULT(session1.ExecuteWithResult(stmt));
      if (metadata_in_exec_resp) {
        ASSERT_EQ(res.RenderToString(), expectedSecondPageSession1[i]);
      } else {
        ASSERT_EQ(res.RenderToString(), expectedSecondPageWithoutMetadata[i]);
      }
    }

    LOG(INFO) << "Client-2: Execute-2 prepared (for schema version 1: 5 columns)";
    res = ASSERT_RESULT(session2.ExecuteWithResult(stmt2));
    ASSERT_EQ(res.RenderToString(), expectedSecondPageSession2[i]);

    // Revert alter table back.
    ASSERT_OK(session2.ExecuteQuery("ALTER TABLE t1 DROP k"));
  }
}

TEST_F(CqlTest, AlteredPrepareForIndexWithPaging) {
  TestAlteredPrepareForIndexWithPaging(/* check_schema_in_paging =*/true);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, AlteredPrepareForIndexWithPaging_NoSchemaCheck) {
  TestAlteredPrepareForIndexWithPaging(/* check_schema_in_paging =*/false);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, AlteredPrepareForIndexWithPaging_MetadataInExecResp) {
  TestAlteredPrepareForIndexWithPaging(
      /* check_schema_in_paging =*/false,
      /* metadata_in_exec_resp =*/true);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, PasswordReset) {
  const string change_pwd = "ALTER ROLE cassandra WITH PASSWORD = 'updated_password'";

  // Password reset disallowed when ycql_allow_non_authenticated_password_reset = false.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = false;
  {
    ASSERT_FALSE(FLAGS_ycql_allow_non_authenticated_password_reset);
    auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
    Status s = session.ExecuteQuery(change_pwd);
    ASSERT_NOK(s);
    ASSERT_NE(s.message().ToBuffer().find("Unauthorized."), std::string::npos) << s;
  }

  // Password reset allowed when ycql_allow_non_authenticated_password_reset = true.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_allow_non_authenticated_password_reset) = true;
  {
    auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
    ASSERT_OK(session.ExecuteQuery(change_pwd));
  }

  // Login works with the updated password and the user is able to create a superuser.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_allow_non_authenticated_password_reset) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cassandra_authentication) = true;
  driver_->SetCredentials("cassandra", "updated_password");
  {
    auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
    ASSERT_OK(session.ExecuteQuery("CREATE ROLE superuser_role WITH SUPERUSER = true AND LOGIN = "
        "true AND PASSWORD = 'superuser_password'"));
  }

  // The created superuser account login works.
  driver_->SetCredentials("superuser_role", "superuser_password");
  ASSERT_RESULT(EstablishSession(driver_.get()));
}

TEST_F(CqlTest, SelectAggregateFunctions) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE tbl (k INT PRIMARY KEY, v INT, c INT) "
                                 "WITH tablets = 5 AND transactions = { 'enabled' : true }"));
  ASSERT_OK(session.ExecuteQuery("CREATE INDEX ON tbl (v)"));

  auto cql = [&](int page_size, const string& query, const string& result) {
      LOG(INFO) << "Page=" << page_size << " Execute query: " << query;
      CassandraStatement stmt(query);
      stmt.SetPageSize(page_size);
      CassandraResult res = ASSERT_RESULT(session.ExecuteWithResult(stmt));
      LOG(INFO) << "Result=" << res.RenderToString() << " HasMorePages=" << res.HasMorePages();
      ASSERT_EQ(result, res.RenderToString());
      // Expecting single-page result - ignoring ycqlsh command like 'Paging 1'.
      ASSERT_FALSE(res.HasMorePages());
    };

  for (int i = 1; i <= 20; ++i) {
    ASSERT_OK(session.ExecuteQuery(
        Format("INSERT INTO tbl (k, v, c) VALUES ($0, $1, $2)", i, 10*i, 100*i)));
  }

  for (int page = 1; page <= 2; ++page) {
    // LIMIT clause must be ignored for the aggregate functions.
    for (string limit : {"", " LIMIT 1", " LIMIT 2"}) {
      cql(page, "select count(*) from tbl" + limit, "20");
      cql(page, "select count(k), min(k), avg(k), max(k), sum(k) from tbl" + limit,
                "20,1,10,20,210");
      cql(page, "select count(v), min(v), avg(v), max(v), sum(v) from tbl" + limit,
                "20,10,105,200,2100");
      cql(page, "select count(c), min(c), avg(c), max(c), sum(c) from tbl" + limit,
                "20,100,1050,2000,21000");

      // Test WHERE clause.
      // Seq scan.
      cql(page, "select count(k), min(k), avg(k), max(k), sum(k) from tbl where k != 1" + limit,
                "19,2,11,20,209");
      cql(page, "select count(v), min(v), avg(v), max(v), sum(v) from tbl where v > 50" + limit,
                "15,60,130,200,1950");
      cql(page, "select count(c), min(c), avg(c), max(c), sum(c) from tbl where v > 50" + limit,
                "15,600,1300,2000,19500");
      // Index Only scan. (Covering index.)
      cql(page, "select count(v), min(v), avg(v), max(v), sum(v) from tbl where v = 50" + limit,
                "1,50,50,50,50");
      cql(page, "select count(v), min(v), avg(v), max(v), sum(v) from tbl "
                    "where v in (50,60,70,80,90)" + limit,
                "5,50,70,90,350");
      cql(page, "select count(k), min(k), avg(k), max(k), sum(k) from tbl where v = 50" + limit,
                "1,5,5,5,5");
      // Index scan. (Non-covering index.)
      cql(page, "select count(c), min(c), avg(c), max(c), sum(c) from tbl where v = 50" + limit,
                "1,500,500,500,500");
      cql(page, "select count(c), min(c), avg(c), max(c), sum(c) from tbl "
                    "where v in (50,60,70,80,90)" + limit,
                "5,500,700,900,3500");
    }
  }

  ASSERT_OK(session.ExecuteQuery("TRUNCATE TABLE tbl"));
  // Same value v=1 for all rows to test if all tablets are scanned when a secondary index is used.
  for (int i = 1; i <= 20; ++i) {
    ASSERT_OK(session.ExecuteQuery(
        Format("INSERT INTO tbl (k, v, c) VALUES ($0, 1, $1)", i, 100*i)));
  }

  for (int page = 1; page <= 2; ++page) {
    // LIMIT clause must be ignored for the aggregate functions.
    for (string limit : {"", " LIMIT 1", " LIMIT 2"}) {
      // Test WHERE clause.
      // Index Only scan. (Covering index.)
      cql(page, "select min(k) from tbl where v = 1" + limit, "1");
      cql(page, "select count(k) from tbl where v = 1" + limit, "20");
      cql(page, "select count(k), min(k), avg(k), max(k), sum(k) from tbl where v = 1" + limit,
                "20,1,10,20,210");
      // Index scan. (Non-covering index.)
      cql(page, "select min(c) from tbl where v = 1" + limit, "100");
      cql(page, "select count(c) from tbl where v = 1" + limit, "20");
      cql(page, "select count(c), min(c), avg(c), max(c), sum(c) from tbl where v = 1" + limit,
                "20,100,1050,2000,21000");
    }
  }

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, CheckStateAfterDrop) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  auto cql = [&](const string query) { ASSERT_OK(session.ExecuteQuery(query)); };

  cql("create table tbl (h1 int primary key, c1 int, c2 int, c3 int, c4 int, c5 int) "
      "with transactions = {'enabled' : true}");
  for (int j = 1; j <= 20; ++j) {
    cql(Format("insert into tbl (h1, c1, c2, c3, c4, c5) VALUES ($0, $0, $0, $0, $0, $0)", j));
  }

  const int num_indexes = 5;
  // Initiate the Index Backfilling.
  for (int i = 1; i <= num_indexes; ++i) {
    cql(Format("create index i$0 on tbl (c$0)", i));
  }

  // Wait here for the creation end.
  // Note: in JAVA tests we have 'waitForReadPermsOnAllIndexes' for the same.
  const YBTableName table_name(YQL_DATABASE_CQL, kCqlTestKeyspace, "tbl");
  for (int i = 1; i <= num_indexes; ++i) {
    const TableName name = Format("i$0", i);
    const client::YBTableName index_name(YQL_DATABASE_CQL, kCqlTestKeyspace, name);
    ASSERT_OK(LoggedWaitFor(
        [this, table_name, index_name]() {
          auto result = client_->WaitUntilIndexPermissionsAtLeast(
              table_name, index_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
          return result.ok() && *result == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE;
        },
        90s,
        "wait for create index '" + name + "' to complete",
        12s));
  }

  class CheckHelper {
    master::CatalogManager& catalog_manager;
   public:
    explicit CheckHelper(MiniCluster* cluster) : catalog_manager(CHECK_NOTNULL(EXPECT_RESULT(
        CHECK_NOTNULL(cluster)->GetLeaderMiniMaster()))->catalog_manager_impl()) {}

    Result<bool> IsDeleteDone(const TableId& table_id) {
      master::IsDeleteTableDoneRequestPB req;
      master::IsDeleteTableDoneResponsePB resp;
      req.set_table_id(table_id);
      const Status s = catalog_manager.IsDeleteTableDone(&req, &resp);
      LOG(INFO) << "IsDeleteTableDone response for [id=" << table_id << "]: " << resp.DebugString();
      if (!s.ok()) {
        return client::internal::StatusFromResp(resp);
      }
      return resp.done();
    }

    // Check State for all listed running & deleted tables.
    // Return TRUE if all deleted tables are in DELETED state, else return FALSE for waiting.
    Result<bool> AreTablesDeleted(const std::vector<string>& running,
                                  const std::vector<string>& deleted,
                                  bool retry = true) {
      using master::SysTablesEntryPB;

      master::ListTablesRequestPB req;
      master::ListTablesResponsePB resp;
      req.set_include_not_running(true);
      req.set_exclude_system_tables(true);
      // Get all user tables.
      RETURN_NOT_OK(catalog_manager.ListTables(&req, &resp));
      LOG(INFO) << "ListTables returned " << resp.tables().size() << " tables";

      // Check RUNNNING tables.
      for (const string& name : running) {
        for (const auto& table : resp.tables()) {
          if (table.name() == name) {
            LOG(INFO) << "Running '" << table.name() << "' [id=" << table.id() << "] state: "
                      << SysTablesEntryPB_State_Name(table.state());
            SCHECK(table.state() == SysTablesEntryPB::RUNNING ||
                   table.state() == SysTablesEntryPB::ALTERING, IllegalState,
                   Format("Bad running table state: $0",
                       SysTablesEntryPB_State_Name(table.state())));
          }
        }
      }

      // Check DELETED tables.
      bool all_deleted = true;
      for (const string& name : deleted) {
        for (const auto& table : resp.tables()) {
          if (table.name() == name) {
            LOG(INFO) << "Deleted '" << table.name() << "' [id=" << table.id() << "] state: "
                      << SysTablesEntryPB_State_Name(table.state());
            SCHECK(table.state() == SysTablesEntryPB::DELETING ||
                   table.state() == SysTablesEntryPB::DELETED, IllegalState,
                   Format("Bad deleted table state: $0",
                       SysTablesEntryPB_State_Name(table.state())));
            const bool deleted_state = (table.state() == master::SysTablesEntryPB::DELETED);
            all_deleted = all_deleted && deleted_state;

            const bool done = VERIFY_RESULT(IsDeleteDone(table.id()));
            if (retry) {
              if (done != deleted_state) {
                // Handle race betrween first ListTables() and this IsDeleteTableDone() calls.
                LOG(INFO) << "Found difference between ListTables & IsDeleteTableDone - retry...";
                return AreTablesDeleted(running, deleted, false);
              }
            } else {
              SCHECK(done == deleted_state, IllegalState,
                     Format("Incorrect 'done' value: $0, state is $1",
                         done, SysTablesEntryPB_State_Name(table.state())));
            }
          }
        }
      }

      return all_deleted;
    }
  } check(cluster_.get());

  const TableId table_id = ASSERT_RESULT(client::GetTableId(client_.get(), table_name));

  // IsDeleteTableDone() for RUNNING table should fail.
  Result<bool> res_done = check.IsDeleteDone(table_id);
  ASSERT_NOK(res_done);
  LOG(INFO) << "Expected error: " << res_done.status();
  // Error example: MasterErrorPB::INVALID_REQUEST:
  //     IllegalState: The object was NOT deleted: Current schema version=17
  ASSERT_EQ(master::MasterError(res_done.status()), master::MasterErrorPB::INVALID_REQUEST);
  ASSERT_TRUE(res_done.status().IsIllegalState());
  ASSERT_STR_CONTAINS(res_done.status().message().ToBuffer(), "The object was NOT deleted");

  ASSERT_TRUE(ASSERT_RESULT(check.AreTablesDeleted({"tbl", "i1", "i2"}, {})));

  cql("drop index i1");
  ASSERT_OK(Wait([&check]() -> Result<bool> {
    return check.AreTablesDeleted({"tbl", "i2"}, {"i1"});
  }, CoarseMonoClock::now() + 30s, "Deleted index cleanup"));

  cql("drop table tbl");
  ASSERT_OK(Wait([&check]() -> Result<bool> {
    return check.AreTablesDeleted({}, {"tbl", "i1", "i2"});
  }, CoarseMonoClock::now() + 30s, "Deleted table cleanup"));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, RetainSchemaPacking) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_tablet_export_metadata_ms) = 1000;

  client::SnapshotTestUtil snapshot_util;
  auto client = snapshot_util.InitWithCluster(cluster_.get());

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE t (key INT PRIMARY KEY, value INT)"));
  for (int i = 0; i != 1000; ++i) {
    ASSERT_OK(session.ExecuteQueryFormat("INSERT INTO t (key, value) VALUES ($0, $1)", i, i * 2));
  }

  ASSERT_OK(session.ExecuteQuery("ALTER TABLE t ADD extra INT"));

  auto snapshot_id = ASSERT_RESULT(snapshot_util.StartSnapshot(
      client::YBTableName(YQLDatabase::YQL_DATABASE_CQL, "test", "t")));
  ASSERT_OK(cluster_->CompactTablets());

  ASSERT_OK(snapshot_util.WaitSnapshotDone(snapshot_id));

  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id));

  auto content = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
  LOG(INFO) << "Content: " << content;
}

}  // namespace yb

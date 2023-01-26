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
#include "yb/client/table_info.h"

#include "yb/consensus/raft_consensus.h"

#include "yb/integration-tests/cql_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_ddl.proxy.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/util/random_util.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

DECLARE_bool(TEST_timeout_non_leader_master_rpcs);
DECLARE_int64(cql_processors_limit);
DECLARE_int32(client_read_write_timeout_ms);

DECLARE_string(TEST_fail_to_fast_resolve_address);
DECLARE_int32(partitions_vtable_cache_refresh_secs);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_bool(cql_always_return_metadata_in_execute_response);
DECLARE_bool(cql_check_table_schema_in_paging_state);

namespace yb {

using client::YBTableInfo;
using client::YBTableName;

class CqlTest : public CqlTestBase<MiniCluster> {
 public:
  virtual ~CqlTest() = default;

  void TestAlteredPrepare(bool metadata_in_exec_resp);
  void TestAlteredPrepareWithPaging(bool check_schema_in_paging,
                                    bool metadata_in_exec_resp = false);
  void TestPrepareWithDropTableWithPaging();
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
  auto cql = [&](const std::string query) {
    ASSERT_OK(session.ExecuteQuery(query));
  };
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

  auto prepared = ASSERT_RESULT(session.Prepare(
      "BEGIN TRANSACTION "
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
    requests.push_back(Request {
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

  int num_masters() override {
    return 3;
  }
};

Status CheckNumAddressesInYqlPartitionsTable(CassandraSession* session, int expected_num_addrs) {
  const int kReplicaAddressesIndex = 5;
  auto result = VERIFY_RESULT(session->ExecuteWithResult("SELECT * FROM system.partitions"));
  auto iterator = result.CreateIterator();
  while (iterator.Next()) {
    auto replica_addresses = iterator.Row().Value(kReplicaAddressesIndex).ToString();
    int num_addrs = 0;
    if (replica_addresses.size() > std::strlen("{}")) {
      num_addrs = std::count(replica_addresses.begin(), replica_addresses.end(), ',') + 1;
    }

    EXPECT_EQ(num_addrs, expected_num_addrs);
  }
  return Status::OK();
}

TEST_F_EX(CqlTest, HostnameResolutionFailureInYqlPartitionsTable, CqlThreeMastersTest) {
  google::FlagSaver flag_saver;
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(CheckNumAddressesInYqlPartitionsTable(&session, 3));

  // TEST_RpcAddress is 1-indexed.
  string hostname = server::TEST_RpcAddress(cluster_->LeaderMasterIdx() + 1,
                                            server::Private::kFalse);

  // Fail resolution of the old leader master's hostname.
  FLAGS_TEST_fail_to_fast_resolve_address = hostname;
  LOG(INFO) << "Setting FLAGS_TEST_fail_to_fast_resolve_address to: "
            << FLAGS_TEST_fail_to_fast_resolve_address;

  // Shutdown the master leader, and wait for new leader to get elected.
  ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Shutdown();
  ASSERT_RESULT(cluster_->GetLeaderMiniMaster());

  // Assert that a new call will succeed, but will be missing the shutdown master address.
  ASSERT_OK(CheckNumAddressesInYqlPartitionsTable(&session, 2));
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
  FLAGS_cql_always_return_metadata_in_execute_response = metadata_in_exec_resp;

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

void CqlTest::TestAlteredPrepareWithPaging(bool check_schema_in_paging,
                                           bool metadata_in_exec_resp) {
  FLAGS_cql_check_table_schema_in_paging_state = check_schema_in_paging;
  FLAGS_cql_always_return_metadata_in_execute_response = metadata_in_exec_resp;

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
  FLAGS_cql_check_table_schema_in_paging_state = true;
  TestPrepareWithDropTableWithPaging();
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlTest, PrepareWithDropTableWithPaging_NoSchemaCheck) {
  FLAGS_cql_check_table_schema_in_paging_state = false;
  TestPrepareWithDropTableWithPaging();
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
  // Error example: MasterErrorPB::OBJECT_NOT_FOUND:
  //     IllegalState: The object was NOT deleted: Current schema version=17
  ASSERT_EQ(master::MasterError(res_done.status()), master::MasterErrorPB::OBJECT_NOT_FOUND);
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

} // namespace yb

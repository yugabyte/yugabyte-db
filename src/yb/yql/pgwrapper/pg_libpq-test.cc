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

#include <signal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <future>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/lexical_cast.hpp>

#include "yb/client/client_fwd.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"
#include "yb/client/client-test-util.h"

#include "yb/common/colocated_util.h"
#include "yb/common/common.pb.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.proxy.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"

#include "yb/tserver/tserver_util_fwd.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/barrier.h"
#include "yb/util/cast.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/os-util.h"
#include "yb/util/path_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_mem.h"
#include "yb/util/status_log.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using std::future;
using std::pair;
using std::promise;
using std::string;

using namespace std::literals;

DEFINE_NON_RUNTIME_int32(num_iter, 10000, "Number of iterations to run StaleMasterReads test");

DECLARE_int64(external_mini_cluster_max_log_bytes);

METRIC_DECLARE_entity(tablet);
METRIC_DECLARE_counter(transaction_not_found);

METRIC_DECLARE_entity(server);
METRIC_DECLARE_counter(rpc_inbound_calls_created);

namespace yb {
namespace pgwrapper {

class PgLibPqTest : public LibPqTestBase {
 protected:
  typedef std::function<Result<master::TabletLocationsPB>(client::YBClient* client,
                                                          std::string database_name,
                                                          PGConn* conn,
                                                          MonoDelta timeout)>
                                                          GetParentTableTabletLocation;
  typedef pair<promise<Result<client::internal::RemoteTabletPtr>>,
               future<Result<client::internal::RemoteTabletPtr>>> promise_future_pair;

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Let colocated database related tests cover new Colocation GA implementation instead of legacy
    // colocated database.
    options->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=false");
  }

  void TestMultiBankAccount(IsolationLevel isolation, const bool colocation = false);

  void DoIncrement(int key, int num_increments, IsolationLevel isolation, bool lock_first = false);

  void TestParallelCounter(IsolationLevel isolation);

  void TestConcurrentCounter(IsolationLevel isolation, bool lock_first = false);

  void TestOnConflict(bool kill_master, const MonoDelta& duration);

  void TestCacheRefreshRetry(const bool is_retry_disabled);

  void CreateDatabaseWithTablegroup(
      const string database_name, const string tablegroup_name, yb::pgwrapper::PGConn* conn);

  void PerformSimultaneousTxnsAndVerifyConflicts(
      const string database_name, bool colocated, const string tablegroup_name = "",
      const string query_statement = "SELECT * FROM t FOR UPDATE");

  void FlushTablesAndPerformBootstrap(
      const string database_name,
      const int timeout_secs,
      bool colocated,
      const bool test_backward_compatibility,
      const string tablegroup_name = "");

  void FlushTablesAndCreateData(
      const string database_name,
      const int timeout_secs,
      bool colocated,
      const string tablegroup_name = "");

  void AddTSToLoadBalanceSingleInstance(
      const auto timeout, const std::map<std::string, int>& ts_loads,
      const std::unique_ptr<yb::client::YBClient>& client);

  void AddTSToLoadBalanceMultipleInstances(
      const auto timeout, const std::unique_ptr<yb::client::YBClient>& client);

  void VerifyLoadBalance(const std::map<std::string, int>& ts_loads);

  void TestTableColocation(GetParentTableTabletLocation getParentTableTabletLocation);

  void TestLoadBalanceSingleColocatedDB(GetParentTableTabletLocation getParentTableTabletLocation);

  void TestLoadBalanceMultipleColocatedDB(
      GetParentTableTabletLocation getParentTableTabletLocation);

  void TestTableColocationEnabledByDefault(
      GetParentTableTabletLocation getParentTableTabletLocation);

  Status TestDuplicateCreateTableRequest(PGConn conn);

  void KillPostmasterProcessOnTservers();

  Result<string> GetSchemaName(const string& relname, PGConn* conn);

 private:
  Result<PGConn> RestartTSAndConnectToPostgres(int ts_idx, const std::string& db_name);
};

class PgLibPqFailOnConflictTest : public PgLibPqTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    UpdateMiniClusterFailOnConflict(options);
    PgLibPqTest::UpdateMiniClusterOptions(options);
  }
};

static Result<PgOid> GetTablegroupOid(PGConn* conn, const std::string& tablegroup_name) {
  return conn->FetchRow<PGOid>(
      Format("SELECT oid FROM pg_yb_tablegroup WHERE grpname = '$0'", tablegroup_name));
}

TEST_F(PgLibPqTest, Simple) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));

  const auto row = ASSERT_RESULT((conn.FetchRow<int32_t, std::string>("SELECT * FROM t")));
  ASSERT_EQ(row, (decltype(row){1, "hello"}));
}

// Make sure index scan queries that error at the beginning of scanning don't bump up the pgstat
// idx_scan metric.
TEST_F(PgLibPqTest, PgStatIdxScanNoIncrementOnErrorTest) {
  auto conn = ASSERT_RESULT(Connect());
  constexpr auto kNumColumns = 30;
  constexpr auto kMaxPredicates = 64;
  // This matches PGSTAT_STAT_INTERVAL.
  const auto kPgstatStatInterval = 500ms;
  // This matches PGSTAT_MAX_WAIT_TIME.
  const auto kPgstatMaxWaitTime = 10000ms;

  std::ostringstream create_table_ss;
  create_table_ss << "CREATE TABLE many (";
  for (int i = 1; i <= kNumColumns; ++i)
    create_table_ss << "c" << i << " INT,";
  create_table_ss << "k INT PRIMARY KEY)";
  ASSERT_OK(conn.Execute(create_table_ss.str()));

  std::ostringstream create_index_ss;
  create_index_ss << "CREATE INDEX ON many (c1 ASC";
  for (int i = 2; i <= kNumColumns; ++i)
    create_index_ss << ", c" << i;
  create_index_ss << ")";
  ASSERT_OK(conn.Execute(create_index_ss.str()));

  // There should be only two indexes: pkey index and secondary index.  We want to get stats for the
  // secondary index, but its name is too long, so filter by != 'many_pkey'.
  const auto idx_scan_query =
      "SELECT idx_scan FROM pg_stat_user_indexes WHERE indexrelname != 'many_pkey'";
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(idx_scan_query)), 0);

  std::ostringstream query_ss;
  query_ss << "SELECT k FROM many WHERE TRUE";
  auto num_predicates = 0;
  for (int i = 1; i <= kNumColumns && num_predicates < kMaxPredicates; ++i, ++num_predicates) {
    query_ss << " AND c" << i << " = 1";
  }
  for (int i = 1; i <= kNumColumns && num_predicates < kMaxPredicates; ++i, ++num_predicates) {
    query_ss << " AND c" << i << " > 0";
  }
  for (int i = 1; i <= kNumColumns && num_predicates < kMaxPredicates; ++i, ++num_predicates) {
    query_ss << " AND c" << i << " < 2";
  }

  // Successful scan should increment idx_scan.
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query_ss.str())));
  ASSERT_OK(conn.FetchMatrix(query_ss.str(), 0, 1));
  // Stats can take time to update, so retry-loop.
  ASSERT_OK(LoggedWaitFor(
      [&conn, &idx_scan_query]() -> Result<bool> {
        return VERIFY_RESULT(conn.FetchRow<int64_t>(idx_scan_query)) == 1;
      },
      kPgstatMaxWaitTime,
      "idx_scan == 1"));

  // Add last predicate, which should not have been added from above and therefore is a new
  // predicate.
  static_assert(kMaxPredicates < kNumColumns * 3);
  query_ss << " AND c" << kNumColumns << " < 2";

  // Unsuccessful scan should not increment idx_scan.
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query_ss.str())));
  auto status = ResultToStatus(conn.Fetch(query_ss.str()));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(),
                      Format("ERROR:  cannot use more than $0 predicates in a table or index scan",
                             kMaxPredicates));
  // To avoid sleeping too long, wait the minimum time (x2) rather than maximum time.  In most
  // cases, the stats should be updated at the minimum pace, so this should be sufficient to catch
  // regressions.
  SleepFor(kPgstatStatInterval * 2);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(idx_scan_query)), 1);
}

TEST_F_EX(PgLibPqTest, SerializableColoring, PgLibPqFailOnConflictTest) {
  SerializableColoringHelper();
}

TEST_F_EX(PgLibPqTest, SerializableReadWriteConflict, PgLibPqFailOnConflictTest) {
  const auto kKeys = RegularBuildVsSanitizers(20, 5);
  const auto kNumTries = RegularBuildVsSanitizers(4, 1);
  auto tries = 1;
  for (; tries <= kNumTries; ++tries) {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS t"));
    ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));

    size_t reads_won = 0, writes_won = 0;
    for (int i = 0; i != kKeys; ++i) {
      // With fail-on-conflict concurrency control, we expect one of these operations to fail.
      // Otherwise, one of them will block indefinitely.
      auto read_conn = ASSERT_RESULT(Connect());
      ASSERT_OK(read_conn.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE"));
      auto res = read_conn.FetchFormat("SELECT * FROM t WHERE key = $0", i);
      auto read_status = ResultToStatus(res);

      auto write_conn = ASSERT_RESULT(Connect());
      ASSERT_OK(write_conn.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE"));
      auto write_status = write_conn.ExecuteFormat("INSERT INTO t (key) VALUES ($0)", i);

      std::thread read_commit_thread([&read_conn, &read_status] {
        if (read_status.ok()) {
          read_status = read_conn.Execute("COMMIT");
        }
      });

      std::thread write_commit_thread([&write_conn, &write_status] {
        if (write_status.ok()) {
          write_status = write_conn.Execute("COMMIT");
        }
      });

      read_commit_thread.join();
      write_commit_thread.join();

      LOG(INFO) << "Read: " << read_status << ", write: " << write_status;

      if (!read_status.ok()) {
        ASSERT_OK(write_status);
        ++writes_won;
      } else {
        ASSERT_NOK(write_status);
        ++reads_won;
      }
    }

    LOG(INFO) << "Reads won: " << reads_won << ", writes won: " << writes_won
              << " (" << tries << "/" << kNumTries << ")";
    // always pass for TSAN, we're just looking for memory issues
    if (RegularBuildVsSanitizers(false, true)) {
      break;
    }
    // break (succeed) if we hit 25% on our "coin toss" transaction conflict above
    if (reads_won >= kKeys / 4 && writes_won >= kKeys / 4) {
      break;
    }
    // otherwise, retry and see if this is consistent behavior
  }
  ASSERT_LE(tries, kNumTries);
}

TEST_F(PgLibPqTest, ReadRestart) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));

  std::atomic<bool> stop(false);
  std::atomic<int> last_written(0);

  std::thread write_thread([this, &stop, &last_written] {
    auto write_conn = ASSERT_RESULT(Connect());
    int write_key = 1;
    while (!stop.load(std::memory_order_acquire)) {
      SCOPED_TRACE(Format("Writing: $0", write_key));

      ASSERT_OK(write_conn.Execute("BEGIN"));
      auto status = write_conn.ExecuteFormat("INSERT INTO t (key) VALUES ($0)", write_key);
      if (status.ok()) {
        status = write_conn.Execute("COMMIT");
      }
      if (status.ok()) {
        last_written.store(write_key, std::memory_order_release);
        ++write_key;
      } else {
        LOG(INFO) << "Write " << write_key << " failed: " << status;
      }
    }
  });

  auto se = ScopeExit([&stop, &write_thread] {
    stop.store(true, std::memory_order_release);
    write_thread.join();
  });

  auto deadline = CoarseMonoClock::now() + 30s;

  while (CoarseMonoClock::now() < deadline) {
    int read_key = last_written.load(std::memory_order_acquire);
    if (read_key == 0) {
      std::this_thread::sleep_for(100ms);
      continue;
    }

    SCOPED_TRACE(Format("Reading: $0", read_key));

    ASSERT_OK(conn.Execute("BEGIN"));
    auto key = ASSERT_RESULT(conn.FetchRow<int32_t>(Format(
        "SELECT * FROM t WHERE key = $0", read_key)));
    ASSERT_EQ(key, read_key);
    ASSERT_OK(conn.Execute("ROLLBACK"));
  }

  ASSERT_GE(last_written.load(std::memory_order_acquire), 100);
}

// Concurrently insert records into tables with foreign key relationship while truncating both.
TEST_F(PgLibPqTest, ConcurrentInsertTruncateForeignKey) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS t2"));
  ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS t1"));
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (k int primary key, v int)"));
  ASSERT_OK(conn.Execute(
        "CREATE TABLE t2 (k int primary key, t1_k int, FOREIGN KEY (t1_k) REFERENCES t1 (k))"));

  const int kMaxKeys = 1 << 20;

  constexpr auto kWriteThreads = 4;
  constexpr auto kTruncateThreads = 2;

  TestThreadHolder thread_holder;
  for (int i = 0; i != kWriteThreads; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
      auto write_conn = ASSERT_RESULT(Connect());
      while (!stop.load(std::memory_order_acquire)) {
        int t1_k = RandomUniformInt(0, kMaxKeys - 1);
        int t1_v = RandomUniformInt(0, kMaxKeys - 1);
        auto status = write_conn.ExecuteFormat("INSERT INTO t1 VALUES ($0, $1)", t1_k, t1_v);
        int t2_k = RandomUniformInt(0, kMaxKeys - 1);
        status = write_conn.ExecuteFormat("INSERT INTO t2 VALUES ($0, $1)", t2_k, t1_k);
      }
    });
  }

  for (int i = 0; i != kTruncateThreads; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
      // TODO (#19975): Enable read committed isolation
      auto truncate_conn = ASSERT_RESULT(
          SetDefaultTransactionIsolation(Connect(), IsolationLevel::SNAPSHOT_ISOLATION));
      int idx __attribute__((unused)) = 0;
      while (!stop.load(std::memory_order_acquire)) {
        auto status = truncate_conn.Execute("TRUNCATE TABLE t1, t2 CASCADE");
        ++idx;
        std::this_thread::sleep_for(100ms);
      }
    });
  }

  thread_holder.WaitAndStop(30s);
}

// Concurrently insert records to table with index.
TEST_F(PgLibPqTest, ConcurrentIndexInsert) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE IF NOT EXISTS users(id text, ename text, age int, PRIMARY KEY(id))"));

  ASSERT_OK(conn.Execute(
      "CREATE INDEX IF NOT EXISTS name_idx ON users(ename)"));

  constexpr auto kWriteThreads = 4;

  std::atomic<bool> stop(false);
  std::vector<std::thread> write_threads;

  while (write_threads.size() != kWriteThreads) {
    write_threads.emplace_back([this, &stop] {
      auto write_conn = ASSERT_RESULT(Connect());
      auto this_thread_id = std::this_thread::get_id();
      auto tid = std::hash<decltype(this_thread_id)>()(this_thread_id);
      int idx = 0;
      while (!stop.load(std::memory_order_acquire)) {
        ASSERT_OK(write_conn.ExecuteFormat(
            "INSERT INTO users (id, ename, age) VALUES ('user-$0-$1', 'name-$1', $2)",
            tid, idx, 20 + (idx % 50)));
        ++idx;
      }
    });
  }

  auto se = ScopeExit([&stop, &write_threads] {
    stop.store(true, std::memory_order_release);
    for (auto& thread : write_threads) {
      thread.join();
    }
  });

  std::this_thread::sleep_for(30s);
}

// Concurrently insert records followed by deletes to tables with a foreign key relationship with
// on-delete cascade. https://github.com/yugabyte/yugabyte-db/issues/14471
TEST_F(PgLibPqTest, ConcurrentInsertAndDeleteOnTablesWithForeignKey) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  const auto num_iterations = 50;
  const auto kTimeout = 60s;

  ASSERT_OK(conn1.Execute("CREATE TABLE IF NOT EXISTS t1 (a int PRIMARY KEY, b int)"));
  ASSERT_OK(conn1.Execute(
      "CREATE TABLE IF NOT EXISTS t2 (i int, j int REFERENCES t1(a) ON DELETE CASCADE)"));

  for (int i = 0; i < num_iterations; ++i) {
    // Insert 50 rows in t1.
    for (int count = 0; count < 50; count++) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO t1 VALUES ($0, $1)", count, count + 1));
    }

    std::atomic<bool> stop = false;
    std::atomic<int> values_in_t1 = 50;
    std::atomic<int> values_in_t2 = 0;

    // Insert rows in t2 on a separate thread.
    std::thread insertion_thread([&conn2, &stop, &values_in_t1, &values_in_t2] {
      while (!stop && values_in_t2 < values_in_t1 + 1) {
        ASSERT_OK(conn2.ExecuteFormat(
            "INSERT INTO t2 VALUES ($0, $1)", values_in_t2, values_in_t2 + 1));
        values_in_t2++;
      }

      // Verify insert prevention due to FK constraints.
      Status s = conn2.Execute("INSERT INTO t2 VALUES (999, 999)");
      ASSERT_FALSE(s.ok());
      ASSERT_EQ(PgsqlError(s), YBPgErrorCode::YB_PG_FOREIGN_KEY_VIOLATION);
      ASSERT_STR_CONTAINS(s.ToString(), "violates foreign key constraint");
    });

    // Insert 50 more values in t1.
    for (int j = 50; j < 100; ++j) {
      ASSERT_OK(conn1.ExecuteFormat("INSERT INTO t1 values ($0, $1)", j, j + 1));
      values_in_t1++;
    }

    // Wait till (9, 10) is inserted in t2 before we delete the row (10, 11) from t1.
    ASSERT_OK(WaitFor([&values_in_t2] { return values_in_t2 >= 10; }, kTimeout,
        Format("Wait till t2 has Row(9, 10)")));

    // Verify for CASCADE behaviour.
    ASSERT_OK(conn1.Execute("DELETE FROM t1 where a = 10"));
    ASSERT_EQ(ASSERT_RESULT(conn1.FetchRow<int64_t>("SELECT COUNT(*) FROM t2 WHERE j = 10")), 0);

    stop = true;
    insertion_thread.join();

    // Verify t1 has 99 i.e. (100 - 1) rows.
    auto curr_rows = ASSERT_RESULT(conn1.FetchRow<int64_t>("SELECT COUNT(*) FROM t1"));
    ASSERT_EQ(curr_rows, 99);

    // Reset the tables for next iteration.
    ASSERT_OK(conn1.Execute("TRUNCATE TABLE t1 CASCADE"));
    curr_rows = ASSERT_RESULT(conn1.FetchRow<int64_t>("SELECT COUNT(*) FROM t2"));
    ASSERT_EQ(curr_rows, 0);
  }
}

Result<int64_t> ReadSumBalance(
    PGConn* conn, int accounts, IsolationLevel isolation,
    std::atomic<int>* counter) {
  RETURN_NOT_OK(conn->StartTransaction(isolation));
  bool failed = true;
  auto se = ScopeExit([conn, &failed] {
    if (failed) {
      EXPECT_OK(conn->Execute("ROLLBACK"));
    }
  });

  std::string query = "";
  for (int i = 1; i <= accounts; ++i) {
    if (!query.empty()) {
      query += " UNION ";
    }
    query += Format("SELECT balance, id FROM account_$0 WHERE id = $0", i);
  }

  auto res = VERIFY_RESULT(conn->FetchMatrix(query, accounts, 2));
  int64_t sum = 0;
  for (int i = 0; i != accounts; ++i) {
    sum += VERIFY_RESULT(GetValue<int64_t>(res.get(), i, 0));
  }

  failed = false;
  RETURN_NOT_OK(conn->Execute("COMMIT"));
  return sum;
}

void PgLibPqTest::TestMultiBankAccount(IsolationLevel isolation, const bool colocation) {
  constexpr int kAccounts = RegularBuildVsSanitizers(20, 10);
  constexpr int64_t kInitialBalance = 100;
  const std::string db_name = "testdb";

#ifndef NDEBUG
  const auto kTimeout = 180s;
  constexpr int kThreads = RegularBuildVsSanitizers(12, 5);
#else
  const auto kTimeout = 60s;
  constexpr int kThreads = 5;
#endif

  PGConn conn = ASSERT_RESULT(Connect());

  if (colocation) {
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", db_name));
  } else {
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", db_name));
  }

  conn = ASSERT_RESULT(ConnectToDB(db_name));

  std::vector<PGConn> thread_connections;
  for (int i = 0; i < kThreads; ++i) {
    thread_connections.push_back(ASSERT_RESULT(ConnectToDB(db_name)));
  }

  for (int i = 1; i <= kAccounts; ++i) {
    ASSERT_OK(
        conn.ExecuteFormat("CREATE TABLE account_$0 (id int, balance bigint, PRIMARY KEY(id))", i));

    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO account_$0 (id, balance) VALUES ($0, $1)", i, kInitialBalance));
  }

  std::atomic<int> writes(0);
  std::atomic<int> reads(0);

  constexpr auto kRequiredReads = RegularBuildVsSanitizers(5, 2);
  constexpr auto kRequiredWrites = RegularBuildVsSanitizers(1000, 500);

  std::atomic<int> counter(100000);
  TestThreadHolder thread_holder;
  for (int i = 1; i <= kThreads; ++i) {
    thread_holder.AddThreadFunctor(
        [&conn = thread_connections[i - 1], &writes, &isolation,
         &stop_flag = thread_holder.stop_flag()]() {
      while (!stop_flag.load(std::memory_order_acquire)) {
        int from = RandomUniformInt(1, kAccounts);
        int to = RandomUniformInt(1, kAccounts - 1);
        if (to >= from) {
          ++to;
        }
        int64_t amount = RandomUniformInt(1, 10);
        ASSERT_OK(conn.StartTransaction(isolation));
        auto status = conn.ExecuteFormat(
              "UPDATE account_$0 SET balance = balance - $1 WHERE id = $0", from, amount);
        if (status.ok()) {
          status = conn.ExecuteFormat(
              "UPDATE account_$0 SET balance = balance + $1 WHERE id = $0", to, amount);
        }
        if (status.ok()) {
          status = conn.Execute("COMMIT;");
        } else {
          ASSERT_OK(conn.Execute("ROLLBACK;"));
        }
        if (!status.ok()) {
          ASSERT_TRUE(TransactionalFailure(status)) << status;
        } else {
          LOG(INFO) << "Updated: " << from << " => " << to << " by " << amount;
          ++writes;
        }
      }
    });
  }

  thread_holder.AddThreadFunctor([this, &counter, &reads, &writes, isolation,
                                  &stop_flag = thread_holder.stop_flag(), &db_name]() {
    SetFlagOnExit set_flag_on_exit(&stop_flag);
    auto connection = ASSERT_RESULT(ConnectToDB(db_name));
    auto failures_in_row = 0;
    while (!stop_flag.load(std::memory_order_acquire)) {
      if (isolation == IsolationLevel::SERIALIZABLE_ISOLATION) {
        auto lower_bound = reads.load() * kRequiredWrites < writes.load() * kRequiredReads
            ? 1.0 - 1.0 / (1ULL << failures_in_row) : 0.0;
        ASSERT_OK(connection.ExecuteFormat(
            "SET yb_transaction_priority_lower_bound = $0", lower_bound));
      }
      auto sum = ReadSumBalance(&connection, kAccounts, isolation, &counter);
      if (!sum.ok()) {
        // Do not overflow long when doing bitshift above.
        failures_in_row = std::min(failures_in_row + 1, 63);
        ASSERT_TRUE(TransactionalFailure(sum.status())) << sum.status();
      } else {
        failures_in_row = 0;
        ASSERT_EQ(*sum, kAccounts * kInitialBalance);
        ++reads;
      }
    }
  });

  auto wait_status = WaitFor([&reads, &writes, &stop = thread_holder.stop_flag()] {
    return stop.load() || (writes.load() >= kRequiredWrites && reads.load() >= kRequiredReads);
  }, kTimeout, Format("At least $0 reads and $1 writes", kRequiredReads, kRequiredWrites));

  LOG(INFO) << "Writes: " << writes.load() << ", reads: " << reads.load();

  ASSERT_OK(wait_status);

  thread_holder.Stop();

  ASSERT_OK(WaitFor([&conn, isolation, &counter]() -> Result<bool> {
    auto sum = ReadSumBalance(&conn, kAccounts, isolation, &counter);
    if (!sum.ok()) {
      if (!TransactionalFailure(sum.status())) {
        return sum.status();
      }
      return false;
    }
    EXPECT_EQ(*sum, kAccounts * kInitialBalance);
    return true;
  }, 10s, "Final read"));

  auto total_not_found = 0;
  for (auto* tserver : cluster_->tserver_daemons()) {
    auto tablets = ASSERT_RESULT(cluster_->GetTabletIds(tserver));
    for (const auto& tablet : tablets) {
      auto result = tserver->GetMetric<int64>(
          &METRIC_ENTITY_tablet, tablet.c_str(), &METRIC_transaction_not_found, "value");
      if (result.ok()) {
        total_not_found += *result;
      } else {
        ASSERT_TRUE(result.status().IsNotFound()) << result.status();
      }
    }
  }

  LOG(INFO) << "Total not found: " << total_not_found;
  // Check that total not found is not too big.
  ASSERT_LE(total_not_found, 200);
}

class PgLibPqFailoverDuringInitDb : public LibPqTestBase {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Use small clock skew, to decrease number of read restarts.
    options->allow_crashes_during_init_db = true;
    options->extra_master_flags.push_back("--TEST_fail_initdb_after_snapshot_restore=true");
  }

  int GetNumMasters() const override {
    return 3;
  }
};

TEST_F(PgLibPqFailoverDuringInitDb, CreateTable) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));

  auto res = ASSERT_RESULT(conn.Fetch("SELECT * FROM t"));
}

class PgLibPqSmallClockSkewFailOnConflictTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Use small clock skew, to decrease number of read restarts.
    options->extra_tserver_flags.push_back("--max_clock_skew_usec=5000");
    UpdateMiniClusterFailOnConflict(options);
  }
};

TEST_F_EX(PgLibPqTest, MultiBankAccountSnapshot, PgLibPqSmallClockSkewFailOnConflictTest) {
  TestMultiBankAccount(IsolationLevel::SNAPSHOT_ISOLATION);
}

TEST_F_EX(
    PgLibPqTest, MultiBankAccountSnapshotWithColocation, PgLibPqSmallClockSkewFailOnConflictTest) {
  TestMultiBankAccount(IsolationLevel::SNAPSHOT_ISOLATION, true /* colocation */);
}

TEST_F_EX(PgLibPqTest, MultiBankAccountSerializable, PgLibPqFailOnConflictTest) {
  TestMultiBankAccount(IsolationLevel::SERIALIZABLE_ISOLATION);
}

TEST_F_EX(PgLibPqTest, MultiBankAccountSerializableWithColocation, PgLibPqFailOnConflictTest) {
  TestMultiBankAccount(IsolationLevel::SERIALIZABLE_ISOLATION, true /* colocation */);
}

void PgLibPqTest::DoIncrement(
    int key, int num_increments, IsolationLevel isolation, bool lock_first) {
  auto conn = ASSERT_RESULT(Connect());

  // Perform increments
  int succeeded_incs = 0;
  while (succeeded_incs < num_increments) {
    ASSERT_OK(conn.StartTransaction(isolation));
    bool committed = false;
    if (lock_first) {
      ASSERT_OK(conn.FetchFormat("SELECT * FROM t WHERE key = $0 FOR UPDATE", key));
    }
    auto exec_status = conn.ExecuteFormat("UPDATE t SET value = value + 1 WHERE key = $0", key);
    if (exec_status.ok()) {
      auto commit_status = conn.Execute("COMMIT");
      if (commit_status.ok()) {
        succeeded_incs++;
        committed = true;
      }
    }
    if (!committed) {
      ASSERT_OK(conn.Execute("ROLLBACK"));
    }
  }
}

void PgLibPqTest::TestParallelCounter(IsolationLevel isolation) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value INT)"));

  const auto kThreads = RegularBuildVsSanitizers(3, 2);
  const auto kIncrements = RegularBuildVsSanitizers(100, 20);

  // Make a counter for each thread and have each thread increment it
  std::vector<std::thread> threads;
  while (threads.size() != kThreads) {
    int key = narrow_cast<int>(threads.size());
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, value) VALUES ($0, 0)", key));

    threads.emplace_back([this, key, isolation] {
      DoIncrement(key, kIncrements, isolation);
    });
  }

  // Wait for completion
  for (auto& thread : threads) {
    thread.join();
  }

  // Check each counter
  for (int i = 0; i < kThreads; i++) {
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>(Format("SELECT value FROM t WHERE key = $0",
                                                            i))),
              kIncrements);
  }
}

TEST_F(PgLibPqTest, TestParallelCounterSerializable) {
  TestParallelCounter(IsolationLevel::SERIALIZABLE_ISOLATION);
}

TEST_F(PgLibPqTest, TestParallelCounterRepeatableRead) {
  TestParallelCounter(IsolationLevel::SNAPSHOT_ISOLATION);
}

void PgLibPqTest::TestConcurrentCounter(IsolationLevel isolation, bool lock_first) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value INT)"));

  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (0, 0)"));

  const auto kThreads = RegularBuildVsSanitizers(3, 2);
  const auto kIncrements = RegularBuildVsSanitizers(100, 20);

  // Have each thread increment the same already-created counter
  std::vector<std::thread> threads;
  while (threads.size() != kThreads) {
    threads.emplace_back([this, isolation, lock_first] {
      DoIncrement(0, kIncrements, isolation, lock_first);
    });
  }

  // Wait for completion
  for (auto& thread : threads) {
    thread.join();
  }

  // Check that we incremented exactly the desired number of times
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT value FROM t WHERE key = 0")),
            kThreads * kIncrements);
}

TEST_F_EX(PgLibPqTest, TestConcurrentCounterSerializable, PgLibPqFailOnConflictTest) {
  // Each of the three threads perform the following:
  // BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
  // UPDATE t SET value = value + 1 WHERE key = 0;
  // COMMIT;
  // The UPDATE first does a read and acquires a kStrongRead lock on the key=0 row. So each thread
  // is able to acquire this lock concurrently. The UPDATE then does a write to the same row and
  // gets blocked. With fail-on-conflict behavior, one of the threads will win at the second RPC and
  // progress can be made. But with wait-on-conflict behavior, progress cannot be made since
  // deadlock is repeatedly encountered by all threads most of the time.
  //
  // In order to test a similar scenario with wait-on-conflict behavior,
  // TestLockedConcurrentCounterSerializable adds a SELECT...FOR UPDATE before the UPDATE, so only
  // one thread can block the row and deadlocks are not encountered.
  TestConcurrentCounter(IsolationLevel::SERIALIZABLE_ISOLATION);
}

TEST_F(PgLibPqTest, TestLockedConcurrentCounterSerializable) {
  // See comment in TestConcurrentCounterSerializable.
  TestConcurrentCounter(IsolationLevel::SERIALIZABLE_ISOLATION, /* lock_first = */ true);
}

TEST_F(PgLibPqTest, TestConcurrentCounterRepeatableRead) {
  TestConcurrentCounter(IsolationLevel::SNAPSHOT_ISOLATION);
}

TEST_F(PgLibPqTest, TestConcurrentCounterReadCommitted) {
  TestConcurrentCounter(IsolationLevel::READ_COMMITTED);
}

TEST_F(PgLibPqTest, SecondaryIndexInsertSelect) {
  constexpr int kThreads = 4;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (a INT PRIMARY KEY, b INT)"));
  ASSERT_OK(conn.Execute("CREATE INDEX ON t (b, a)"));

  TestThreadHolder holder;
  std::array<std::atomic<int>, kThreads> written;
  for (auto& w : written) {
    w.store(0, std::memory_order_release);
  }

  for (int i = 0; i != kThreads; ++i) {
    holder.AddThread([this, i, &stop = holder.stop_flag(), &written] {
      auto connection = ASSERT_RESULT(Connect());
      int key = 0;

      while (!stop.load(std::memory_order_acquire)) {
        if (RandomUniformBool()) {
          int a = i * 1000000 + key;
          int b = key;
          ASSERT_OK(connection.ExecuteFormat("INSERT INTO t (a, b) VALUES ($0, $1)", a, b));
          written[i].store(++key, std::memory_order_release);
        } else {
          int writer_index = RandomUniformInt(0, kThreads - 1);
          int num_written = written[writer_index].load(std::memory_order_acquire);
          if (num_written == 0) {
            continue;
          }
          int read_key = num_written - 1;
          int b = read_key;
          int read_a = ASSERT_RESULT(connection.FetchRow<int32_t>(
              Format("SELECT a FROM t WHERE b = $0 LIMIT 1", b)));
          ASSERT_EQ(read_a % 1000000, read_key);
        }
      }
    });
  }

  holder.WaitAndStop(60s);
}

void AssertRows(PGConn *conn, int expected_num_rows) {
  auto res = ASSERT_RESULT(conn->Fetch("SELECT * FROM test"));
  ASSERT_EQ(PQntuples(res.get()), expected_num_rows);
}

TEST_F(PgLibPqTest, InTxnDelete) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (pk int PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1)"));
  ASSERT_NO_FATALS(AssertRows(&conn, 1));
  ASSERT_OK(conn.Execute("DELETE FROM test"));
  ASSERT_NO_FATALS(AssertRows(&conn, 0));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1)"));
  ASSERT_NO_FATALS(AssertRows(&conn, 1));
  ASSERT_OK(conn.Execute("COMMIT"));

  ASSERT_NO_FATALS(AssertRows(&conn, 1));
}

class PgLibPqReadFromSysCatalogTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgLibPqTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        "--TEST_get_ysql_catalog_version_from_sys_catalog=true");
  }
};

TEST_F_EX(PgLibPqTest, StaleMasterReads, PgLibPqReadFromSysCatalogTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  uint64_t ver_orig;
  ASSERT_OK(client->GetYsqlCatalogMasterVersion(&ver_orig));
  for (int i = 1; i <= FLAGS_num_iter; i++) {
    LOG(INFO) << "ITERATION " << i;
    BumpCatalogVersion(1, &conn);
    LOG(INFO) << "Fetching CatalogVersion. Expecting " << i + ver_orig;
    uint64_t ver;
    ASSERT_OK(client->GetYsqlCatalogMasterVersion(&ver));
    ASSERT_EQ(ver_orig + i, ver);
  }
}

TEST_F(PgLibPqTest, CompoundKeyColumnOrder) {
  const string namespace_name = "yugabyte";
  const string table_name = "test";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (r2 int, r1 int, h int, v2 int, v1 int, primary key (h, r1, r2))",
      table_name));
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  std::string table_id =
      ASSERT_RESULT(GetTableIdByTableName(client.get(), namespace_name, table_name));
  std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
  {
    Synchronizer sync;
    ASSERT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
    ASSERT_OK(sync.Wait());
  }

  const auto& columns = table_info->schema.columns();
  std::array<string, 5> expected_column_names{"h", "r1", "r2", "v2", "v1"};
  ASSERT_EQ(expected_column_names.size(), columns.size());
  for (size_t i = 0; i < expected_column_names.size(); ++i) {
    ASSERT_EQ(columns[i].name(), expected_column_names[i]);
  }
}

TEST_F(PgLibPqTest, BulkCopy) {
  const std::string kTableName = "customer";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE CUSTOMER ( CUSTKEY     INTEGER NOT NULL PRIMARY KEY,\n"
      "                        NAME        VARCHAR(25) NOT NULL,\n"
      "                        ADDRESS     VARCHAR(40) NOT NULL,\n"
      "                        NATIONKEY   INTEGER NOT NULL,\n"
      "                        PHONE       CHAR(15) NOT NULL,\n"
      "                        MKTSEGMENT  CHAR(10) NOT NULL,\n"
      "                        COMMENT     VARCHAR(117) NOT NULL);",
      kTableName));

  constexpr int kNumBatches = 10;
  constexpr int kBatchSize = 1000;

  int customer_key = 0;
  for (int i = 0; i != kNumBatches; ++i) {
    ASSERT_OK(conn.CopyBegin(Format("COPY $0 FROM STDIN WITH BINARY", kTableName)));
    for (int j = 0; j != kBatchSize; ++j) {
      conn.CopyStartRow(7);
      conn.CopyPutInt32(++customer_key);
      conn.CopyPutString(Format("Name $0 $1", i, j));
      conn.CopyPutString(Format("Address $0 $1", i, j));
      conn.CopyPutInt32(i);
      conn.CopyPutString(std::to_string(999999876543210 + customer_key));
      conn.CopyPutString(std::to_string(9876543210 + customer_key));
      conn.CopyPutString(Format("Comment $0 $1", i, j));
    }

    ASSERT_OK(conn.CopyEnd());
  }

  LOG(INFO) << "Finished copy";
  for (;;) {
    auto result = conn.FetchRow<PGUint64>(Format("SELECT COUNT(*) FROM $0", kTableName));
    if (result.ok()) {
      auto count = *result;
      LOG(INFO) << "Total count: " << count;
      ASSERT_EQ(count, kNumBatches * kBatchSize);
      break;
    } else {
      auto message = result.status().ToString();
      ASSERT_TRUE(message.find("Snaphost too old") != std::string::npos) << result.status();
    }
  }
}

TEST_F(PgLibPqTest, CatalogManagerMapsTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test_db"));
  {
    auto test_conn = ASSERT_RESULT(ConnectToDB("test_db"));
    ASSERT_OK(test_conn.Execute("CREATE TABLE foo (a int PRIMARY KEY)"));
    ASSERT_OK(test_conn.Execute("ALTER TABLE foo RENAME TO bar"));
    ASSERT_OK(test_conn.Execute("ALTER TABLE bar RENAME COLUMN a to b"));
  }
  ASSERT_OK(conn.Execute("ALTER DATABASE test_db RENAME TO test_db_renamed"));

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  Result<bool> result(false);
  result = client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, "test_db_renamed", "bar"));
  ASSERT_OK(result);
  ASSERT_TRUE(result.get());
  result = client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, "test_db_renamed", "foo"));
  ASSERT_OK(result);
  ASSERT_FALSE(result.get());
  result = client->NamespaceExists("test_db_renamed", YQL_DATABASE_PGSQL);
  ASSERT_OK(result);
  ASSERT_TRUE(result.get());
  result = client->NamespaceExists("test_db", YQL_DATABASE_PGSQL);
  ASSERT_OK(result);
  ASSERT_FALSE(result.get());

  std::string table_id =
      ASSERT_RESULT(GetTableIdByTableName(client.get(), "test_db_renamed", "bar"));
  std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
  {
    Synchronizer sync;
    ASSERT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
    ASSERT_OK(sync.Wait());
  }
  ASSERT_EQ(table_info->schema.num_columns(), 1);
  ASSERT_EQ(table_info->schema.Column(0).name(), "b");
}

TEST_F(PgLibPqTest, TestSystemTableRollback) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE pktable (ptest1 int PRIMARY KEY);"));
  Status s = conn1.Execute("CREATE TABLE fktable (ftest1 inet REFERENCES pktable);");
  LOG(INFO) << "Status of second table creation: " << s;
  auto res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM pg_class WHERE relname='fktable'"));
  ASSERT_EQ(0, PQntuples(res.get()));
}

namespace {

Result<master::TabletLocationsPB> GetLegacyColocatedDBTabletLocations(
    client::YBClient* client,
    std::string database_name,
    PGConn* dummy,
    MonoDelta timeout) {
  const string ns_id =
      VERIFY_RESULT(GetNamespaceIdByNamespaceName(client, database_name));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;

  // Get TabletLocations for the colocated tablet.
  RETURN_NOT_OK(WaitFor(
      [&]() -> Result<bool> {
        Status s = client->GetTabletsFromTableId(
            GetColocatedDbParentTableId(ns_id),
            0 /* max_tablets */,
            &tablets);
        if (s.ok()) {
          return tablets.size() == 1;
        } else if (s.IsNotFound()) {
          return false;
        } else {
          return s;
        }
      },
      timeout,
      "wait for colocated parent tablet"));

  return tablets[0];
}

struct TableGroupInfo {
  Oid oid;
  std::string id;
  TabletId tablet_id;
  std::shared_ptr<client::YBTable> table;
};

Result<TableId> GetColocationOrTablegroupParentTableId(
    client::YBClient* client,
    const std::string& database_name,
    const std::string& tablegroup_id) {
  master::GetNamespaceInfoResponsePB resp;
  Status s = client->GetNamespaceInfo("", database_name, YQL_DATABASE_PGSQL, &resp);
  if (!s.ok()) {
    return s;
  }

  TableId parent_table_id;
  if (resp.colocated())
    parent_table_id = GetColocationParentTableId(tablegroup_id);
  else
    parent_table_id = GetTablegroupParentTableId(tablegroup_id);

  return parent_table_id;
}

Result<master::TabletLocationsPB> GetTablegroupTabletLocations(
    client::YBClient* client,
    std::string database_name,
    std::string tablegroup_id,
    MonoDelta timeout) {
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;

  bool exists = VERIFY_RESULT(client->TablegroupExists(database_name, tablegroup_id));
  if (!exists) {
    return STATUS(NotFound, "tablegroup does not exist");
  }

  TableId parent_table_id = VERIFY_RESULT(GetColocationOrTablegroupParentTableId(client,
                                                                                 database_name,
                                                                                 tablegroup_id));
  // Get TabletLocations for the tablegroup tablet.
  RETURN_NOT_OK(WaitFor(
      [&]() -> Result<bool> {
        Status s = client->GetTabletsFromTableId(
            parent_table_id,
            0 /* max_tablets */,
            &tablets);
        if (s.ok()) {
          return tablets.size() == 1;
        } else if (s.IsNotFound()) {
          return false;
        } else {
          return s;
        }
      },
      timeout,
      "wait for tablegroup parent tablet"));
  return tablets[0];
}

Result<TableGroupInfo> SelectTablegroup(
    client::YBClient* client, PGConn* conn, const std::string& database_name,
    const std::string& group_name) {
  TableGroupInfo group_info;
  const auto database_oid = VERIFY_RESULT(GetDatabaseOid(conn, database_name));
  group_info.oid = VERIFY_RESULT(conn->FetchRow<PGOid>(
      Format("SELECT oid FROM pg_yb_tablegroup WHERE grpname=\'$0\'", group_name)));

  group_info.id = GetPgsqlTablegroupId(database_oid, group_info.oid);
  group_info.tablet_id = VERIFY_RESULT(GetTablegroupTabletLocations(
      client,
      database_name,
      group_info.id,
      30s))
    .tablet_id();
  TableId parent_table_id = VERIFY_RESULT(GetColocationOrTablegroupParentTableId(client,
                                                                                 database_name,
                                                                                 group_info.id));
  group_info.table = VERIFY_RESULT(client->OpenTable(parent_table_id));
  SCHECK(VERIFY_RESULT(client->TablegroupExists(database_name, group_info.id)),
         InternalError,
         "YBClient::TablegroupExists couldn't find a tablegroup!");
  return group_info;
}

Result<master::TabletLocationsPB>
    GetColocatedDbDefaultTablegroupTabletLocations(
    client::YBClient* client,
    std::string database_name,
    PGConn* conn,
    MonoDelta timeout) {
  // For a colocated database, its default underlying tablegroup is
  // called "default".
  const auto tablegroup =
      VERIFY_RESULT(SelectTablegroup(client, conn, database_name, "default"));

  // Get TabletLocations for the default tablegroup tablet.
  return GetTablegroupTabletLocations(client, database_name, tablegroup.id,
                                      timeout);
}

} // namespace

void PgLibPqTest::CreateDatabaseWithTablegroup(
    const string database_name, const string tablegroup_name, yb::pgwrapper::PGConn* conn) {
  ASSERT_OK(conn->ExecuteFormat("CREATE DATABASE $0", database_name));
  *conn = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn->ExecuteFormat("CREATE TABLEGROUP $0", tablegroup_name));
}

void PgLibPqTest::TestTableColocation(GetParentTableTabletLocation getParentTableTabletLocation) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_bar_index;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocation = true", kDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  // Create a range partition table, the table should share the tablet with the parent table.
  ASSERT_OK(conn.Execute("CREATE TABLE foo (a INT, PRIMARY KEY (a ASC))"));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  // A parent table of default tablegroup with one tablet should be created when the first
  // colocated table is created in a colocated database.
  const auto colocated_tablet_locations =
      ASSERT_RESULT(getParentTableTabletLocation(client.get(), kDatabaseName,
                                                                   &conn, 30s));
  const auto colocated_tablet_id = colocated_tablet_locations.tablet_id();
  const auto colocated_table = ASSERT_RESULT(client->OpenTable(
      colocated_tablet_locations.table_id()));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Create a colocated index table.
  ASSERT_OK(conn.Execute("CREATE INDEX foo_index1 ON foo (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo_index1"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Create a hash partition table and opt out of using the parent tablet.
  ASSERT_OK(conn.Execute("CREATE TABLE bar (a INT) WITH (colocation = false)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "bar"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  for (auto& tablet : tablets) {
    ASSERT_NE(tablet.tablet_id(), colocated_tablet_id);
  }

  // Create an index on the non-colocated table. The index should follow the table and opt out of
  // colocation.
  ASSERT_OK(conn.Execute("CREATE INDEX bar_index ON bar (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "bar_index"));
  const auto table_bar_index = ASSERT_RESULT(client->OpenTable(table_id));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  for (auto& tablet : tablets) {
    ASSERT_NE(tablet.tablet_id(), colocated_tablet_id);
  }
  tablets_bar_index.Swap(&tablets);

  // Create a range partition table without specifying primary key.
  ASSERT_OK(conn.Execute("CREATE TABLE baz (a INT)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "baz"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Create another table and index.
  ASSERT_OK(conn.Execute("CREATE TABLE qux (a INT, PRIMARY KEY (a ASC)) WITH (colocation = true)"));
  ASSERT_OK(conn.Execute("CREATE INDEX qux_index ON qux (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "qux_index"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));

  // Drop a table in the parent tablet.
  ASSERT_OK(conn.Execute("DROP TABLE qux"));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "qux"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "qux_index"))));

  // Drop a table that is opted out.
  ASSERT_OK(conn.Execute("DROP TABLE bar"));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "bar"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "bar_index"))));

  // The tablets for bar_index should be deleted.
  std::vector<bool> tablet_founds(tablets_bar_index.size(), true);
  std::vector<promise_future_pair> tablet_promises_futures(tablets_bar_index.size());
  ASSERT_OK(WaitFor(
      [&] {
        for (int i = 0; i < tablets_bar_index.size(); ++i) {
          tablet_promises_futures[i].second = tablet_promises_futures[i].first.get_future();
          client->LookupTabletById(
              tablets_bar_index[i].tablet_id(),
              table_bar_index,
              master::IncludeInactive::kFalse,
              master::IncludeDeleted::kFalse,
              CoarseMonoClock::Now() + 30s,
              [&, i](const Result<client::internal::RemoteTabletPtr>& result) {
                tablet_promises_futures[i].first.set_value(result);
              },
              client::UseCache::kFalse);
        }
        for (int i = 0; i < tablets_bar_index.size(); ++i) {
          tablet_founds[i] = tablet_promises_futures[i].second.get().ok();
        }
        return std::all_of(
            tablet_founds.cbegin(),
            tablet_founds.cend(),
            [](bool tablet_found) {
              return !tablet_found;
            });
      },
      30s, "Drop table opted out of colocation"));

  // Drop the database.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDatabaseName));
  ASSERT_FALSE(ASSERT_RESULT(client->NamespaceExists(kDatabaseName, YQL_DATABASE_PGSQL)));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "foo"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "foo_index1"))));

  // The colocation tablet should be deleted.
  bool tablet_found = true;
  promise<Result<client::internal::RemoteTabletPtr>> tablet_promise;
  auto tablet_future = tablet_promise.get_future();
  ASSERT_OK(WaitFor(
      [&] {
        client->LookupTabletById(
            colocated_tablet_id,
            colocated_table,
            master::IncludeInactive::kFalse,
            master::IncludeDeleted::kFalse,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              tablet_promise.set_value(result);
            },
            client::UseCache::kFalse);
        tablet_found = tablet_future.get().ok();
        return !tablet_found;
      },
      30s, "Drop colocated database"));
}

TEST_F(PgLibPqTest, TableColocation) {
  TestTableColocation(GetColocatedDbDefaultTablegroupTabletLocations);
}

class PgLibPqTableColocationEnabledByDefaultTest : public PgLibPqTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgLibPqTest::UpdateMiniClusterOptions(options);
    // Enable colocation by default on the cluster.
    options->extra_tserver_flags.push_back("--ysql_colocate_database_by_default=true");
  }
};

void PgLibPqTest::TestTableColocationEnabledByDefault(
    GetParentTableTabletLocation getParentTableTabletLocation) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseNameColocatedByDefault = "test_db_colocated_by_default";
  const string kDatabaseNameColocatedExplicitly = "test_db_colocated_explicitly";
  const string kDatabaseNameNotColocated = "test_db_not_colocated";
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> foo_tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> bar_tablets;

  auto conn = ASSERT_RESULT(Connect());

  // Database without specifying colocation value must be created with colocation = true.
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseNameColocatedByDefault));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseNameColocatedByDefault));
  // Create a range partition table, the table should share the tablet with the parent table.
  ASSERT_OK(conn.Execute("CREATE TABLE foo (a INT, PRIMARY KEY (a ASC))"));

  // A parent table with one tablet should be created.
  auto colocated_tablet_locations = ASSERT_RESULT(
      getParentTableTabletLocation(client.get(), kDatabaseNameColocatedByDefault, &conn, 30s));
  auto colocated_tablet_id = colocated_tablet_locations.tablet_id();
  auto colocated_table = ASSERT_RESULT(client->OpenTable(colocated_tablet_locations.table_id()));
  ASSERT_TRUE(colocated_table->colocated());

  auto table_id =
      ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseNameColocatedByDefault, "foo"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Create a colocated index table.
  ASSERT_OK(conn.Execute("CREATE INDEX foo_index ON foo (a)"));
  table_id = ASSERT_RESULT(
      GetTableIdByTableName(client.get(), kDatabaseNameColocatedByDefault, "foo_index"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // A table should be able to opt out of colocation.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE foo_non_colocated (a INT, PRIMARY KEY (a ASC)) WITH (colocation = false)"));
  table_id = ASSERT_RESULT(
      GetTableIdByTableName(client.get(), kDatabaseNameColocatedByDefault, "foo_non_colocated"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  for (auto& tablet : tablets) {
    ASSERT_NE(tablet.tablet_id(), colocated_tablet_id);
  }

  // Database which explicitly specifies colocation = true should work as expected.
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE DATABASE $0 WITH colocation = true", kDatabaseNameColocatedExplicitly));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseNameColocatedExplicitly));
  // Create a range partition table, the table should share the tablet with the parent table.
  ASSERT_OK(conn.Execute("CREATE TABLE foo (a INT, PRIMARY KEY (a ASC))"));

  // A parent table with one tablet should be created.
  colocated_tablet_locations = ASSERT_RESULT(
      getParentTableTabletLocation(client.get(), kDatabaseNameColocatedExplicitly, &conn, 30s));
  colocated_tablet_id = colocated_tablet_locations.tablet_id();
  colocated_table = ASSERT_RESULT(client->OpenTable(colocated_tablet_locations.table_id()));
  ASSERT_TRUE(colocated_table->colocated());

  table_id =
      ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseNameColocatedExplicitly,
      "foo"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Database which explicitly opts out of colocation must work as expected.
  ASSERT_OK(
      conn.ExecuteFormat("CREATE DATABASE $0 WITH colocation = false",
      kDatabaseNameNotColocated));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseNameNotColocated));

  // Create two tables which shouldn't share the same tablet.
  ASSERT_OK(conn.Execute("CREATE TABLE foo (a INT, PRIMARY KEY (a ASC))"));
  ASSERT_OK(conn.Execute("CREATE TABLE bar (b INT)"));
  auto table_id_foo =
      ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseNameNotColocated, "foo"));
  auto table_id_bar =
      ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseNameNotColocated, "bar"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id_foo, 0, &foo_tablets));
  ASSERT_OK(client->GetTabletsFromTableId(table_id_bar, 0, &bar_tablets));
  ASSERT_EQ(foo_tablets.size(), 1);
  for (auto& tablet : bar_tablets) {
    ASSERT_NE(tablet.tablet_id(), foo_tablets[0].tablet_id());
  }
}

TEST_F_EX(
    PgLibPqTest, TableColocationEnabledByDefault,
    PgLibPqTableColocationEnabledByDefaultTest) {
  TestTableColocationEnabledByDefault(GetColocatedDbDefaultTablegroupTabletLocations);
}

void PgLibPqTest::PerformSimultaneousTxnsAndVerifyConflicts(
    const string database_name, bool colocated, const string tablegroup_name,
    const string query_statement) {
  auto conn1 = ASSERT_RESULT(ConnectToDB(database_name));
  auto conn2 = ASSERT_RESULT(ConnectToDB(database_name));

  if (colocated) {
    ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE t (a INT, PRIMARY KEY (a ASC))"));
  } else {
    ASSERT_OK(conn1.ExecuteFormat(
        "CREATE TABLE t (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0", tablegroup_name));
  }

  ASSERT_OK(conn1.Execute("INSERT INTO t(a) VALUES(1)"));

  // From conn1, select the row in UPDATE row lock mode. From conn2, delete the row.
  // Ensure that conn1's transaction will detect a conflict at the time of commit.
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  auto res = ASSERT_RESULT(conn1.Fetch(query_statement));
  ASSERT_EQ(PQntuples(res.get()), 1);

  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto status = conn2.Execute("DELETE FROM t WHERE a = 1");
  ASSERT_TRUE(IsSerializeAccessError(status)) <<  status;
  ASSERT_STR_CONTAINS(status.ToString(), "conflicts with higher priority transaction");

  ASSERT_OK(conn1.CommitTransaction());
  ASSERT_OK(conn2.CommitTransaction());

  // Ensure that reads to separate tables in a colocated database/tablegroup do not conflict.
  if (colocated) {
    ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE t2 (a INT, PRIMARY KEY (a ASC))"));
  } else {
    ASSERT_OK(conn1.ExecuteFormat(
        "CREATE TABLE t2 (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0", tablegroup_name));
  }

  ASSERT_OK(conn1.Execute("INSERT INTO t2(a) VALUES(1)"));

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));

  ASSERT_OK(conn1.FetchRow<int32_t>("SELECT * FROM t FOR UPDATE"));
  ASSERT_OK(conn2.FetchRow<int32_t>("SELECT * FROM t2 FOR UPDATE"));

  ASSERT_OK(conn1.CommitTransaction());
  ASSERT_OK(conn2.CommitTransaction());
}

// Test for ensuring that transaction conflicts work as expected for colocated tables.
// Related to https://github.com/yugabyte/yugabyte-db/issues/3251.
TEST_F_EX(PgLibPqTest, TxnConflictsForColocatedTables, PgLibPqFailOnConflictTest) {
  auto conn = ASSERT_RESULT(Connect());
  const string database_name = "test_db";
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocation = true", database_name));
  PerformSimultaneousTxnsAndVerifyConflicts("test_db" /* database_name */, true /* colocated */);
}

// Test for ensuring that transaction conflicts work as expected for Tablegroups.
TEST_F_EX(PgLibPqTest, TxnConflictsForTablegroups, PgLibPqFailOnConflictTest) {
  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(
      "test_db" /* database_name */, "test_tgroup" /* tablegroup_name */, &conn);
  PerformSimultaneousTxnsAndVerifyConflicts(
      "test_db" /* database_name */,
      true, /* colocated */
      "test_tgroup" /* tablegroup_name */);
}

// Test for ensuring that transaction conflicts work as expected for Tablegroups, where the SELECT
// is done with pg_hint_plan to use YB Sequential Scan.
TEST_F_EX(PgLibPqTest, TxnConflictsForTablegroupsYbSeq, PgLibPqFailOnConflictTest) {
  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(
      "test_db" /* database_name */, "test_tgroup" /* tablegroup_name */, &conn);
  PerformSimultaneousTxnsAndVerifyConflicts(
      "test_db" /* database_name */, true, /* colocated */
      "test_tgroup" /* tablegroup_name */,
      "/*+ SeqScan(t) */ SELECT * FROM t FOR UPDATE" /* query_statement */);
}

// Test for ensuring that transaction conflicts work as expected for Tablegroups, where the SELECT
// is done with an ORDER BY clause.
TEST_F_EX(PgLibPqTest, TxnConflictsForTablegroupsOrdered, PgLibPqFailOnConflictTest) {
  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(
      "test_db" /* database_name */, "test_tgroup" /* tablegroup_name */, &conn);
  PerformSimultaneousTxnsAndVerifyConflicts(
      "test_db" /* database_name */, true, /* colocated */
      "test_tgroup" /* tablegroup_name */,
      "SELECT * FROM t ORDER BY a FOR UPDATE" /* query_statement */);
}

Result<PGConn> PgLibPqTest::RestartTSAndConnectToPostgres(
    int ts_idx, const std::string& db_name) {
  cluster_->tablet_server(ts_idx)->Shutdown();

  LOG(INFO) << "Restart tserver " << ts_idx;
  RETURN_NOT_OK(cluster_->tablet_server(ts_idx)->Restart());
  RETURN_NOT_OK(cluster_->WaitForTabletsRunning(cluster_->tablet_server(ts_idx),
      MonoDelta::FromSeconds(60 * kTimeMultiplier)));

  pg_ts = cluster_->tablet_server(ts_idx);
  return ConnectToDB(db_name);
}

void PgLibPqTest::FlushTablesAndCreateData(
    const string database_name,
    const int timeout_secs,
    bool colocated,
    const string tablegroup_name) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  PGConn conn_new = ASSERT_RESULT(ConnectToDB(database_name));
  if (colocated) {
    ASSERT_OK(conn_new.Execute("CREATE TABLE foo (i int)"));
  } else {
    ASSERT_OK(conn_new.ExecuteFormat("CREATE TABLE foo (i int) tablegroup $0", tablegroup_name));
  }
  ASSERT_OK(conn_new.Execute("INSERT INTO foo VALUES (10)"));

  // Flush tablets; requests from here on will be replayed from the WAL during bootstrap.
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), database_name, "foo"));
  ASSERT_OK(client->FlushTables(
      {table_id},
      false /* add_indexes */,
      timeout_secs,
      false /* is_compaction */));

  // ALTER requires foo's table id to be in the TS raft metadata
  ASSERT_OK(conn_new.Execute("ALTER TABLE foo ADD c char"));
  ASSERT_OK(conn_new.Execute("ALTER TABLE foo RENAME COLUMN c to d"));
  // but DROP will remove foo's table id from the TS raft metadata
  ASSERT_OK(conn_new.Execute("DROP TABLE foo"));
  ASSERT_OK(conn_new.Execute("CREATE TABLE bar (c char)"));
}

void PgLibPqTest::FlushTablesAndPerformBootstrap(
    const string database_name,
    const int timeout_secs,
    bool colocated,
    const bool test_backward_compatibility,
    const string tablegroup_name) {

  FlushTablesAndCreateData(database_name, timeout_secs, colocated, tablegroup_name);
  {
    // Restart a TS that serves this tablet so we do a local bootstrap and replay WAL files.
    // Ensure we don't crash here due to missing table info in metadata when replaying the ALTER.
    auto conn_after = ASSERT_RESULT(RestartTSAndConnectToPostgres(0, database_name));
    auto res = ASSERT_RESULT(conn_after.FetchRow<int64_t>("SELECT COUNT(*) FROM bar"));
    ASSERT_EQ(res, 0);
  }

  // Subsequent bootstraps should have the last_flushed_change_metadata_op_id set but
  // they should also not crash.
  if (test_backward_compatibility) {
    ASSERT_OK(cluster_->SetFlagOnTServers("TEST_invalidate_last_change_metadata_op", "false"));
    {
      auto conn_after = ASSERT_RESULT(RestartTSAndConnectToPostgres(0, database_name));
      auto res = ASSERT_RESULT(conn_after.FetchRow<int64_t>("SELECT COUNT(*) FROM bar"));
      ASSERT_EQ(res, 0);

      ASSERT_OK(conn_after.Execute("CREATE TABLE bar2 (c char)"));
      ASSERT_OK(conn_after.Execute("ALTER TABLE bar2 RENAME COLUMN c to d"));
    }
    auto conn_after = ASSERT_RESULT(RestartTSAndConnectToPostgres(0, database_name));

    auto res = ASSERT_RESULT(conn_after.FetchRow<int64_t>("SELECT COUNT(*) FROM bar"));
    ASSERT_EQ(res, 0);
    res = ASSERT_RESULT(conn_after.FetchRow<int64_t>("SELECT COUNT(*) FROM bar2"));
    ASSERT_EQ(res, 0);
  }
}

// Ensure tablet bootstrap doesn't crash when replaying change metadata operations
// for a deleted colocated table. This is a regression test for #6096.
TEST_F(PgLibPqTest, ReplayDeletedTableInColocatedDB) {
  const string database_name = "test_db";
  {
    PGConn conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", database_name));
  }
  FlushTablesAndPerformBootstrap(
      database_name /* database_name */, 30 /* timeout_secs */,
      true /* colocated */, false /* test_backward_compatibility */);
}

// Ensure tablet bootstrap doesn't crash when replaying change metadata operations
// for a deleted colocated table after an upgrade from older versions.
TEST_F(PgLibPqTest, ReplayDeletedTableInColocatedDBPostUpgrade) {
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_invalidate_last_change_metadata_op", "true"));
  const string database_name = "test_db";
  {
    PGConn conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", database_name));
  }
  FlushTablesAndPerformBootstrap(
      database_name /* database_name */, 30 /* timeout_secs */,
      true /* colocated */, true /* test_backward_compatibility */);
}

// Ensure tablet bootstrap doesn't crash when replaying change metadata operations
// for a deleted tabelgroup.
TEST_F(PgLibPqTest, ReplayDeletedTableInTablegroups) {
  PGConn conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(
      "test_db" /* database_name */, "test_tgroup" /* tablegroup_name */, &conn);
  FlushTablesAndPerformBootstrap(
      "test_db" /* database_name */,
      30 /* timeout_secs */,
      true /* colocated */,
      false /* test_backward_compatibility */,
      "test_tgroup" /* tablegroup_name */);
}

class PgLibPqDuplicateClientCreateTableTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--TEST_duplicate_create_table_request=true");
    options->extra_tserver_flags.push_back(Format("--yb_client_admin_operation_timeout_sec=$0",
                                                  30));
  }

  void SetUp() override {
    // Skip in TSAN as InitDB times out.
    YB_SKIP_TEST_IN_TSAN();
    PgLibPqTest::SetUp();
  }
};

Status PgLibPqTest::TestDuplicateCreateTableRequest(PGConn conn) {
  RETURN_NOT_OK(conn.Execute("CREATE TABLE tbl (k int primary key)"));
  RETURN_NOT_OK(conn.Execute("INSERT INTO tbl VALUES (1)"));
  const int k = VERIFY_RESULT(conn.FetchRow<int32_t>("SELECT * FROM tbl"));
  SCHECK_EQ(k, 1, IllegalState, "wrong result");

  return Status::OK();
}

Result<string> PgLibPqTest::GetSchemaName(const string& relname, PGConn* conn) {
  return conn->FetchRow<std::string>(Format(
      "SELECT nspname FROM pg_class JOIN pg_namespace "
      "ON pg_class.relnamespace = pg_namespace.oid WHERE relname = '$0'",
      relname));
}

// Ensure if client sends out duplicate create table requests, one create table request can
// succeed and the other create table request should fail.
TEST_F_EX(PgLibPqTest, DuplicateCreateTableRequest, PgLibPqDuplicateClientCreateTableTest) {
  ASSERT_OK(TestDuplicateCreateTableRequest(ASSERT_RESULT(Connect())));
}

// Ensure if client sends out duplicate create table requests, one create table request can
// succeed and the other create table request should fail in a colocated database.
TEST_F_EX(PgLibPqTest, DuplicateCreateTableRequestInColocatedDB,
          PgLibPqDuplicateClientCreateTableTest) {
  const string database_name = "col_db";
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", database_name));
  ASSERT_OK(TestDuplicateCreateTableRequest(ASSERT_RESULT(ConnectToDB(database_name))));
}


class PgLibPqRbsTests : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // colocated database.
    options->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=false");
    options->extra_master_flags.push_back("--tserver_unresponsive_timeout_ms=2000");
    options->extra_tserver_flags.push_back("--follower_unavailable_considered_failed_sec=5");
    options->extra_tserver_flags.push_back("--skip_flushed_entries=false");
  }
};

TEST_F(PgLibPqRbsTests, YB_DISABLE_TEST_IN_TSAN(ReplayRemoteBootstrappedTablet)) {
  const string database_name = "test_db";
  {
    PGConn conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", database_name));
  }
  FlushTablesAndCreateData(
      database_name /* database_name */, 30 /* timeout_secs */, true /* colocated */);

  // Stop a tserver and wait for it to be removed from quorum.
  cluster_->tablet_server(2)->Shutdown();

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  master::NamespaceIdentifierPB filter;
  client::YBTableName table_name;
  filter.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
  filter.set_name(database_name);
  auto tables = ASSERT_RESULT(client->ListUserTables(filter));
  for (const auto& table : tables) {
    if (table.table_name() == "bar") {
      LOG(INFO) << "Complete table details for bar table " << table.ToString();
      table_name = table;
    }
  }

  master::TabletLocationsPB colocated_tablet;
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(client->GetTablets(
        table_name, -1, &tablets, /* partition_list_version =*/ nullptr,
        RequireTabletsRunning::kTrue));
    EXPECT_EQ(tablets.size(), 1);
    colocated_tablet = tablets[0];
    LOG(INFO) << "Got tablet " << colocated_tablet.ShortDebugString();
    return colocated_tablet.replicas_size() == 2;
  }, 60s * kTimeMultiplier, "wait for replica count to become 2"));

  // Add a tserver, it should get remote bootstrapped and not crash.
  ASSERT_OK(cluster_->AddTabletServer(ExternalMiniClusterOptions::kDefaultStartCqlProxy, {}));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto tablets = VERIFY_RESULT(cluster_->GetTablets(cluster_->tablet_server(3)));
    bool found = false;
    for (const auto& tablet : tablets) {
      if (tablet.tablet_id() == colocated_tablet.tablet_id()) {
        LOG(INFO) << "Found tablet " << tablet.ShortDebugString();
        found = true;
        break;
      }
    }
    return found;
  }, 60s * kTimeMultiplier, "Wait for tablet to be present on tserver"));
}

class PgLibPqTest3Masters: public PgLibPqTest {
  int GetNumMasters() const override {
    return 3;
  }
};

TEST_F(PgLibPqTest3Masters, TabletBootstrapReplayChangeMetadataOp) {
  const std::string kDatabaseName = "testdb";

  // Get details about a master that is not the leader.
  auto follower_idx = ASSERT_RESULT(cluster_->GetFirstNonLeaderMasterIndex());
  std::string follower_uuid = cluster_->master(follower_idx)->uuid();

  // Set flag to skip apply on this follower.
  ASSERT_OK(cluster_->SetFlag(
      cluster_->master(follower_idx), "TEST_ignore_apply_change_metadata_on_followers", "true"));

  // Now create a database. This will trigger a bunch of ADD_TABLE change metadata
  // operations for the pg system tables which will only get applied on the leader
  // and 1 follower and not on the other follower due to the flag.
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));
  LOG(INFO) << "Database created successfully";

  // Reset the flag.
  ASSERT_OK(cluster_->SetFlag(
      cluster_->master(follower_idx), "TEST_ignore_apply_change_metadata_on_followers", "false"));

  // Shutdown and restart this follower now. During tablet bootstrap it should apply
  // these ADD_TABLE change metadata operations thus rebuilding state of the created
  // database completely and correctly.
  cluster_->master(follower_idx)->Shutdown();
  ASSERT_OK(cluster_->master(follower_idx)->Restart());
  LOG(INFO) << follower_idx << " has been restarted";

  // Wait for this master to join back the cluster.
  SleepFor(MonoDelta::FromSeconds(2 * kTimeMultiplier));

  // Stepdown the leader to this follower. If the above tablet bootstrap replayed
  // everything correctly, the created database should be usable now.
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader(follower_uuid));
  LOG(INFO) << follower_idx << " is the leader";

  // Try to connect to the new db and issue a few commands.
  PGConn conn_new = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn_new.Execute("CREATE TABLE foo (i int)"));
  ASSERT_OK(conn_new.Execute("INSERT INTO foo VALUES (10)"));
}

class PgLibPqTablegroupTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Enable tablegroup beta feature
    options->extra_tserver_flags.push_back("--ysql_beta_feature_tablegroup=true");
  }
};

TEST_F_EX(PgLibPqTest, TablegroupCreateTables,
          PgLibPqTablegroupTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";
  const string kTablegroupName = "tg1";

  // Let ts-1 be the leader of tablet.
  ASSERT_OK(cluster_->SetFlagOnMasters("use_create_table_leader_hint", "false"));
  ASSERT_OK(cluster_->SetFlag(
      cluster_->tablet_server(1), "TEST_skip_election_when_fail_detected", "true"));
  ASSERT_OK(cluster_->SetFlag(
      cluster_->tablet_server(2), "TEST_skip_election_when_fail_detected", "true"));

  // Make one follower ignore applying change metadata operations.
  ASSERT_OK(cluster_->SetFlag(
      cluster_->tablet_server(1), "TEST_ignore_apply_change_metadata_on_followers", "true"));

  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(kDatabaseName, kTablegroupName, &conn);

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test_tbl ("
      "h INT PRIMARY KEY,"
      "a INT,"
      "b FLOAT CONSTRAINT test_tbl_uniq UNIQUE WITH (colocation_id=654321)"
      ") WITH (colocation_id=123456) TABLEGROUP $0",
      kTablegroupName));

  cluster_->AssertNoCrashes();
}

TEST_F_EX(PgLibPqTest, TablegroupBasics,
          PgLibPqTablegroupTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";
  const string kTablegroupName ="test_tgroup";
  const string kTablegroupAltName = "test_alt_tgroup";
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_bar_index;

  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(kDatabaseName, kTablegroupName, &conn);

  // A parent table with one tablet should be created when the tablegroup is created.
  const auto tablegroup = ASSERT_RESULT(SelectTablegroup(
      client.get(), &conn, kDatabaseName, kTablegroupName));

  // Create a range partition table, the table should share the tablet with the parent table.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE foo (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0",
                               kTablegroupName));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup.tablet_id);

  // Create a index table that uses the tablegroup by default.
  ASSERT_OK(conn.Execute("CREATE INDEX foo_index1 ON foo (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo_index1"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup.tablet_id);

  // Create a hash partition table and dont use tablegroup.
  ASSERT_OK(conn.Execute("CREATE TABLE bar (a INT)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "bar"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  for (auto& tablet : tablets) {
    ASSERT_NE(tablet.tablet_id(), tablegroup.tablet_id);
  }

  // Create an index on the table not in a tablegroup. The index should follow the table
  // and opt out of the tablegroup.
  ASSERT_OK(conn.Execute("CREATE INDEX bar_index ON bar (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "bar_index"));
  const auto table_bar_index = ASSERT_RESULT(client->OpenTable(table_id));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  for (auto& tablet : tablets) {
    ASSERT_NE(tablet.tablet_id(), tablegroup.tablet_id);
  }
  tablets_bar_index.Swap(&tablets);

  // Create a range partition table without specifying primary key.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE baz (a INT) TABLEGROUP $0", kTablegroupName));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "baz"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup.tablet_id);

  // Create another table and index.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE qux (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0",
                               kTablegroupName));
  ASSERT_OK(conn.Execute("CREATE INDEX qux_index ON qux (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "qux"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup.tablet_id);
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "qux_index"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup.tablet_id);

  // Now create a second tablegroup.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupAltName));

  // A parent table with one tablet should be created when the tablegroup is created.
  auto tablegroup_alt = ASSERT_RESULT(SelectTablegroup(
      client.get(), &conn, kDatabaseName, kTablegroupAltName));

  // Create another range partition table - should be part of the second tablegroup
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE quuz (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0",
                               kTablegroupAltName));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "quuz"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup_alt.tablet_id);

  // Drop a table in the parent tablet.
  ASSERT_OK(conn.Execute("DROP TABLE quuz"));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "quuz"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "quuz_index"))));

  // Drop a table that is opted out.
  ASSERT_OK(conn.Execute("DROP TABLE bar"));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "bar"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "bar_index"))));

  // The tablets for bar_index should be deleted.
  std::vector<bool> tablet_founds(tablets_bar_index.size(), true);
  std::vector<promise_future_pair> tablet_promises_futures(tablets_bar_index.size());
  ASSERT_OK(WaitFor(
      [&] {
        for (int i = 0; i < tablets_bar_index.size(); ++i) {
          tablet_promises_futures[i].second = tablet_promises_futures[i].first.get_future();
          client->LookupTabletById(
              tablets_bar_index[i].tablet_id(),
              table_bar_index,
              master::IncludeInactive::kFalse,
              master::IncludeDeleted::kFalse,
              CoarseMonoClock::Now() + 30s,
              [&, i](const Result<client::internal::RemoteTabletPtr>& result) {
                tablet_promises_futures[i].first.set_value(result);
              },
              client::UseCache::kFalse);
        }
        for (int i = 0; i < tablets_bar_index.size(); ++i) {
          tablet_founds[i] = tablet_promises_futures[i].second.get().ok();
        }
        return std::all_of(
            tablet_founds.cbegin(),
            tablet_founds.cend(),
            [](bool tablet_found) {
              return !tablet_found;
            });
      },
      30s, "Drop table did not use tablegroups"));

  // Drop a tablegroup.
  ASSERT_TRUE(ASSERT_RESULT(client->TablegroupExists(kDatabaseName, tablegroup_alt.id)));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLEGROUP $0", kTablegroupAltName));
  ASSERT_FALSE(ASSERT_RESULT(client->TablegroupExists(kDatabaseName, tablegroup_alt.id)));

  // The alt tablegroup tablet should be deleted after dropping the tablegroup.
  bool alt_tablet_found = true;
  promise<Result<client::internal::RemoteTabletPtr>> alt_tablet_promise;
  auto alt_tablet_future = alt_tablet_promise.get_future();
  ASSERT_OK(WaitFor(
      [&] {
        client->LookupTabletById(
            tablegroup_alt.tablet_id,
            tablegroup_alt.table,
            master::IncludeInactive::kFalse,
            master::IncludeDeleted::kFalse,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              alt_tablet_promise.set_value(result);
            },
            client::UseCache::kFalse);
        alt_tablet_found = alt_tablet_future.get().ok();
        return !alt_tablet_found;
      },
      30s, "Drop tablegroup"));

  // Recreate that tablegroup. Being able to recreate it and add tables to it tests that it was
  // properly cleaned up from catalog manager maps and postgres metadata at time of DROP.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupAltName));

  // A parent table with one tablet should be created when the tablegroup is created.
  tablegroup_alt = ASSERT_RESULT(SelectTablegroup(
        client.get(), &conn, kDatabaseName, kTablegroupAltName));

  // Add a table back in and ensure that it is part of the recreated tablegroup.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE quuz (a INT, PRIMARY KEY (a ASC)) TABLEGROUP $0",
                               kTablegroupAltName));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "quuz"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), tablegroup_alt.tablet_id);

  // Drop the database.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDatabaseName));
  ASSERT_FALSE(ASSERT_RESULT(client->NamespaceExists(kDatabaseName, YQL_DATABASE_PGSQL)));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "foo"))));
  ASSERT_FALSE(ASSERT_RESULT(
        client->TableExists(client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, "foo_index1"))));

  // The original tablegroup tablet should be deleted after dropping the database.
  bool orig_tablet_found = true;
  promise<Result<client::internal::RemoteTabletPtr>> orig_tablet_promise;
  auto orig_tablet_future = orig_tablet_promise.get_future();
  ASSERT_OK(WaitFor(
      [&] {
        client->LookupTabletById(
            tablegroup.tablet_id,
            tablegroup.table,
            master::IncludeInactive::kFalse,
            master::IncludeDeleted::kFalse,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              orig_tablet_promise.set_value(result);
            },
            client::UseCache::kFalse);
        orig_tablet_found = orig_tablet_future.get().ok();
        return !orig_tablet_found;
      },
      30s, "Drop database with tablegroup"));

  // The second tablegroup tablet should also be deleted after dropping the database.
  bool second_tablet_found = true;
  promise<Result<client::internal::RemoteTabletPtr>> second_tablet_promise;
  auto second_tablet_future = second_tablet_promise.get_future();
  ASSERT_OK(WaitFor(
      [&] {
        client->LookupTabletById(
            tablegroup_alt.tablet_id,
            tablegroup_alt.table,
            master::IncludeInactive::kFalse,
            master::IncludeDeleted::kFalse,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              second_tablet_promise.set_value(result);
            },
            client::UseCache::kFalse);
        second_tablet_found = second_tablet_future.get().ok();
        return !second_tablet_found;
      },
      30s, "Drop database with tablegroup"));
}

TEST_F_EX(
    PgLibPqTest, TablegroupTruncateTable,
    PgLibPqTablegroupTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";
  const string kTablegroupName = "test_tgroup";

  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(kDatabaseName, kTablegroupName, &conn);

  // Create a table within the tablegroup and insert some values.
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE foo (a INT, value TEXT, PRIMARY KEY (a ASC)) TABLEGROUP $0", kTablegroupName));
  ASSERT_OK(conn.Execute("INSERT INTO foo (a, value) VALUES (1, 'hello')"));
  ASSERT_OK(conn.Execute("INSERT INTO foo (a, value) VALUES (2, 'hello2')"));
  ASSERT_OK(conn.Execute("INSERT INTO foo (a, value) VALUES (3, 'hello3')"));
  ASSERT_EQ(PQntuples(ASSERT_RESULT(conn.Fetch("SELECT * FROM foo")).get()), 3);

  // Create index and verify it's content by forcing an index scan.
  ASSERT_OK(conn.Execute("CREATE INDEX foo_index ON foo (a ASC)"));
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan("SELECT * FROM foo ORDER BY a")));
  const auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
      "SELECT * FROM foo ORDER BY a")));
  ASSERT_EQ(rows, (decltype(rows){{1, "hello"}, {2, "hello2"}, {3, "hello3"}}));

  // Create another table within the tablegroup and insert some values.
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE bar (a INT) TABLEGROUP $0", kTablegroupName));
  ASSERT_OK(conn.Execute("INSERT INTO bar (a) VALUES (100)"));
  ASSERT_OK(conn.Execute("INSERT INTO bar (a) VALUES (200)"));
  ASSERT_EQ(PQntuples(ASSERT_RESULT(conn.Fetch("SELECT * FROM bar")).get()), 2);

  // Create index on bar and verify it's content by forcing an index scan.
  ASSERT_OK(conn.Execute("CREATE INDEX bar_index ON bar (a DESC)"));
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan("SELECT * FROM bar ORDER BY a DESC")));
  auto values = ASSERT_RESULT(conn.FetchRows<int32_t>("SELECT * FROM bar ORDER BY a DESC"));
  ASSERT_EQ(values, (decltype(values){200, 100}));

  // Truncating foo works correctly.
  ASSERT_OK(conn.Execute("TRUNCATE TABLE foo"));
  ASSERT_OK(conn.FetchMatrix("SELECT * FROM foo", 0, 2));

  // Index scan on foo should also return 0 rows.
  const auto query = "SELECT * FROM foo ORDER BY a";
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  ASSERT_OK(conn.FetchMatrix(query, 0, 2));

  // Truncation of foo shouldn't affect bar.
  ASSERT_OK(conn.FetchMatrix("SELECT * FROM bar", 2, 1));
  const auto query2 = "SELECT * FROM bar ORDER BY a DESC";
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query2)));
  values = ASSERT_RESULT(conn.FetchRows<int32_t>(query2));
  ASSERT_EQ(values, (decltype(values){200, 100}));
}

TEST_F_EX(
    PgLibPqTest, TablegroupDDLs,
    PgLibPqTablegroupTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";
  const string kSchemaName = "test_schema";
  const string kTablegroupName = "test_tgroup";
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;

  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(kDatabaseName, kTablegroupName, &conn);

  // A parent table with one tablet should be created when the tablegroup is created.
  const auto tablegroup =
      ASSERT_RESULT(SelectTablegroup(client.get(), &conn, kDatabaseName, kTablegroupName));

  // Create a table within the tablegroup.
  ASSERT_OK(
      conn.ExecuteFormat("CREATE TABLE foo (a INT, value TEXT) TABLEGROUP $0", kTablegroupName));
  ASSERT_OK(conn.Execute("INSERT INTO foo (a, value) VALUES (1, 'hello')"));

  // Adding a PK works and inserting duplicate PK fails.
  ASSERT_OK(conn.Execute("ALTER TABLE foo ADD PRIMARY KEY (a)"));
  ASSERT_NOK(conn.Execute("INSERT INTO foo (a, value) VALUES (1, 'hello2')"));
  ASSERT_OK(conn.Execute("INSERT INTO foo (a, value) VALUES (2, 'hello2')"));

  // Creating a unique index on non-PK column works.
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX foo_unique_index ON foo(value)"));
  ASSERT_NOK(conn.Execute("INSERT INTO foo (a, value) VALUES (3, 'hello')"));
  ASSERT_OK(conn.Execute("INSERT INTO foo (a, value) VALUES (3, 'hello3')"));

  // Creating a view for the table works.
  ASSERT_OK(conn.ExecuteFormat("CREATE VIEW odd_a_view AS SELECT * FROM foo WHERE MOD(a, 2) = 1"));
  const auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
      "SELECT * FROM odd_a_view ORDER BY a")));
  ASSERT_EQ(rows, (decltype(rows){{1, "hello"}, {3, "hello3"}}));

  // Creating a table within the tablegroup with a different schema is successful.
  ASSERT_OK(conn.ExecuteFormat("CREATE SCHEMA $0", kSchemaName));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0.bar (a INT, value TEXT) TABLEGROUP $1", kSchemaName, kTablegroupName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0.bar (a, value) VALUES (1, 'hello')", kSchemaName));

  // Index on bar should work.
  ASSERT_OK(
      conn.ExecuteFormat("CREATE UNIQUE INDEX bar_unique_index ON $0.bar(value)", kSchemaName));
  ASSERT_NOK(conn.ExecuteFormat("INSERT INTO $0.bar (a, value) VALUES (2, 'hello')", kSchemaName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0.bar (a, value) VALUES (2, 'hello2')", kSchemaName));

  // All tables/indexes should be in the same tablet as the parent.
  auto relation_names = {"foo", "foo_unique_index", "bar", "bar_unique_index"};
  for (auto relation : relation_names) {
    auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, relation));
    ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
    ASSERT_EQ(tablets.size(), 1);
    ASSERT_EQ(tablets[0].tablet_id(), tablegroup.tablet_id);
  }
}

TEST_F_EX(
    PgLibPqTest, TablegroupCreationFailure,
    PgLibPqTablegroupTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";

  const string set_next_tablegroup_oid_sql = "SELECT binary_upgrade_set_next_tablegroup_oid($0)";

  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(kDatabaseName, "tg1", &conn);
  const auto database_oid = ASSERT_RESULT(GetDatabaseOid(&conn, kDatabaseName));

  // Expect the next tablegroup created to take the next OID.
  PgOid next_tg_oid = ASSERT_RESULT(GetTablegroupOid(&conn, "tg1")) + 1;
  TablegroupId next_tg_id = GetPgsqlTablegroupId(database_oid, next_tg_oid);

  // Force CREATE TABLEGROUP to fail, and delay the cleanup.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "3000"));
  ASSERT_OK(conn.TestFailDdl("CREATE TABLEGROUP tg2"));

  // Forcing PG to reuse tablegroup OID.
  ASSERT_OK(conn.Execute("SET yb_binary_restore TO true"));
  ASSERT_OK(conn.FetchFormat(set_next_tablegroup_oid_sql, next_tg_oid));
  // Cleanup hasn't been processed yet, so this fails.
  ASSERT_QUERY_FAIL(conn.Execute("CREATE TABLEGROUP tg3"),
                    "Duplicate tablegroup");

  // Wait for cleanup thread to delete a table.
  // Since delete hasn't started initially, WaitForDeleteTableToFinish will error out.
  const auto tg_parent_table_id = GetTablegroupParentTableId(next_tg_id);
  ASSERT_OK(WaitFor(
      [&client, &tg_parent_table_id] {
        Status s = client->WaitForDeleteTableToFinish(tg_parent_table_id);
        return s.ok();
      },
      30s,
      "Wait for tablegroup cleanup"));

  ASSERT_OK(conn.FetchFormat(set_next_tablegroup_oid_sql, next_tg_oid));
  ASSERT_OK(conn.Execute("CREATE TABLEGROUP tg4"));
  ASSERT_EQ(ASSERT_RESULT(GetTablegroupOid(&conn, "tg4")), next_tg_oid);
}

TEST_F_EX(
    PgLibPqTest, TablegroupCreationFailureWithRestart,
    PgLibPqTablegroupTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";

  const string set_next_tablegroup_oid_sql = "SELECT binary_upgrade_set_next_tablegroup_oid($0)";

  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(kDatabaseName, "tg1", &conn);

  // Expect the next tablegroup created to take the next OID.
  PgOid next_tg_oid = ASSERT_RESULT(GetTablegroupOid(&conn, "tg1")) + 1;
  const auto database_oid = ASSERT_RESULT(GetDatabaseOid(&conn, kDatabaseName));

  // Force CREATE TABLEGROUP to fail, and delay the cleanup.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "3000"));
  ASSERT_OK(conn.TestFailDdl("CREATE TABLEGROUP tg2"));

  // Verify that tablegroup is cleaned up on startup.
  cluster_->Shutdown();
  ASSERT_OK(cluster_->Restart());

  // Wait for cleanup thread to delete a table.
  // Since delete hasn't started initially, WaitForDeleteTableToFinish will error out.
  TablegroupId next_tg_id = GetPgsqlTablegroupId(database_oid, next_tg_oid);
  const auto tg_parent_table_id = GetTablegroupParentTableId(next_tg_id);
  ASSERT_OK(WaitFor(
    [&client, &tg_parent_table_id] {
      Status s = client->WaitForDeleteTableToFinish(tg_parent_table_id);
      return s.ok();
    },
    30s,
    "Wait for tablegroup cleanup"));

  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  // Cleanup (DeleteTable) has been processed during cluster startup, we're good to go.
  ASSERT_OK(conn.Execute("SET yb_binary_restore TO true"));
  ASSERT_OK(conn.FetchFormat(set_next_tablegroup_oid_sql, next_tg_oid));
  ASSERT_OK(conn.Execute("CREATE TABLEGROUP tg3"));
  ASSERT_EQ(ASSERT_RESULT(GetTablegroupOid(&conn, "tg3")), next_tg_oid);
}

TEST_F_EX(
    PgLibPqTest, TablegroupAccessMethods,
    PgLibPqTablegroupTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";
  const string kTablegroupName = "test_tgroup";
  const string kQuery = "SELECT * FROM $0 ORDER BY value";
  const string kQueryForIndexScan = "SELECT * FROM $0 ORDER BY a";
  const string kQueryForIndexOnlyScan = "SELECT a FROM $0 ORDER BY a";
  const std::vector<string> kTableNames = {"table_without_pk", "table_with_pk"};
  const std::vector<string> kCreateTableQueries = {
      Format("CREATE TABLE $0 (a INT, value TEXT) TABLEGROUP $1", kTableNames[0], kTablegroupName),
      Format(
          "CREATE TABLE $0 (a INT, value TEXT, PRIMARY KEY (a ASC)) TABLEGROUP $1", kTableNames[1],
          kTablegroupName)};

  auto conn = ASSERT_RESULT(Connect());
  CreateDatabaseWithTablegroup(kDatabaseName, kTablegroupName, &conn);

  for (size_t idx = 0; idx < kTableNames.size(); idx++) {
    // Create a table within the tablegroup and insert some values.
    ASSERT_OK(conn.Execute(kCreateTableQueries[idx]));
    ASSERT_OK(
        conn.ExecuteFormat("INSERT INTO $0 (a, value) VALUES (1, 'hello')", kTableNames[idx]));
    ASSERT_OK(
        conn.ExecuteFormat("INSERT INTO $0 (a, value) VALUES (2, 'hello2')", kTableNames[idx]));
    ASSERT_OK(
        conn.ExecuteFormat("INSERT INTO $0 (a, value) VALUES (3, 'hello3')", kTableNames[idx]));

    // Sequential scan.
    auto query = Format(kQuery, kTableNames[idx]);
    ASSERT_TRUE(ASSERT_RESULT(conn.HasScanType(query, "Seq")));
    auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(query)));
    ASSERT_EQ(rows, (decltype(rows){{1, "hello"}, {2, "hello2"}, {3, "hello3"}}));

    // Index scan.
    ASSERT_OK(
        conn.ExecuteFormat("CREATE UNIQUE INDEX foo_index_$0 ON $0 (a ASC)", kTableNames[idx]));
    auto queryForIndexScan = Format(kQueryForIndexScan, kTableNames[idx]);
    ASSERT_TRUE(ASSERT_RESULT(conn.HasScanType(queryForIndexScan, "Index")));
    rows = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(queryForIndexScan)));
    ASSERT_EQ(rows, (decltype(rows){{1, "hello"}, {2, "hello2"}, {3, "hello3"}}));

    // Index only scan.
    auto queryForIndexOnlyScan = Format(kQueryForIndexOnlyScan, kTableNames[idx]);
    ASSERT_TRUE(ASSERT_RESULT(conn.HasScanType(queryForIndexOnlyScan, "Index Only")));
    {
      const auto values = ASSERT_RESULT(conn.FetchRows<int32_t>(queryForIndexOnlyScan));
      ASSERT_EQ(values, (decltype(values){1, 2, 3}));
    }
  }
}

namespace {

class PgLibPqTestRF1: public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--replication_factor=1");
  }

  int GetNumMasters() const override {
    return 1;
  }

  int GetNumTabletServers() const override {
    return 1;
  }
};

} // namespace

// Test that the number of RPCs sent to master upon first connection is not too high.
// See https://github.com/yugabyte/yugabyte-db/issues/3049
// Test uses RF1 cluster to avoid possible relelections which affects the number of RPCs received
// by a master.
TEST_F_EX(PgLibPqTest, NumberOfInitialRpcs, PgLibPqTestRF1) {
  auto get_master_inbound_rpcs_created = [this]() -> Result<int64_t> {
    int64_t m_in_created = 0;
    for (const auto* master : this->cluster_->master_daemons()) {
      m_in_created += VERIFY_RESULT(master->GetMetric<int64>(
          &METRIC_ENTITY_server, "yb.master", &METRIC_rpc_inbound_calls_created, "value"));
    }
    return m_in_created;
  };

  auto rpcs_before = ASSERT_RESULT(get_master_inbound_rpcs_created());
  ASSERT_RESULT(Connect());
  auto rpcs_during = ASSERT_RESULT(get_master_inbound_rpcs_created()) - rpcs_before;

  // Real-world numbers (debug build, local PC): 58 RPCs
  LOG(INFO) << "Master inbound RPC during connection: " << rpcs_during;
  // RPC counter is affected no only by table read/write operations but also by heartbeat mechanism.
  // As far as ASAN builds are slower they can receive more heartbeats while processing requests.
  // As a result RPC count might be higher in comparison to other build types.
  ASSERT_LT(rpcs_during, 100);
}

namespace {

template<class T>
T RelativeDiff(T a, T b) {
  const auto d = std::max(std::abs(a), std::abs(b));
  return d == 0.0 ? 0.0 : std::abs(a - b) / d;
}

template<typename T>
concept HasExactRepresentation = std::numeric_limits<T>::is_exact || std::is_same_v<T, std::string>;

template<HasExactRepresentation T>
bool IsEqual(const T& a, const T& b) {
  return a == b;
}

bool IsEqual(long double a, long double b) {
  constexpr auto kRelativeThreshold = 1e-10;
  return RelativeDiff(a, b) < kRelativeThreshold;
}

// Run SELECT query of [T in] casted to the equivalent pg type. Check that FetchRow returns the
// same thing back.
template<typename Tag, typename T>
Status CheckFetch(PGConn* conn, const T& in, const std::string& type) {
  std::ostringstream ss;
  constexpr int kPrecision = 1000;
  // In case of a large float/double, all digits should be specified to avoid rounding to an
  // out-of-bounds number.
  ss << std::setprecision(kPrecision) << in;
  const auto query = Format("SELECT '$0'::$1", ss.str(), type);
  LOG(INFO) << "Query: " << query;

  auto out = VERIFY_RESULT(conn->FetchRow<Tag>(query));
  LOG(INFO) << "Result: " << out;
  SCHECK(IsEqual(in, out), IllegalState, Format("Unexpected result: in=$0, out=$1", in, out));
  return Status::OK();
}

template<typename T>
Status CheckFetch(PGConn* conn, const T& in, const std::string& type) {
  return CheckFetch<T, T>(conn, in, type);
}

} // namespace

TEST_F_EX(PgLibPqTest, Fetch, PgLibPqTestRF1) {
  constexpr auto kPgTypeBool = "bool";
  constexpr auto kPgTypeInt2 = "int2";
  constexpr auto kPgTypeInt4 = "int4";
  constexpr auto kPgTypeInt8 = "int8";
  constexpr auto kPgTypeFloat4 = "float4";
  constexpr auto kPgTypeFloat8 = "float8";
  constexpr auto kPgTypeOid = "oid";

  auto conn = ASSERT_RESULT(Connect());

  LOG(INFO) << "Test bool";
  ASSERT_OK(CheckFetch(&conn, false, kPgTypeBool));
  ASSERT_OK(CheckFetch(&conn, true, kPgTypeBool));

  LOG(INFO) << "Test signed ints";
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<int16_t>::min(), kPgTypeInt2));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<int16_t>::max(), kPgTypeInt2));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<int32_t>::min(), kPgTypeInt4));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<int32_t>::max(), kPgTypeInt4));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<int64_t>::min(), kPgTypeInt8));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<int64_t>::max(), kPgTypeInt8));

  LOG(INFO) << "Test float/double";
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<float>::lowest(), kPgTypeFloat4));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<float>::min(), kPgTypeFloat4));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<float>::max(), kPgTypeFloat4));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<double>::lowest(), kPgTypeFloat8));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<double>::min(), kPgTypeFloat8));
  ASSERT_OK(CheckFetch(&conn, std::numeric_limits<double>::max(), kPgTypeFloat8));

  LOG(INFO) << "Test string";
  const auto str = "hello     "s;
  ASSERT_OK(CheckFetch(&conn, str, "text"));
  ASSERT_OK(CheckFetch(&conn, str, "char(10)"));
  ASSERT_OK(CheckFetch(&conn, str, "bpchar"));
  ASSERT_OK(CheckFetch(&conn, str, "bpchar(10)"));
  ASSERT_OK(CheckFetch(&conn, str, "varchar"));
  ASSERT_OK(CheckFetch(&conn, str, "varchar(10)"));
  ASSERT_OK(CheckFetch(&conn, str, "cstring"));

  LOG(INFO) << "Test oid: unsigned int with no conversion";
  ASSERT_OK(CheckFetch<PGOid>(&conn, std::numeric_limits<Oid>::min(), kPgTypeOid));
  ASSERT_OK(CheckFetch<PGOid>(&conn, std::numeric_limits<Oid>::max(), kPgTypeOid));

  LOG(INFO) << "Test unsigned ints: signed int converted to unsigned int with sign check";
  ASSERT_NOK(CheckFetch<PGUint16>(&conn, -1, kPgTypeInt2));
  ASSERT_OK(CheckFetch<PGUint16>(&conn, 0, kPgTypeInt2));
  ASSERT_OK(CheckFetch<PGUint16>(&conn, std::numeric_limits<int16_t>::max(), kPgTypeInt2));
  ASSERT_NOK(CheckFetch<PGUint32>(&conn, -1, kPgTypeInt4));
  ASSERT_OK(CheckFetch<PGUint32>(&conn, 0, kPgTypeInt4));
  ASSERT_OK(CheckFetch<PGUint32>(&conn, std::numeric_limits<int32_t>::max(), kPgTypeInt4));
  ASSERT_NOK(CheckFetch<PGUint64>(&conn, -1, kPgTypeInt8));
  ASSERT_OK(CheckFetch<PGUint64>(&conn, 0, kPgTypeInt8));
  ASSERT_OK(CheckFetch<PGUint64>(&conn, std::numeric_limits<int64_t>::max(), kPgTypeInt8));
}

TEST_F(PgLibPqTest, RangePresplit) {
  const string kDatabaseName ="yugabyte";
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  auto conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.Execute("CREATE TABLE range(a int, PRIMARY KEY(a ASC)) " \
      "SPLIT AT VALUES ((100), (1000))"));

  auto ns_id = ASSERT_RESULT(GetNamespaceIdByNamespaceName(client.get(), kDatabaseName));
  ASSERT_FALSE(ns_id.empty());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "range"));

  // Validate that number of tablets created is 3.
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 3);
}

// Override the base test to start a cluster that kicks out unresponsive tservers faster.
class PgLibPqTestSmallTSTimeout : public PgLibPqTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=false");
    options->extra_master_flags.push_back("--tserver_unresponsive_timeout_ms=8000");
    options->extra_master_flags.push_back("--unresponsive_ts_rpc_timeout_ms=10000");
    options->extra_tserver_flags.push_back("--follower_unavailable_considered_failed_sec=8");
  }
};

void PgLibPqTest::AddTSToLoadBalanceSingleInstance(
    const auto timeout, const std::map<std::string, int>& ts_loads,
    const std::unique_ptr<yb::client::YBClient>& client) {
  const auto starting_num_tablet_servers = cluster_->num_tablet_servers();
  ExternalMiniClusterOptions opts;
  static const int tserver_unresponsive_timeout_ms = 8000;

  // Ensure each tserver has exactly one colocation/tablegroup tablet replica.
  ASSERT_EQ(ts_loads.size(), starting_num_tablet_servers);
  for (const auto& entry : ts_loads) {
    ASSERT_ONLY_NOTNULL(cluster_->tablet_server_by_uuid(entry.first));
    ASSERT_EQ(entry.second, 1);
    LOG(INFO) << "found ts " << entry.first << " has " << entry.second << " replicas";
  }

  // Add a tablet server.
  UpdateMiniClusterOptions(&opts);
  ASSERT_OK(cluster_->AddTabletServer(ExternalMiniClusterOptions::kDefaultStartCqlProxy,
                          opts.extra_tserver_flags));
  ASSERT_OK(cluster_->WaitForTabletServerCount(starting_num_tablet_servers + 1, timeout));

  // Wait for load balancing.  This should move some tablet-peers (e.g. of the colocation tablet,
  // system.transactions tablets) to the new tserver.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        bool is_idle = VERIFY_RESULT(client->IsLoadBalancerIdle());
        return !is_idle;
      },
      timeout,
      "wait for load balancer to be active"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return client->IsLoadBalancerIdle(); },
      timeout,
      "wait for load balancer to be idle"));

  // Remove a tablet server.
  cluster_->tablet_server(1)->Shutdown();

  // Wait for the master leader to mark it dead.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return cluster_->is_ts_stale(1);
  },
  MonoDelta::FromMilliseconds(2 * tserver_unresponsive_timeout_ms),
  "Is TS dead",
  MonoDelta::FromSeconds(1)));
}

void PgLibPqTest::TestLoadBalanceSingleColocatedDB(
    GetParentTableTabletLocation getParentTableTabletLocation) {
  const std::string database_name = "test_db";
  const auto timeout = 60s;
  const auto starting_num_tablet_servers = cluster_->num_tablet_servers();
  std::map<std::string, int> ts_loads;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocation = true", database_name));
  conn = ASSERT_RESULT(ConnectToDB(database_name));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE tbl (k INT, v INT)"));

  // Collect colocation tablet replica locations.
  {
    master::TabletLocationsPB tablet_locations =
        ASSERT_RESULT(getParentTableTabletLocation(client.get(), database_name,
                                                   &conn, timeout));
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
  }

  AddTSToLoadBalanceSingleInstance(timeout, ts_loads, client);

  // Collect colocation tablet replica locations and verify that load has been moved off
  // from the dead TS.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    master::TabletLocationsPB tablet_locations =
      VERIFY_RESULT(getParentTableTabletLocation(client.get(), database_name,
                                                 &conn, timeout));
    ts_loads.clear();
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
    // Ensure each colocation tablet replica is on the three tablet servers excluding the first
    // one, which is shut down.
    if (ts_loads.size() != starting_num_tablet_servers) {
      return false;
    }
    for (const auto& entry : ts_loads) {
      ExternalTabletServer* ts = cluster_->tablet_server_by_uuid(entry.first);
      if (ts == nullptr || ts == cluster_->tablet_server(1) || entry.second != 1) {
        return false;
      }
    }
    return true;
  },
  timeout,
  "Wait for load to be moved off from tserver 1"));
}

// Test that adding a tserver and removing a tserver causes the colocation tablet of a colocation
// database to adjust raft configuration off the old tserver and onto the new tserver.
TEST_F_EX(PgLibPqTest,
          LoadBalanceSingleColocatedDB,
          PgLibPqTestSmallTSTimeout) {
  TestLoadBalanceSingleColocatedDB(GetColocatedDbDefaultTablegroupTabletLocations);
}

// Test that adding a tserver and removing a tserver causes the tablegroup tablet to adjust raft
// configuration off the old tserver and onto the new tserver.
TEST_F_EX(
    PgLibPqTest, LoadBalanceSingleTablegroup, PgLibPqTestSmallTSTimeout) {
  const std::string database_name = "test_db";
  const string tablegroup_name = "test_tgroup";
  const auto timeout = 60s;
  const auto starting_num_tablet_servers = cluster_->num_tablet_servers();
  std::map<std::string, int> ts_loads;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());

  CreateDatabaseWithTablegroup(database_name, tablegroup_name, &conn);
  const auto tablegroup =
      ASSERT_RESULT(SelectTablegroup(client.get(), &conn, database_name, tablegroup_name));

  // Collect tablegroup tablet replica locations.
  {
    master::TabletLocationsPB tablet_locations = ASSERT_RESULT(GetTablegroupTabletLocations(
        client.get(),
        database_name,
        tablegroup.id,
        timeout));
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
  }

  AddTSToLoadBalanceSingleInstance(timeout, ts_loads, client);

  // Collect tablegroup tablet replica locations and verify that load has been moved off
  // from the dead TS.
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        master::TabletLocationsPB tablet_locations = VERIFY_RESULT(
            GetTablegroupTabletLocations(client.get(), database_name, tablegroup.id, timeout));
        ts_loads.clear();
        for (const auto& replica : tablet_locations.replicas()) {
          ts_loads[replica.ts_info().permanent_uuid()]++;
        }
        // Ensure each tablegroup tablet replica is on the three tablet servers excluding the first
        // one, which is shut down.
        if (ts_loads.size() != starting_num_tablet_servers) {
          return false;
        }
        for (const auto& entry : ts_loads) {
          ExternalTabletServer* ts = cluster_->tablet_server_by_uuid(entry.first);
          if (ts == nullptr || ts == cluster_->tablet_server(1) || entry.second != 1) {
            return false;
          }
        }
        return true;
      },
      timeout,
      "Wait for load to be moved off from tserver 1"));
}

void PgLibPqTest::AddTSToLoadBalanceMultipleInstances(
    const auto timeout,
    const std::unique_ptr<yb::client::YBClient>& client) {
  const size_t starting_num_tablet_servers = cluster_->num_tablet_servers();
  // Add a tablet server.
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(starting_num_tablet_servers + 1, timeout));

  // Wait for load balancing.  This should move some tablet-peers (e.g. of the colocation tablets,
  // system.transactions tablets) to the new tserver.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        bool is_idle = VERIFY_RESULT(client->IsLoadBalancerIdle());
        return !is_idle;
      },
      timeout,
      "wait for load balancer to be active"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return client->IsLoadBalancerIdle();
      },
      timeout,
      "wait for load balancer to be idle"));
}

void PgLibPqTest::VerifyLoadBalance(const std::map<std::string, int>& ts_loads) {
  constexpr int kNumDatabases = 3;
  // Ensure that the load is properly distributed.
  int min_load = kNumDatabases;
  int max_load = 0;
  for (const auto& entry : ts_loads) {
    if (entry.second < min_load) {
      min_load = entry.second;
    } else if (entry.second > max_load) {
      max_load = entry.second;
    }
  }
  LOG(INFO) << "Found max_load on a TS = " << max_load << ", and min_load on a ts = " << min_load;
  ASSERT_LT(max_load - min_load, 2);
  ASSERT_EQ(ts_loads.size(), kNumDatabases + 1);
}

void PgLibPqTest::TestLoadBalanceMultipleColocatedDB(
    GetParentTableTabletLocation getParentTableTabletLocation) {
  constexpr int num_databases = 3;
  const auto timeout = 60s;
  const std::string database_prefix = "co";
  std::map<std::string, int> ts_loads;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());

  for (int i = 0; i < num_databases; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0$1 WITH colocation = true",
                                 database_prefix, i));
    conn = ASSERT_RESULT(ConnectToDB(Format("$0$1", database_prefix, i)));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE tbl (k INT, v INT)"));
  }

  AddTSToLoadBalanceMultipleInstances(timeout, client);

  // Collect colocation tablets' replica locations.
  for (int i = 0; i < num_databases; ++i) {
    const std::string database_name = Format("$0$1", database_prefix, i);
    conn = ASSERT_RESULT(ConnectToDB(database_name));
    master::TabletLocationsPB tablet_locations = ASSERT_RESULT(
        getParentTableTabletLocation(client.get(), database_name,
                                     &conn, timeout));
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
  }

  VerifyLoadBalance(ts_loads);
}

// Test that adding a tserver causes colocation tablets of colocation databases to offload
// tablet-peers to the new tserver.
TEST_F(PgLibPqTest, LoadBalanceMultipleColocatedDB) {
  TestLoadBalanceMultipleColocatedDB(GetColocatedDbDefaultTablegroupTabletLocations);
}

TEST_F(PgLibPqTest, LoadBalanceMultipleTablegroups) {
  constexpr int num_databases = 3;
  const auto timeout = 60s;
  const std::string database_prefix = "test_db";
  const std::string tablegroup_prefix = "tg";
  std::map<std::string, int> ts_loads;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());

  for (int i = 0; i < num_databases; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0$1", database_prefix, i));
    conn = ASSERT_RESULT(ConnectToDB(Format("$0$1", database_prefix, i)));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0$1", tablegroup_prefix, i));
  }

  AddTSToLoadBalanceMultipleInstances(timeout, client);

  // Collect tablegroup tablets' replica locations.
  for (int i = 0; i < num_databases; ++i) {
    conn = ASSERT_RESULT(ConnectToDB(Format("$0$1", database_prefix, i)));
    const auto tablegroup = ASSERT_RESULT(SelectTablegroup(
        client.get(), &conn, Format("$0$1", database_prefix, i),
        Format("$0$1", tablegroup_prefix, i)));

    master::TabletLocationsPB tablet_locations = ASSERT_RESULT(GetTablegroupTabletLocations(
        client.get(), Format("$0$1", database_prefix, i), tablegroup.id, timeout));
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
  }

  VerifyLoadBalance(ts_loads);
}

// Override the base test to start a cluster with transparent retries on cache version mismatch
// disabled.
class PgLibPqTestNoRetry : public PgLibPqTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        "--TEST_ysql_disable_transparent_cache_refresh_retry=true");
  }
};

// This test is like "TestPgCacheConsistency#testVersionMismatchWithFailedRetry".  That one gets
// failures because the queries are "parse" message types, and we don't consider retry for those.
// These queries are "simple query" message types, so they should be considered for transparent
// retry.  The last factor is whether `--TEST_ysql_disable_transparent_cache_refresh_retry` is
// specified.
void PgLibPqTest::TestCacheRefreshRetry(const bool is_retry_disabled) {
  constexpr int kNumTries = 5;
  const std::string kNamespaceName = "yugabyte";
  const std::string kTableName = "t";
  int num_successes = 0;
  std::array<PGConn, 2> conns = {
    ASSERT_RESULT(ConnectToDB(kNamespaceName, true /* simple_query_protocol */)),
    ASSERT_RESULT(ConnectToDB(kNamespaceName, true /* simple_query_protocol */)),
  };

  ASSERT_OK(conns[0].ExecuteFormat("CREATE TABLE $0 (i int)", kTableName));
  // Make the catalog version cache up to date.
  ASSERT_OK(conns[1].FetchFormat("SELECT * FROM $0", kTableName));

  for (int i = 0; i < kNumTries; ++i) {
    ASSERT_OK(conns[0].ExecuteFormat("ALTER TABLE $0 ADD COLUMN j$1 int", kTableName, i));
    auto res = conns[1].FetchFormat("SELECT * FROM $0", kTableName);
    if (is_retry_disabled) {
      // Ensure that we fall under one of two cases:
      // 1. tserver gets updated catalog version before SELECT (rare)
      //    - YBCheckSharedCatalogCacheVersion causes YBRefreshCache
      //    - trying the SELECT requires getting the table schema, but it will be a cache miss since
      //      the whole cache was invalidated, so we get the up-to-date table schema and succeed
      // 2. tserver doesn't get updated catalog version before SELECT (common)
      //    - trying the SELECT causes catalog version mismatch
      if (res.ok()) {
        LOG(WARNING) << "SELECT was ok";
        num_successes++;
        continue;
      }
      auto msg = res.status().message().ToBuffer();
      ASSERT_TRUE(msg.find("Catalog Version Mismatch") != std::string::npos) << res.status();
    } else {
      // Ensure that the request is successful (thanks to retry).
      if (!res.ok()) {
        LOG(WARNING) << "SELECT was not ok: " << res.status();
        continue;
      }
      num_successes++;
    }
    // Make the catalog version cache up to date, if needed.
    ASSERT_OK(conns[1].FetchFormat("SELECT * FROM $0", kTableName));
  }

  LOG(INFO) << "number of successes: " << num_successes << "/" << kNumTries;
  if (is_retry_disabled) {
    // Expect at least half of the tries to fail with catalog version mismatch.  There can be some
    // successes because, between the ALTER and SELECT, the catalog version could have propogated
    // through shared memory (see `YBCheckSharedCatalogCacheVersion`).
    const int num_failures = kNumTries - num_successes;
    ASSERT_GE(num_failures, kNumTries / 2);
  } else {
    // Expect all the tries to succeed.  This is because it is unacceptable to fail when retries are
    // enabled.
    ASSERT_EQ(num_successes, kNumTries);
  }
}

TEST_F_EX(PgLibPqTest,
          CacheRefreshRetryDisabled,
          PgLibPqTestNoRetry) {
  TestCacheRefreshRetry(true /* is_retry_disabled */);
}

TEST_F(PgLibPqTest, CacheRefreshRetryEnabled) {
  TestCacheRefreshRetry(false /* is_retry_disabled */);
}

class PgLibPqTestEnumType: public PgLibPqTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--TEST_do_not_add_enum_sort_order=true");
  }
};

void PgLibPqTest::KillPostmasterProcessOnTservers() {
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    ExternalTabletServer* ts = cluster_->tablet_server(i);
    const string pg_pid_file = JoinPathSegments(ts->GetRootDir(), "pg_data",
                                                "postmaster.pid");

    LOG(INFO) << "pg_pid_file: " << pg_pid_file;
    ASSERT_TRUE(Env::Default()->FileExists(pg_pid_file));
    std::ifstream pg_pid_in;
    pg_pid_in.open(pg_pid_file, std::ios_base::in);
    ASSERT_FALSE(pg_pid_in.eof());
    pid_t pg_pid = 0;
    pg_pid_in >> pg_pid;
    ASSERT_GT(pg_pid, 0);
    LOG(INFO) << "Killing PostgresSQL process: " << pg_pid;
    ASSERT_EQ(kill(pg_pid, SIGKILL), 0);
  }
}

// Make sure that enum type backfill works.
TEST_F_EX(PgLibPqTest,
          EnumType,
          PgLibPqTestEnumType) {
  const string kDatabaseName ="yugabyte";
  const string kTableName ="enum_table";
  const string kEnumTypeName ="enum_type";
  auto conn = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  ASSERT_OK(conn->ExecuteFormat(
    "CREATE TYPE $0 as enum('b', 'e', 'f', 'c', 'a', 'd')", kEnumTypeName));
  ASSERT_OK(conn->ExecuteFormat(
    "CREATE TABLE $0 (id $1)",
    kTableName,
    kEnumTypeName));
  ASSERT_OK(conn->ExecuteFormat("INSERT INTO $0 VALUES ('a')", kTableName));
  ASSERT_OK(conn->ExecuteFormat("INSERT INTO $0 VALUES ('b')", kTableName));
  ASSERT_OK(conn->ExecuteFormat("INSERT INTO $0 VALUES ('c')", kTableName));

  // Do table scan to verify contents the table with an ORDER BY clause. This
  // ensures that old enum values which did not have sort order can be read back,
  // sorted and displayed correctly.
  const std::string query = Format("SELECT * FROM $0 ORDER BY id", kTableName);
  ASSERT_FALSE(ASSERT_RESULT(conn->HasIndexScan(query)));
  auto values = ASSERT_RESULT(conn->FetchRows<std::string>(query));
  ASSERT_EQ(values, (decltype(values){"b", "c", "a"}));

  // Now alter the gflag so any new values will have sort order added.
  ASSERT_OK(cluster_->SetFlagOnTServers(
              "TEST_do_not_add_enum_sort_order", "false"));

  // Disconnect from the database so we don't have a case where the
  // postmaster dies while clients are still connected.
  conn = nullptr;

  // For each tablet server, kill the corresponding PostgreSQL process.
  // A new PostgreSQL process will be respawned by the tablet server and
  // inherit the new --TEST_do_not_add_enum_sort_order flag from the tablet
  // server.
  KillPostmasterProcessOnTservers();

  // Reconnect to the database after the new PostgreSQL starts.
  conn = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));

  // Insert three more rows with --TEST_do_not_add_enum_sort_order=false.
  // The new enum values will have sort order added.
  ASSERT_OK(conn->ExecuteFormat("INSERT INTO $0 VALUES ('d')", kTableName));
  ASSERT_OK(conn->ExecuteFormat("INSERT INTO $0 VALUES ('e')", kTableName));
  ASSERT_OK(conn->ExecuteFormat("INSERT INTO $0 VALUES ('f')", kTableName));

  // Do table scan again to verify contents the table with an ORDER BY clause.
  // This ensures that old enum values which did not have sort order, mixed
  // with new enum values which have sort order, can be read back, sorted and
  // displayed correctly.
  ASSERT_FALSE(ASSERT_RESULT(conn->HasIndexScan(query)));
  values = ASSERT_RESULT(conn->FetchRows<std::string>(query));
  ASSERT_EQ(values, (decltype(values){"b", "e", "f", "c", "a", "d"}));

  // Create an index on the enum table column.
  ASSERT_OK(conn->ExecuteFormat("CREATE INDEX ON $0 (id ASC)", kTableName));

  // Index only scan to verify contents of index table.
  ASSERT_TRUE(ASSERT_RESULT(conn->HasIndexScan(query)));
  values = ASSERT_RESULT(conn->FetchRows<std::string>(query));
  ASSERT_EQ(values, (decltype(values){"b", "e", "f", "c", "a", "d"}));

  // Test where clause.
  const std::string query2 = Format("SELECT * FROM $0 where id = 'b'", kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn->HasIndexScan(query2)));
  ASSERT_EQ(ASSERT_RESULT(conn->FetchRow<std::string>(query2)), "b");
}

// Test postgres large oid (>= 2^31). Internally postgres oid is an unsigned 32-bit integer. But
// when extended to Datum type (unsigned long), the sign-bit is extended so that the high 32-bit
// is ffffffff. This caused unexpected assertion failures and errors.
class PgLibPqLargeOidTest: public PgLibPqTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
      Format("--TEST_ysql_oid_prefetch_adjustment=$0", kOidAdjustment));
  }
  const Oid kOidAdjustment = 2147483648U - kPgFirstNormalObjectId; // 2^31 - 16384
};
TEST_F_EX(PgLibPqTest,
          LargeOid,
          PgLibPqLargeOidTest) {
  // Test large OID with enum type which had Postgres Assert failure.
  const string kDatabaseName ="yugabyte";
  const string kTableName ="enum_table";
  const string kEnumTypeName ="enum_type";
  PGConn conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TYPE $0 as enum('a', 'c')", kEnumTypeName));
  // Do ALTER TYPE to ensure we correctly put sort order as the high 32-bit after clearing
  // the signed extended ffffffff. The following index scan would yield wrong order if we
  // left ffffffff in the high 32-bit.
  ASSERT_OK(conn.ExecuteFormat("ALTER TYPE $0 ADD VALUE 'b' BEFORE 'c'", kEnumTypeName));
  auto query = "SELECT oid FROM pg_enum"s;
  auto values = ASSERT_RESULT(conn.FetchRows<PGOid>(query));
  ASSERT_EQ(values.size(), 3);
  // Ensure that we do see large OIDs in pg_enum table.
  for (const auto& oid : values) {
    ASSERT_GT(oid, kOidAdjustment);
  }

  // Create a table using the enum type and insert a few rows.
  ASSERT_OK(conn.ExecuteFormat(
    "CREATE TABLE $0 (id $1)",
    kTableName,
    kEnumTypeName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ('a'), ('b'), ('c')", kTableName));

  // Create an index on the enum table column.
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (id ASC)", kTableName));

  // Index only scan to verify that with large OIDs, the contents of index table
  // is still correct. This also triggers index backfill statement, which used to
  // fail on large oid such as:
  // BACKFILL INDEX 2147500041 WITH x'0880011a00' READ TIME 6725053491126669312 PARTITION x'5555';
  // We fix the syntax error by rewriting it to
  // BACKFILL INDEX -2147467255 WITH x'0880011a00' READ TIME 6725053491126669312 PARTITION x'5555';
  // Internally, -2147467255 will be reinterpreted as OID 2147500041 which is the OID of the index.
  query = Format("SELECT * FROM $0 ORDER BY id", kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  {
    auto values = ASSERT_RESULT(conn.FetchRows<std::string>(query));
    ASSERT_EQ(values, (decltype(values){"a", "b", "c"}));
  }
}

namespace {

class CoordinatedRunner {
 public:
  using RepeatableCommand = std::function<Status()>;

  explicit CoordinatedRunner(std::vector<RepeatableCommand> commands)
      : barrier_(commands.size()) {
    threads_.reserve(commands.size());
    for (auto& c : commands) {
      threads_.emplace_back([this, cmd = std::move(c)] () {
        while (!(stop_.load(std::memory_order_acquire) ||
            error_detected_.load(std::memory_order_acquire))) {
          barrier_.Wait();
          const auto status = cmd();
          if (!status.ok()) {
            LOG(ERROR) << "Error detected: " << status;
            error_detected_.store(true, std::memory_order_release);
          }
        }
        barrier_.Detach();
      });
    }
  }

  void Stop() {
    stop_.store(true, std::memory_order_release);
    for (auto& thread : threads_) {
      thread.join();
    }
  }

  bool HasError() {
    return error_detected_.load(std::memory_order_acquire);
  }

 private:
  std::vector<std::thread> threads_;
  Barrier barrier_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> error_detected_{false};
};

} // namespace

TEST_F(PgLibPqTest, PagingReadRestart) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT generate_series(1, 5000)"));
  const size_t reader_count = 20;
  std::vector<CoordinatedRunner::RepeatableCommand> commands;
  commands.reserve(reader_count + 1);
  commands.emplace_back(
      [connection = std::make_shared<PGConn>(ASSERT_RESULT(Connect()))] () -> Status {
        RETURN_NOT_OK(connection->Execute("ALTER TABLE t ADD COLUMN v INT DEFAULT 100"));
        RETURN_NOT_OK(connection->Execute("ALTER TABLE t DROP COLUMN v"));
        return Status::OK();
  });
  for (size_t i = 0; i < reader_count; ++i) {
    commands.emplace_back(
        [connection = std::make_shared<PGConn>(ASSERT_RESULT(Connect()))] () -> Status {
          const auto res = connection->Fetch("SELECT key FROM t");
          return (res.ok() || IsRetryable(res.status())) ? Status::OK() : res.status();
    });
  }
  CoordinatedRunner runner(std::move(commands));
  std::this_thread::sleep_for(10s);
  runner.Stop();
  ASSERT_FALSE(runner.HasError());
}

TEST_F(PgLibPqTest, CollationRangePresplit) {
  const string kDatabaseName ="yugabyte";
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  auto conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.Execute("CREATE TABLE collrange(a text COLLATE \"en-US-x-icu\", "
                         "PRIMARY KEY(a ASC)) SPLIT AT VALUES (('100'), ('200'))"));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "collrange"));

  // Validate that number of tablets created is 3.
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 3);
  // Partition key length of plain encoded '100' or '200'.
  const size_t partition_key_length = 7;
  // When a text value is collation encoded, we need at least 3 extra bytes.
  const size_t min_collation_extra_bytes = 3;
  for (const auto& tablet : tablets) {
    ASSERT_TRUE(tablet.has_partition());
    auto partition_start = tablet.partition().partition_key_start();
    auto partition_end = tablet.partition().partition_key_end();
    LOG(INFO) << "partition_start: " << b2a_hex(partition_start)
              << ", partition_end: " << b2a_hex(partition_end);
    ASSERT_TRUE(partition_start.empty() ||
                partition_start.size() >= partition_key_length + min_collation_extra_bytes);
    ASSERT_TRUE(partition_end.empty() ||
                partition_end.size() >= partition_key_length + min_collation_extra_bytes);
  }
}

// The motive of this test is to prove that when a postgres backend crashes
// while possessing an LWLock, the postmaster will kill all postgres backends
// and would perform a restart.
// TEST_lwlock_crash_after_acquire_lock_pg_stat_statements_reset when set true
// will crash a postgres backend after acquiring a LWLock. Specifically in this
// example, when pg_stat_statements_reset() function is called when this flag
// is set, it crashes after acquiring a lock on pgss->lock. This causes the
// postmaster to terminate all the connections. Hence, the SELECT 1 that is
// executed by conn2 also fails.
class PgLibPqYSQLBackendCrash: public PgLibPqTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        Format("--TEST_yb_lwlock_crash_after_acquire_pg_stat_statements_reset=true"));
    options->extra_tserver_flags.push_back(
        Format("--yb_backend_oom_score_adj=" + expected_backend_oom_score));
    options->extra_tserver_flags.push_back(
        Format("--yb_webserver_oom_score_adj=" + expected_webserver_oom_score));
  }

 protected:
  const std::string expected_backend_oom_score = "123";
  const std::string expected_webserver_oom_score = "456";
};

TEST_F_EX(PgLibPqTest,
          TestLWPgBackendKillAfterLWLockAcquire,
          PgLibPqYSQLBackendCrash) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_NOK(conn1.Fetch("SELECT pg_stat_statements_reset()"));
  ASSERT_NOK(conn2.Fetch("SELECT 1"));

  // validate that this query is added to yb_terminated_queries
  auto conn3 = ASSERT_RESULT(Connect());
  const string get_yb_terminated_queries =
    "SELECT query_text, termination_reason FROM yb_terminated_queries";
  auto row = ASSERT_RESULT((conn3.FetchRow<std::string, std::string>(get_yb_terminated_queries)));
  ASSERT_EQ(row, (decltype(row){"SELECT pg_stat_statements_reset()", "Terminated by SIGKILL"}));
}

#ifdef __linux__
TEST_F_EX(PgLibPqTest,
          TestOomScoreAdjPGBackend,
          PgLibPqYSQLBackendCrash) {

  auto conn = ASSERT_RESULT(Connect());
  auto backend_pid = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
  std::string file_name = "/proc/" + std::to_string(backend_pid) + "/oom_score_adj";
  std::ifstream fPtr(file_name);
  std::string oom_score_adj;
  getline(fPtr, oom_score_adj);
  ASSERT_EQ(oom_score_adj, expected_backend_oom_score);
}
TEST_F_EX(PgLibPqTest,
          TestOomScoreAdjPGWebserver,
          PgLibPqYSQLBackendCrash) {

  // Find the postmaster pid (parent process of our webserver)
  string postmaster_pid;
  auto conn = ASSERT_RESULT(Connect());
  auto backend_pid = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
  RunShellProcess(Format("ps -o ppid= $0", backend_pid), &postmaster_pid);

  // Get the webserver pid using postmaster pid
  string webserver_pid;
  RunShellProcess(Format("pgrep -f 'YSQL webserver' -P $0", postmaster_pid), &webserver_pid);
  webserver_pid.erase(std::remove(webserver_pid.begin(), webserver_pid.end(), '\n'),
                      webserver_pid.end());

  // Check the webserver's OOM score
  std::string file_name = "/proc/" + webserver_pid + "/oom_score_adj";
  std::ifstream fPtr(file_name);
  std::string oom_score_adj;
  getline(fPtr, oom_score_adj);
  ASSERT_EQ(oom_score_adj, expected_webserver_oom_score);
}
#endif

TEST_F_EX(PgLibPqTest, YbTableProperties, PgLibPqTestRF1) {
  const string kDatabaseName = "yugabyte";
  const string kTableName = "test";

  auto conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.Execute("CREATE TABLE test (k int, v int, PRIMARY KEY (k ASC))"));
  ASSERT_OK(conn.Execute("INSERT INTO test SELECT i, i FROM generate_series(1,100) AS i"));
  const string query1 = "SELECT * FROM yb_table_properties('test'::regclass)";
  auto row = ASSERT_RESULT((
      conn.FetchRow<PGUint64, PGUint64, bool, std::optional<PGOid>, std::optional<PGOid>>(query1)));
  ASSERT_EQ(row, (decltype(row){1, 0, false, std::nullopt, std::nullopt}));
  const string query2 = "SELECT * FROM yb_get_range_split_clause('test'::regclass)";
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(query2)), "");
  const TabletId tablet_to_split = ASSERT_RESULT(GetSingleTabletId(kTableName));
  LOG(INFO) << "tablet_to_split: " << tablet_to_split;
  auto output = ASSERT_RESULT(RunYbAdminCommand("flush_table ysql.yugabyte test"));
  LOG(INFO) << "flush_table command output: " << output;

  output = ASSERT_RESULT(
      RunYbAdminCommand(Format("split_tablet $0", tablet_to_split)));
  LOG(INFO) << "split_tablet command output: " << output;

  // Wait for the tablet split to complete.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  do {
    std::this_thread::sleep_for(1s);
    ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  } while (tablets.size() < 2);
  ASSERT_EQ(tablets.size(), 2);

  // Execute simple command to update table's partitioning at pggate side.
  // The split_tablet command does not increment catalog version or table
  // schema version. It increments partition_list_version.
  ASSERT_OK(conn.FetchFormat("SELECT count(*) FROM $0", kTableName));

  row = ASSERT_RESULT((
      conn.FetchRow<PGUint64, PGUint64, bool, std::optional<PGOid>, std::optional<PGOid>>(query1)));
  ASSERT_EQ(row, (decltype(row){2, 0, false, std::nullopt, std::nullopt}));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(query2)), "SPLIT AT VALUES ((49))");
}

TEST_F(PgLibPqTest, AggrSystemColumn) {
  const string kDatabaseName = "yugabyte";
  auto conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  // Count oid column which is a system column.
  const auto count_oid = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(oid) FROM pg_type"));

  // Count oid column which is a system column, but cast oid to int.
  const auto count_oid_int = ASSERT_RESULT(
      conn.FetchRow<PGUint64>("SELECT COUNT(oid::int) FROM pg_type"));
  // Should get the same count.
  ASSERT_EQ(count_oid_int, count_oid);

  // Count typname column which is a regular column.
  const auto count_typname = ASSERT_RESULT(
      conn.FetchRow<PGUint64>("SELECT COUNT(typname) FROM pg_type"));
  // Should get the same count.
  ASSERT_EQ(count_oid, count_typname);

  // Test unsupported system columns which would otherwise get the same count as shown
  // in vanilla Postgres.
  ASSERT_NOK(conn.Fetch("SELECT COUNT(ctid) FROM pg_type"));
  ASSERT_NOK(conn.Fetch("SELECT COUNT(cmin) FROM pg_type"));
  ASSERT_NOK(conn.Fetch("SELECT COUNT(cmax) FROM pg_type"));
  ASSERT_NOK(conn.Fetch("SELECT COUNT(xmin) FROM pg_type"));
  ASSERT_NOK(conn.Fetch("SELECT COUNT(xmax) FROM pg_type"));

  // Test SUM(oid) results in error.
  ASSERT_NOK(conn.Fetch("SELECT SUM(oid) FROM pg_type"));
}

class PgLibPqLegacyColocatedDBTest : public PgLibPqTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Override the flag value set in parent class PgLibPqTest to enable to create legacy colocated
    // databases.
    options->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=true");
  }
};

class PgLibPqLegacyColocatedDBFailOnConflictTest : public PgLibPqLegacyColocatedDBTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    UpdateMiniClusterFailOnConflict(options);
    PgLibPqLegacyColocatedDBTest::UpdateMiniClusterOptions(options);
  }
};

TEST_F_EX(PgLibPqTest, LegacyColocatedDBTableColocation, PgLibPqLegacyColocatedDBTest) {
  TestTableColocation(GetLegacyColocatedDBTabletLocations);
}

// Test for ensuring that transaction conflicts work as expected for colocated tables in a legacy
// colocated database.
TEST_F_EX(PgLibPqTest,
    TxnConflictsForColocatedTablesInLegacyColocatedDB,
    PgLibPqLegacyColocatedDBFailOnConflictTest) {
  auto conn = ASSERT_RESULT(Connect());
  const string database_name = "test_db";
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocation = true", database_name));
  PerformSimultaneousTxnsAndVerifyConflicts("test_db" /* database_name */, true /* colocated */);
}

// Ensure tablet bootstrap doesn't crash when replaying change metadata operations
// for a deleted colocated table in a legacy colocated database.
TEST_F_EX(PgLibPqTest,
    ReplayDeletedTableInLegacyColocatedDB, PgLibPqLegacyColocatedDBTest) {
  PGConn conn = ASSERT_RESULT(Connect());
  const string database_name = "test_db";
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocation = true", database_name));
  FlushTablesAndPerformBootstrap(
      "test_db" /* database_name */, 30 /* timeout_secs */, true /* colocated */,
      false /* test_backward_compatibility */);
}

class PgLibPqLegacyColocatedDBTestSmallTSTimeout : public PgLibPqTestSmallTSTimeout {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Override the flag value set in parent class to enable to create legacy colocated
    // databases.
    options->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=true");
    options->extra_master_flags.push_back("--tserver_unresponsive_timeout_ms=8000");
    options->extra_master_flags.push_back("--unresponsive_ts_rpc_timeout_ms=10000");
    options->extra_tserver_flags.push_back("--follower_unavailable_considered_failed_sec=8");
  }
};

// Test that adding a tserver and removing a tserver causes the colocation tablet of a legacy
// database to adjust raft configuration off the old tserver and onto the new tserver.
TEST_F_EX(PgLibPqTest, LoadBalanceSingleLegacyColocatedDB,
    PgLibPqLegacyColocatedDBTestSmallTSTimeout) {
  TestLoadBalanceSingleColocatedDB(GetLegacyColocatedDBTabletLocations);
}

// Test that adding a tserver causes colocation tablets of legacy colocated databases to offload
// tablet-peers to the new tserver.
TEST_F_EX(PgLibPqTest, LoadBalanceMultipleLegacyColocatedDB,
    PgLibPqLegacyColocatedDBTest) {
  TestLoadBalanceMultipleColocatedDB(GetLegacyColocatedDBTabletLocations);
}

class PgLibPqLegacyColocatedDBTableColocationEnabledByDefaultTest
    : public PgLibPqTableColocationEnabledByDefaultTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgLibPqTableColocationEnabledByDefaultTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=true");
  }
};

TEST_F_EX(PgLibPqTest,
    LegacyColocatedDBTableColocationEnabledByDefault,
    PgLibPqLegacyColocatedDBTableColocationEnabledByDefaultTest) {
  TestTableColocationEnabledByDefault(GetLegacyColocatedDBTabletLocations);
}

// Instead of introducing test sleeps in the code, use wait-on-conflict feature to make testing the
// timeout functionality easier.
class PgLibPqTestStatementTimeout : public PgLibPqTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgLibPqTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
        Format("--ysql_pg_conf_csv=statement_timeout=$0", kClientStatementTimeoutSeconds * 1000));
    options->extra_tserver_flags.push_back("--enable_wait_queues=true");
  }

  Result<std::future<Status>> ExpectBlockedAsync(
      pgwrapper::PGConn* conn, const std::string& query) {
    auto status = std::async(std::launch::async, [&conn, query]() {
      return conn->Execute(query);
    });

    RETURN_NOT_OK(WaitFor([&conn] () {
      return conn->IsBusy();
    }, 1s * kTimeMultiplier, "Wait for blocking request to be submitted to the query layer"));
    return status;
  }

 protected:
  static constexpr int kClientStatementTimeoutSeconds = 4;
};

TEST_F_EX(
    PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestStatementTimeout), PgLibPqTestStatementTimeout) {
  auto setup_conn = ASSERT_RESULT(Connect());

  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("insert into foo VALUES (1, 1)"));

  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  ASSERT_OK(conn1.Execute("UPDATE foo SET v=v+10 WHERE k=1"));

  auto status_future =
      ASSERT_RESULT(ExpectBlockedAsync(&conn2, "UPDATE foo SET v=v+100 WHERE k=1"));

  SleepFor(MonoDelta::FromSeconds(kClientStatementTimeoutSeconds * 2));
  // conn2 should not wait on conn1 to release the lock, for reporting back the
  // timeout error to the client.
  ASSERT_OK(WaitFor([&status_future] () {
    return status_future.wait_for(0s) == std::future_status::ready;
  }, 1s, "Wait for status_future to be available"));
  ASSERT_NOK(status_future.get());
}

class PgOidCollisionTestBase : public PgLibPqTest {
 protected:
  void RestartClusterWithOidAllocator(bool per_database) {
    cluster_->Shutdown();
    const string oid_allocator_gflag =
        Format("--ysql_enable_pg_per_database_oid_allocator=$0",
               per_database ? "true" : "false");
    const string create_database_retry_gflag =
        Format("--ysql_enable_create_database_oid_collision_retry=$0",
               ysql_enable_create_database_oid_collision_retry ? "true" : "false");
    LOG(INFO) << "Restart cluster with " << oid_allocator_gflag << " "
              << create_database_retry_gflag;
    for (size_t i = 0; i != cluster_->num_masters(); ++i) {
      cluster_->master(i)->mutable_flags()->push_back(oid_allocator_gflag);
    }
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      cluster_->tablet_server(i)->mutable_flags()->push_back(oid_allocator_gflag);
      cluster_->tablet_server(i)->mutable_flags()->push_back(create_database_retry_gflag);
    }
    ASSERT_OK(cluster_->Restart());
  }
  bool ysql_enable_create_database_oid_collision_retry = true;
};

class PgOidCollisionTest
    : public PgOidCollisionTestBase,
      public ::testing::WithParamInterface<bool> {
};

INSTANTIATE_TEST_CASE_P(PgOidCollisionTest,
                        PgOidCollisionTest,
                        ::testing::Values(false, true));

// Test case for PG per-database oid allocation.
// Using the old PG global oid allocation method, we can hit the following OID collision.
// Connections used in the example:
// Connection 1 on tserver 0 (Conn1) && Connection 2 on tserver 1 (Conn2)
// Example:
// Conn1: CREATE TABLE tbl (k INT); -- tbl OID: 16384
// Conn1: CREATE MATERIALIZED VIEW mv AS SELECT * FROM tbl; -- mv OID: 16387, mv relfilenode: 16387
// Conn1: REFRESH MATERIALIZED VIEW mv; -- mv OID: 16387, mv relfilenode: 16391
// Conn1: CREATE DATABASE db2; -- Used to trigger the same range of OID allocation on tserver 1
// Conn2: \c db2
// Conn2: CREATE TABLE trigger_oid_allocation (k INT); -- trigger_oid_allocation OID: 16384
// Conn2: create 4 dummy types to increase OID
// Conn2: \c yugabyte
// Conn2: CREATE TABLE danger (k INT); -- danger OID: 16391 (same as relfilenode of mv, so danger
// and refreshed mv uses the same table id in DocDB as well)
TEST_P(PgOidCollisionTest, MaterializedViewPgOidCollisionFromTservers) {
  const bool ysql_enable_pg_per_database_oid_allocator = GetParam();
  RestartClusterWithOidAllocator(ysql_enable_pg_per_database_oid_allocator);
  const string db2 = "db2";
  // Tserver 0
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE tbl (k INT)"));
  ASSERT_OK(conn1.Execute("CREATE MATERIALIZED VIEW mv AS SELECT * FROM tbl"));
  ASSERT_OK(conn1.Execute("REFRESH MATERIALIZED VIEW mv"));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0", db2));
  // Tserver 1
  LOG(INFO) << "Make a new connection to a different node at index 1";
  pg_ts = cluster_->tablet_server(1);
  auto conn2 = ASSERT_RESULT(ConnectToDB(db2));
  ASSERT_OK(conn2.Execute("CREATE TABLE trigger_oid_allocation (k INT)"));
  for (int i = 0; i < 4; ++i) {
    ASSERT_OK(conn2.ExecuteFormat("CREATE TYPE dummy$0", i));
  }
  conn2 = ASSERT_RESULT(Connect());
  auto status = conn2.Execute("CREATE TABLE danger (k INT, V INT)");
  if (ysql_enable_pg_per_database_oid_allocator) {
    ASSERT_OK(status);
    ASSERT_OK(conn2.Execute("INSERT INTO danger VALUES (1, 1)"));
    // Verify
    ASSERT_OK(conn1.FetchMatrix("SELECT * FROM mv", 0, 1));
    auto row = ASSERT_RESULT((conn1.FetchRow<int32_t, int32_t>("SELECT * FROM danger")));
    ASSERT_EQ(row, (decltype(row){1, 1}));
  } else {
    ASSERT_TRUE(status.IsNetworkError()) << status;
    ASSERT_STR_CONTAINS(status.ToString(), "Duplicate table");
  }
}

// Test case for PG per-database oid allocation based on issue #15468.
// meta-cache cannot handle table id resue.
// Using the old PG global oid allocation method, we can hit the following OID collision.
// Connections used in the example:
// Connection 1 on tserver 0 (Conn1) && Connection 2 on tserver 1 (Conn2)
// Example:
// Conn1: CREATE DATABASE db2;
// Conn1: CREATE TABLE tbl (k INT); -- tbl OID: 16385
// Conn2: \c db2
// Conn2: CREATE TYPE dummy;
// Conn2: \c yugabyte
// Conn2: SELECT COUNT(*) FROM tbl;
// Conn2: DROP TABLE tbl;
// -- danger OID: 16385 (same as table id of deleted table tbl)
// Conn2: CREATE TABLE danger (k INT, v INT);
// Conn2: INSERT INTO danger SELECT i, i from generate_series(1, 100) i;
TEST_P(PgOidCollisionTest, MetaCachePgOidCollisionFromTservers) {
  const bool ysql_enable_pg_per_database_oid_allocator = GetParam();
  RestartClusterWithOidAllocator(ysql_enable_pg_per_database_oid_allocator);
  const string dbname = "db2";
  // Tserver 0
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0", dbname));
  ASSERT_OK(conn1.Execute("CREATE TABLE tbl (k INT)"));
  // Tserver 1
  LOG(INFO) << "Make a new connection to a different node at index 1";
  pg_ts = cluster_->tablet_server(1);
  auto conn2 = ASSERT_RESULT(ConnectToDB(dbname));
  ASSERT_OK(conn2.Execute("CREATE TYPE dummy"));
  conn2 = ASSERT_RESULT(ConnectToDB("yugabyte"));
  ASSERT_OK(conn2.Fetch("SELECT COUNT(*) FROM tbl"));
  ASSERT_OK(conn2.Execute("DROP TABLE tbl"));
  ASSERT_OK(conn2.Execute("CREATE TABLE danger (k INT, v INT)"));
  auto status = conn2.Execute("INSERT INTO danger SELECT i, i from generate_series(1, 100) i");
  if (ysql_enable_pg_per_database_oid_allocator) {
    ASSERT_OK(status);
    // Verify
    ASSERT_EQ(ASSERT_RESULT(conn1.FetchRow<PGUint64>("SELECT COUNT(*) FROM danger")), 100);
  } else {
    ASSERT_TRUE(status.IsNetworkError()) << status;
    ASSERT_STR_CONTAINS(status.ToString(), "Tablet deleted:");
  }
}

class PgOidCollisionCreateDatabaseTest
    : public PgOidCollisionTestBase,
    public ::testing::WithParamInterface<std::pair<bool, bool>> {
};

INSTANTIATE_TEST_CASE_P(PgOidCollisionCreateDatabaseTest,
                        PgOidCollisionCreateDatabaseTest,
                        ::testing::Values(std::make_pair(false, false),
                                          std::make_pair(false, true),
                                          std::make_pair(true, false),
                                          std::make_pair(true, true)));

// Test case for PG per-database oid allocation.
// Using the old PG global oid allocation method, we can hit the following OID collision.
// for PG CREATE DATABASE.
// Connections used in the example:
// Connection 1 on tserver 0 (Conn1) && Connection 2 on tserver 1 (Conn2)
// Example:
// Conn1: CREATE DATABASE db1; -- db1 OID: 16384; OID range: [16384, 16640) on tserver 0
// -- db2 OID: 16385; db2 is used to trigger the same range of OID allocation on tserver 1
// Conn1: CREATE DATABASE db2;
// Conn1: DROP DATABASE db1;
// Conn2: \c db2
// Conn2: CREATE DATABASE db3; -- db3 OID: 16384; OID range: [16384, 16640) on tserver 1
// ERROR:  Keyspace 'db3' already exists
// Using a per-database PG oid allocator, or retry CREATE DATABASE on oid
// collision, the CREATE DATABASE OID collision issue can be solved.
TEST_P(PgOidCollisionCreateDatabaseTest, CreateDatabasePgOidCollisionFromTservers) {
  const bool ysql_enable_pg_per_database_oid_allocator = GetParam().first;
  ysql_enable_create_database_oid_collision_retry = GetParam().second;
  RestartClusterWithOidAllocator(ysql_enable_pg_per_database_oid_allocator);
  // Verify the keyspace already exists issue still doesn't exist if we disable retry CREATE
  // DATABASE because we use new PG per-database oid allocation.
  const string db1 = "db1";
  const string db2 = "db2";
  const string db3 = "db3";

  // Tserver 0
  // if ysql_enable_pg_per_database_oid_allocator=true
  //   template1's range [16384, 16640) is allocated on tserver 0
  // else
  //   yugabyte's range [16384, 16640) is allocated on tserver 0
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0", db1));
  ASSERT_OK(conn1.ExecuteFormat("CREATE DATABASE $0", db2));
  ASSERT_OK(conn1.ExecuteFormat("DROP DATABASE $0", db1));

  // Tserver 1
  // if ysql_enable_pg_per_database_oid_allocator=true
  //   template1's range [16640, 16896) is allocated on tserver 1
  // else
  //   db2's range [16384, 16640) is allocated on tserver 1
  LOG(INFO) << "Make a new connection to a different node at index 1";
  pg_ts = cluster_->tablet_server(1);
  auto conn2 = ASSERT_RESULT(ConnectToDB(db2));
  auto status = conn2.ExecuteFormat("CREATE DATABASE $0", db3);
  if (ysql_enable_pg_per_database_oid_allocator) {
    ASSERT_OK(status);
    // Verify no OID collision and the expected OID is used.
    int db3_oid = ASSERT_RESULT(conn2.FetchRow<int32_t>(
        Format("SELECT oid FROM pg_database WHERE datname = \'$0\'", db3)));
    ASSERT_EQ(db3_oid, 16640);
  } else if (ysql_enable_create_database_oid_collision_retry) {
    // Verify internally retry CREATE DATABASE works.
    int db3_oid = ASSERT_RESULT(conn2.FetchRow<int32_t>(
        Format("SELECT oid FROM pg_database WHERE datname = \'$0\'", db3)));
    ASSERT_EQ(db3_oid, 16386);
  } else {
    // Verify the keyspace already exists issue still exists if we disable retry.
    // Creation of db3 on Tserver 1 uses 16384 as the next available oid.
    ASSERT_TRUE(status.IsNetworkError()) << status;
    ASSERT_STR_CONTAINS(status.ToString(), "Keyspace with id");
    ASSERT_STR_CONTAINS(status.ToString(), "already exists");
  }
}

// This test shows interop between using the old PG allocator and new PG
// allocator.
TEST_P(PgOidCollisionTest, TablespaceOidCollision) {
  const bool ysql_enable_pg_per_database_oid_allocator = GetParam();
  // Restart the cluster using the specified PG OID allocator.
  RestartClusterWithOidAllocator(ysql_enable_pg_per_database_oid_allocator);
  auto conn = ASSERT_RESULT(Connect());
  const int32_t num_system_tablespaces = 2; // pg_default and pg_global
  const int32_t num_tablespaces = 512; // two allocation chunks of OIDs.
  // Create some tablespaces using the specified PG OID allocator.
  for (int i = 0; i < num_tablespaces; i++) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLESPACE tp$0 LOCATION '/data'", i));
  }
  const string shared_pg_table = "pg_tablespace";
  const string count_oid_query =
    Format("SELECT count(oid) FROM $0", shared_pg_table);
  const string max_oid_query =
    Format("SELECT max(oid) FROM $0", shared_pg_table);
  auto count_oid = ASSERT_RESULT(conn.FetchRow<int64_t>(count_oid_query));
  auto max_oid = ASSERT_RESULT(conn.FetchRow<PGOid>(max_oid_query));
  ASSERT_EQ(count_oid, num_tablespaces + num_system_tablespaces);
  ASSERT_EQ(max_oid, kPgFirstNormalObjectId + num_tablespaces - 1);

  // Restart the cluster using the opposite PG OID allocator.
  RestartClusterWithOidAllocator(!ysql_enable_pg_per_database_oid_allocator);
  conn = ASSERT_RESULT(Connect());
  // Create some more tablespaces using the opposite PG OID allocator.
  // We should not see any OID collision. The PG function DoesOidExistInRelation
  // should keep generate new OID until we find one not in the shared table.
  for (int i = num_tablespaces; i < num_tablespaces * 2; i++) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLESPACE tp$0 LOCATION '/data'", i));
  }
  count_oid = ASSERT_RESULT(conn.FetchRow<int64_t>(count_oid_query));
  max_oid = ASSERT_RESULT(conn.FetchRow<PGOid>(max_oid_query));
  ASSERT_EQ(count_oid, num_tablespaces * 2 + num_system_tablespaces);
  ASSERT_EQ(max_oid, kPgFirstNormalObjectId + num_tablespaces * 2 - 1);
}

class PgLibPqTempTest: public PgLibPqTest {
 public:
  Status TestDeletedByQuery(
      const std::vector<string>& relnames, const string& query, PGConn* conn) {
    std::vector<string> filepaths;
    filepaths.reserve(relnames.size());
    for (const string& relname : relnames) {
      string pg_filepath = VERIFY_RESULT(
          conn->FetchRow<std::string>(Format("SELECT pg_relation_filepath('$0')", relname)));
      filepaths.push_back(JoinPathSegments(pg_ts->GetRootDir(), "pg_data", pg_filepath));
    }
    for (const string& filepath : filepaths) {
      SCHECK(Env::Default()->FileExists(filepath), IllegalState,
             Format("File $0 should exist, but does not", filepath));
    }
    RETURN_NOT_OK(conn->Execute(query));
    for (const string& filepath : filepaths) {
      SCHECK(!Env::Default()->FileExists(filepath), IllegalState,
             Format("File $0 should not exist, but does", filepath));
    }
    return Status::OK();
  }

  Status TestRemoveTempTable(bool is_drop) {
    PGConn conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("CREATE TEMP TABLE foo (k INT PRIMARY KEY, v INT)"));
    const string schema = VERIFY_RESULT(GetSchemaName("foo", &conn));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE $0.bar (k INT PRIMARY KEY, v INT)", schema));

    string command = is_drop ? "DROP TABLE foo, bar" : "DISCARD TEMP";
    RETURN_NOT_OK(TestDeletedByQuery({"foo", "foo_pkey", "bar", "bar_pkey"}, command, &conn));
    return Status::OK();
  }

  Status TestRemoveTempSequence(bool is_drop) {
    PGConn conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("CREATE TEMP SEQUENCE foo"));
    const string schema = VERIFY_RESULT(GetSchemaName("foo", &conn));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE SEQUENCE $0.bar ", schema));

    if (is_drop) {
      // TODO(#880): use single call to drop both sequences at the same time.
      RETURN_NOT_OK(TestDeletedByQuery({"foo"}, "DROP SEQUENCE foo", &conn));
      RETURN_NOT_OK(TestDeletedByQuery({"bar"}, "DROP SEQUENCE bar", &conn));
    } else {
      RETURN_NOT_OK(TestDeletedByQuery({"foo", "bar"}, "DISCARD TEMP", &conn));
    }
    return Status::OK();
  }
};

// Test that the physical storage for a temporary sequence is removed
// when calling DROP SEQUENCE.
TEST_F(PgLibPqTempTest, DropTempSequence) {
  ASSERT_OK(TestRemoveTempSequence(true));
}

// Test that the physical storage for a temporary sequence is removed
// when calling DISCARD TEMP.
TEST_F(PgLibPqTempTest, DiscardTempSequence) {
  ASSERT_OK(TestRemoveTempSequence(false));
}

// Test that the physical storage for a temporary table and index are removed
// when calling DROP TABLE.
TEST_F(PgLibPqTempTest, DropTempTable) {
  ASSERT_OK(TestRemoveTempTable(true));
}

// Test that the physical storage for a temporary table and index are removed
// when calling DISCARD TEMP.
TEST_F(PgLibPqTempTest, DiscardTempTable) {
  ASSERT_OK(TestRemoveTempTable(false));
}

// Drop Sequence test.
TEST_F(PgLibPqTest, DropSequenceTest) {
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE SEQUENCE foo"));

  // Verify that if DROP SEQUENCE fails, the sequence is actually not
  // dropped.
  ASSERT_OK(conn.Execute("SET yb_test_fail_next_ddl=true"));
  ASSERT_NOK(conn.Execute("DROP SEQUENCE foo"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT nextval('foo')")), 1);

  // Verify same behavior for sequences created using CREATE TABLE.
  ASSERT_OK(conn.Execute("CREATE TABLE t (k SERIAL)"));
  ASSERT_OK(conn.Execute("SET yb_test_fail_next_ddl=true"));
  ASSERT_NOK(conn.Execute("DROP SEQUENCE t_k_seq CASCADE"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT nextval('t_k_seq')")), 1);

  // Verify same behavior is seen while trying to drop the table.
  ASSERT_OK(conn.Execute("SET yb_test_fail_next_ddl=true"));
  ASSERT_NOK(conn.Execute("DROP TABLE t"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT nextval('t_k_seq')")), 2);

  // Verify that if DROP SEQUENCE is successful, we cannot query the sequence
  // anymore.
  ASSERT_OK(conn.Execute("DROP SEQUENCE foo"));
  ASSERT_NOK(conn.FetchRow<int64_t>("SELECT nextval('foo')"));
}

TEST_F(PgLibPqTest, TempTableViewFileCountTest) {
  const std::string kTableName = "foo";
  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE TEMP TABLE $0 (k INT)", kTableName));

  // Check that only one file is present in this database and that corresponds to temp table foo.
  auto query = Format(
      "SELECT pg_ls_dir('$0/pg_data/' || substring(pg_relation_filepath('$1') from '.*/')) = 't1_' "
      "|| '$1'::regclass::oid::text;",
      pg_ts->GetRootDir(), kTableName);
  auto values = ASSERT_RESULT(conn.FetchRows<bool>(query));
  ASSERT_EQ(values, decltype(values){true});

  ASSERT_OK(conn.ExecuteFormat("CREATE VIEW tempview AS SELECT * FROM $0", kTableName));

  // Check that no new files are created on view creation.
  values = ASSERT_RESULT(conn.FetchRows<bool>(query));
  ASSERT_EQ(values, decltype(values){true});
}

} // namespace pgwrapper
} // namespace yb

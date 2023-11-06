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

#include <fstream>
#include <thread>

#include "yb/client/client_fwd.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/schema.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"

#include "yb/util/async_util.h"
#include "yb/util/barrier.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/path_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

DEFINE_int32(num_iter, 10000, "Number of iterations to run StaleMasterReads test");

DECLARE_int64(external_mini_cluster_max_log_bytes);

METRIC_DECLARE_entity(tablet);
METRIC_DECLARE_counter(transaction_not_found);

METRIC_DECLARE_entity(server);
METRIC_DECLARE_counter(rpc_inbound_calls_created);

namespace yb {
namespace pgwrapper {

using master::GetColocatedDbParentTableId;
using master::GetTablegroupParentTableId;

class PgLibPqTest : public LibPqTestBase {
 protected:
  void TestUriAuth();

  void TestMultiBankAccount(IsolationLevel isolation);

  void DoIncrement(int key, int num_increments, IsolationLevel isolation);

  void TestParallelCounter(IsolationLevel isolation);

  void TestConcurrentCounter(IsolationLevel isolation);

  void TestOnConflict(bool kill_master, const MonoDelta& duration);

  void TestCacheRefreshRetry(const bool is_retry_disabled);

  const std::vector<std::string> names{
    "uppercase:P",
    "space: ",
    "symbol:#",
    "single_quote:'",
    "double_quote:\"",
    "backslash:\\",
    "mixed:P #'\"\\",
  };
};

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(Simple)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));

  auto res = ASSERT_RESULT(conn.Fetch("SELECT * FROM t"));

  {
    auto lines = PQntuples(res.get());
    ASSERT_EQ(1, lines);

    auto columns = PQnfields(res.get());
    ASSERT_EQ(2, columns);

    auto key = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(key, 1);
    auto value = ASSERT_RESULT(GetString(res.get(), 0, 1));
    ASSERT_EQ(value, "hello");
  }
}

// Test libpq connection to various database names.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(DatabaseNames)) {
  PGConn conn = ASSERT_RESULT(Connect());

  for (const std::string& db_name : names) {
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", PqEscapeIdentifier(db_name)));
    ASSERT_OK(ConnectToDB(db_name));
  }
}

// Test libpq connection to various user names.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(UserNames)) {
  PGConn conn = ASSERT_RESULT(Connect());

  for (const std::string& user_name : names) {
    ASSERT_OK(conn.ExecuteFormat("CREATE USER $0", PqEscapeIdentifier(user_name)));
    ASSERT_OK(ConnectToDBAsUser("" /* db_name */, user_name));
  }
}

// Test libpq connection using URI connection string.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(Uri)) {
  const std::string& host = pg_ts->bind_host();
  const uint16_t port = pg_ts->pgsql_rpc_port();
  {
    const std::string& conn_str = Format("postgres://yugabyte@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    PGConn conn = ASSERT_RESULT(ConnectUsingString(conn_str));
    {
      auto res = ASSERT_RESULT(conn.Fetch("select current_database()"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, "yugabyte");
    }
    {
      auto res = ASSERT_RESULT(conn.Fetch("select current_user"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, "yugabyte");
    }
    {
      auto res = ASSERT_RESULT(conn.Fetch("show listen_addresses"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, host);
    }
    {
      auto res = ASSERT_RESULT(conn.Fetch("show port"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, std::to_string(port));
    }
  }
  // Supply database name.
  {
    const std::string& conn_str = Format("postgres://yugabyte@$0:$1/template1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    PGConn conn = ASSERT_RESULT(ConnectUsingString(conn_str));
    {
      auto res = ASSERT_RESULT(conn.Fetch("select current_database()"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, "template1");
    }
  }
  // Supply an incorrect password.  Since HBA config gives the yugabyte user trust access, postgres
  // won't request a password, our client won't send this password, and the authentication should
  // succeed.
  {
    const std::string& conn_str = Format("postgres://yugabyte:monkey123@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    ASSERT_OK(ConnectUsingString(conn_str));
  }
}

void PgLibPqTest::TestUriAuth() {
  const std::string& host = pg_ts->bind_host();
  const uint16_t port = pg_ts->pgsql_rpc_port();
  // Don't supply password.
  {
    const std::string& conn_str = Format("postgres://yugabyte@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    Result<PGConn> result = ConnectUsingString(
        conn_str,
        CoarseMonoClock::Now() + 2s /* deadline */);
    ASSERT_NOK(result);
    ASSERT_TRUE(result.status().IsNetworkError());
    ASSERT_TRUE(result.status().message().ToBuffer().find("Connect failed") != std::string::npos)
        << result.status();
  }
  // Supply an incorrect password.
  {
    const std::string& conn_str = Format("postgres://yugabyte:monkey123@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    Result<PGConn> result = ConnectUsingString(
        conn_str,
        CoarseMonoClock::Now() + 2s /* deadline */);
    ASSERT_NOK(result);
    ASSERT_TRUE(result.status().IsNetworkError());
    ASSERT_TRUE(result.status().message().ToBuffer().find("Connect failed") != std::string::npos)
        << result.status();
  }
  // Supply the correct password.
  {
    const std::string& conn_str = Format("postgres://yugabyte:yugabyte@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    ASSERT_OK(ConnectUsingString(conn_str));
  }
}

// Enable authentication using password.  This scheme requests the plain password.  You may still
// use SSL for encryption on the wire.
class PgLibPqTestAuthPassword : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--ysql_hba_conf_csv=host all all samehost password");
  }
};

TEST_F_EX(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(UriPassword), PgLibPqTestAuthPassword) {
  TestUriAuth();
}

// Enable authentication using md5.  This scheme is a challenge and response, so the plain password
// isn't sent.
class PgLibPqTestAuthMd5 : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--ysql_hba_conf_csv=host all all samehost md5");
  }
};

TEST_F_EX(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(UriMd5), PgLibPqTestAuthMd5) {
  TestUriAuth();
}

// Test that repeats example from this article:
// https://blogs.msdn.microsoft.com/craigfr/2007/05/16/serializable-vs-snapshot-isolation-level/
//
// Multiple rows with values 0 and 1 are stored in table.
// Two concurrent transaction fetches all rows from table and does the following.
// First transaction changes value of all rows with value 0 to 1.
// Second transaction changes value of all rows with value 1 to 0.
// As outcome we should have rows with the same value.
//
// The described prodecure is repeated multiple times to increase probability of catching bug,
// w/o running test multiple times.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(SerializableColoring)) {
  constexpr auto kKeys = RegularBuildVsSanitizers(10, 20);
  constexpr auto kColors = 2;
  constexpr auto kIterations = 20;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, color INT)"));

  auto iterations_left = kIterations;

  for (int iteration = 0; iterations_left > 0; ++iteration) {
    auto iteration_title = Format("Iteration: $0", iteration);
    SCOPED_TRACE(iteration_title);
    LOG(INFO) << iteration_title;

    auto s = conn.Execute("DELETE FROM t");
    if (!s.ok()) {
      ASSERT_TRUE(HasTryAgain(s)) << s;
      continue;
    }
    for (int k = 0; k != kKeys; ++k) {
      int32_t color = RandomUniformInt(0, kColors - 1);
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, color) VALUES ($0, $1)", k, color));
    }

    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    for (int i = 0; i != kColors; ++i) {
      int32_t color = i;
      threads.emplace_back([this, color, kKeys, &complete] {
        auto connection = ASSERT_RESULT(Connect());

        ASSERT_OK(connection.Execute("BEGIN"));
        ASSERT_OK(connection.Execute("SET TRANSACTION ISOLATION LEVEL SERIALIZABLE"));

        auto res = connection.Fetch("SELECT * FROM t");
        if (!res.ok()) {
          ASSERT_TRUE(HasTryAgain(res.status())) << res.status();
          return;
        }
        auto columns = PQnfields(res->get());
        ASSERT_EQ(2, columns);

        auto lines = PQntuples(res->get());
        ASSERT_EQ(kKeys, lines);
        for (int j = 0; j != lines; ++j) {
          if (ASSERT_RESULT(GetInt32(res->get(), j, 1)) == color) {
            continue;
          }

          auto key = ASSERT_RESULT(GetInt32(res->get(), j, 0));
          auto status = connection.ExecuteFormat(
              "UPDATE t SET color = $1 WHERE key = $0", key, color);
          if (!status.ok()) {
            auto msg = status.message().ToBuffer();
            // Missing metadata means that transaction was aborted and cleaned.
            ASSERT_TRUE(HasTryAgain(status) ||
                        msg.find("Missing metadata") != std::string::npos) << status;
            break;
          }
        }

        auto status = connection.Execute("COMMIT");
        if (!status.ok()) {
          auto msg = status.message().ToBuffer();
          ASSERT_TRUE(msg.find("Operation expired") != std::string::npos) << status;
          return;
        }

        ++complete;
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }

    if (complete == 0) {
      continue;
    }

    auto res = ASSERT_RESULT(conn.Fetch("SELECT * FROM t"));
    auto columns = PQnfields(res.get());
    ASSERT_EQ(2, columns);

    auto lines = PQntuples(res.get());
    ASSERT_EQ(kKeys, lines);

    std::vector<int32_t> zeroes, ones;
    for (int i = 0; i != lines; ++i) {
      auto key = ASSERT_RESULT(GetInt32(res.get(), i, 0));
      auto current = ASSERT_RESULT(GetInt32(res.get(), i, 1));
      if (current == 0) {
        zeroes.push_back(key);
      } else {
        ones.push_back(key);
      }
    }

    std::sort(ones.begin(), ones.end());
    std::sort(zeroes.begin(), zeroes.end());

    LOG(INFO) << "Zeroes: " << yb::ToString(zeroes) << ", ones: " << yb::ToString(ones);
    ASSERT_TRUE(zeroes.empty() || ones.empty());

    --iterations_left;
  }
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(SerializableReadWriteConflict)) {
  const auto kKeys = RegularBuildVsSanitizers(20, 5);
  const auto kNumTries = RegularBuildVsSanitizers(4, 1);
  auto tries = 1;
  for (; tries <= kNumTries; ++tries) {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS t"));
    ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));

    size_t reads_won = 0, writes_won = 0;
    for (int i = 0; i != kKeys; ++i) {
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

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ReadRestart)) {
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

    auto res = ASSERT_RESULT(conn.FetchFormat("SELECT * FROM t WHERE key = $0", read_key));
    auto columns = PQnfields(res.get());
    ASSERT_EQ(1, columns);

    auto lines = PQntuples(res.get());
    ASSERT_EQ(1, lines);

    auto key = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(key, read_key);

    ASSERT_OK(conn.Execute("ROLLBACK"));
  }

  ASSERT_GE(last_written.load(std::memory_order_acquire), 100);
}

// Concurrently insert records into tables with foreign key relationship while truncating both.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ConcurrentInsertTruncateForeignKey)) {
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
      auto truncate_conn = ASSERT_RESULT(Connect());
      int idx = 0;
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
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ConcurrentIndexInsert)) {
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

void PgLibPqTest::TestMultiBankAccount(IsolationLevel isolation) {
  constexpr int kAccounts = RegularBuildVsSanitizers(20, 10);
  constexpr int64_t kInitialBalance = 100;

#ifndef NDEBUG
  const auto kTimeout = 180s;
  constexpr int kThreads = RegularBuildVsSanitizers(12, 5);
#else
  const auto kTimeout = 60s;
  constexpr int kThreads = 5;
#endif

  PGConn conn = ASSERT_RESULT(Connect());
  std::vector<PGConn> thread_connections;
  for (int i = 0; i < kThreads; ++i) {
    thread_connections.push_back(ASSERT_RESULT(Connect()));
  }

  for (int i = 1; i <= kAccounts; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE account_$0 (id int, balance bigint, PRIMARY KEY(id))", i));
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

  thread_holder.AddThreadFunctor(
      [this, &counter, &reads, &writes, isolation, &stop_flag = thread_holder.stop_flag()]() {
    SetFlagOnExit set_flag_on_exit(&stop_flag);
    auto connection = ASSERT_RESULT(Connect());
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
      auto result = tserver->GetInt64Metric(
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
    options->extra_master_flags.push_back("--TEST_fail_initdb_after_snapshot_restore=true");
  }

  int GetNumMasters() const override {
    return 3;
  }
};

TEST_F(PgLibPqFailoverDuringInitDb, YB_DISABLE_TEST_IN_TSAN(CreateTable)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));

  auto res = ASSERT_RESULT(conn.Fetch("SELECT * FROM t"));
}

class PgLibPqSmallClockSkewTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Use small clock skew, to decrease number of read restarts.
    options->extra_tserver_flags.push_back("--max_clock_skew_usec=5000");
  }
};

TEST_F_EX(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(MultiBankAccountSnapshot),
          PgLibPqSmallClockSkewTest) {
  TestMultiBankAccount(IsolationLevel::SNAPSHOT_ISOLATION);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(MultiBankAccountSerializable)) {
  TestMultiBankAccount(IsolationLevel::SERIALIZABLE_ISOLATION);
}

void PgLibPqTest::DoIncrement(int key, int num_increments, IsolationLevel isolation) {
  auto conn = ASSERT_RESULT(Connect());

  // Perform increments
  int succeeded_incs = 0;
  while (succeeded_incs < num_increments) {
    ASSERT_OK(conn.StartTransaction(isolation));
    bool committed = false;
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
    auto res = ASSERT_RESULT(conn.FetchFormat("SELECT value FROM t WHERE key = $0", i));

    auto row_val = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(row_val, kIncrements);
  }
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestParallelCounterSerializable)) {
  TestParallelCounter(IsolationLevel::SERIALIZABLE_ISOLATION);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestParallelCounterRepeatableRead)) {
  TestParallelCounter(IsolationLevel::SNAPSHOT_ISOLATION);
}

void PgLibPqTest::TestConcurrentCounter(IsolationLevel isolation) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT, value INT)"));

  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (0, 0)"));

  const auto kThreads = RegularBuildVsSanitizers(3, 2);
  const auto kIncrements = RegularBuildVsSanitizers(100, 20);

  // Have each thread increment the same already-created counter
  std::vector<std::thread> threads;
  while (threads.size() != kThreads) {
    threads.emplace_back([this, isolation] {
      DoIncrement(0, kIncrements, isolation);
    });
  }

  // Wait for completion
  for (auto& thread : threads) {
    thread.join();
  }

  // Check that we incremented exactly the desired number of times
  auto res = ASSERT_RESULT(conn.Fetch("SELECT value FROM t WHERE key = 0"));

  auto row_val = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
  ASSERT_EQ(row_val, kThreads * kIncrements);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestConcurrentCounterSerializable)) {
  TestConcurrentCounter(IsolationLevel::SERIALIZABLE_ISOLATION);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestConcurrentCounterRepeatableRead)) {
  TestConcurrentCounter(IsolationLevel::SNAPSHOT_ISOLATION);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(SecondaryIndexInsertSelect)) {
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
          int read_a = ASSERT_RESULT(connection.FetchValue<int32_t>(
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

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(InTxnDelete)) {
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

namespace {

Result<string> GetNamespaceIdByNamespaceName(
    client::YBClient* client, const string& namespace_name) {
  const auto namespaces = VERIFY_RESULT(client->ListNamespaces(YQL_DATABASE_PGSQL));
  for (const auto& ns : namespaces) {
    if (ns.name() == namespace_name) {
      return ns.id();
    }
  }
  return STATUS(NotFound, "The namespace does not exist");
}

Result<string> GetTableIdByTableName(
    client::YBClient* client, const string& namespace_name, const string& table_name) {
  const auto tables = VERIFY_RESULT(client->ListTables());
  for (const auto& t : tables) {
    if (t.namespace_name() == namespace_name && t.table_name() == table_name) {
      return t.table_id();
    }
  }
  return STATUS(NotFound, "The table does not exist");
}

} // namespace

class PgLibPqReadFromSysCatalogTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgLibPqTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        "--TEST_get_ysql_catalog_version_from_sys_catalog=true");
  }
};

TEST_F_EX(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(StaleMasterReads), PgLibPqReadFromSysCatalogTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  uint64_t ver_orig;
  ASSERT_OK(client->GetYsqlCatalogMasterVersion(&ver_orig));
  for (int i = 1; i <= FLAGS_num_iter; i++) {
    LOG(INFO) << "ITERATION " << i;
    LOG(INFO) << "Creating user " << i;
    ASSERT_OK(conn.ExecuteFormat("CREATE USER user$0", i));
    LOG(INFO) << "Fetching CatalogVersion. Expecting " << i + ver_orig;
    uint64_t ver;
    ASSERT_OK(client->GetYsqlCatalogMasterVersion(&ver));
    ASSERT_EQ(ver_orig + i, ver);
  }
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(CompoundKeyColumnOrder)) {
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

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(BulkCopy)) {
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
    auto result = conn.FetchFormat("SELECT COUNT(*) FROM $0", kTableName);
    if (result.ok()) {
      LogResult(result->get());
      auto count = ASSERT_RESULT(GetInt64(result->get(), 0, 0));
      LOG(INFO) << "Total count: " << count;
      ASSERT_EQ(count, kNumBatches * kBatchSize);
      break;
    } else {
      auto message = result.status().ToString();
      ASSERT_TRUE(message.find("Snaphost too old") != std::string::npos) << result.status();
    }
  }
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(CatalogManagerMapsTest)) {
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

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TestSystemTableRollback)) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE pktable (ptest1 int PRIMARY KEY);"));
  Status s = conn1.Execute("CREATE TABLE fktable (ftest1 inet REFERENCES pktable);");
  LOG(INFO) << "Status of second table creation: " << s;
  auto res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM pg_class WHERE relname='fktable'"));
  ASSERT_EQ(0, PQntuples(res.get()));
}

namespace {

Result<master::TabletLocationsPB> GetColocatedTabletLocations(
    client::YBClient* client,
    std::string database_name,
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

  // Get TabletLocations for the tablegroup tablet.
  RETURN_NOT_OK(WaitFor(
      [&]() -> Result<bool> {
        Status s = client->GetTabletsFromTableId(
            GetTablegroupParentTableId(tablegroup_id),
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

} // namespace

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TableColocation)) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName = "test_db";
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_bar_index;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", kDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  // A parent table with one tablet should be created when the database is created.
  const auto colocated_tablet_locations = ASSERT_RESULT(GetColocatedTabletLocations(
      client.get(),
      kDatabaseName,
      30s));
  const auto colocated_tablet_id = colocated_tablet_locations.tablet_id();
  const auto colocated_table = ASSERT_RESULT(client->OpenTable(
      colocated_tablet_locations.table_id()));

  // Create a range partition table, the table should share the tablet with the parent table.
  ASSERT_OK(conn.Execute("CREATE TABLE foo (a INT, PRIMARY KEY (a ASC))"));
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Create a colocated index table.
  ASSERT_OK(conn.Execute("CREATE INDEX foo_index1 ON foo (a)"));
  table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo_index1"));
  ASSERT_OK(client->GetTabletsFromTableId(table_id, 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets[0].tablet_id(), colocated_tablet_id);

  // Create a hash partition table and opt out of using the parent tablet.
  ASSERT_OK(conn.Execute("CREATE TABLE bar (a INT) WITH (colocated = false)"));
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
  ASSERT_OK(conn.Execute("CREATE TABLE qux (a INT, PRIMARY KEY (a ASC)) WITH (colocated = true)"));
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
  ASSERT_OK(WaitFor(
      [&] {
        for (int i = 0; i < tablets_bar_index.size(); ++i) {
          client->LookupTabletById(
              tablets_bar_index[i].tablet_id(),
              table_bar_index,
              master::IncludeInactive::kFalse,
              CoarseMonoClock::Now() + 30s,
              [&, i](const Result<client::internal::RemoteTabletPtr>& result) {
                tablet_founds[i] = result.ok();
              },
              client::UseCache::kFalse);
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
  int rpc_calls = 0;
  ASSERT_OK(WaitFor(
      [&] {
        rpc_calls++;
        client->LookupTabletById(
            colocated_tablet_id,
            colocated_table,
              master::IncludeInactive::kFalse,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              tablet_found = result.ok();
              rpc_calls--;
            },
            client::UseCache::kFalse);
        return !tablet_found;
      },
      30s, "Drop colocated database"));
  // To prevent an "AddressSanitizer: stack-use-after-scope", do not return from this function until
  // all callbacks are done.
  ASSERT_OK(WaitFor(
      [&rpc_calls] {
        LOG(INFO) << "Waiting for " << rpc_calls << " RPCs to run callbacks";
        return rpc_calls == 0;
      },
      30s, "Drop colocated database (wait for RPCs to finish)"));
}

// Test for ensuring that transaction conflicts work as expected for colocated tables.
// Related to https://github.com/yugabyte/yugabyte-db/issues/3251.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(TxnConflictsForColocatedTables)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test_db WITH colocated = true"));

  auto conn1 = ASSERT_RESULT(ConnectToDB("test_db"));
  auto conn2 = ASSERT_RESULT(ConnectToDB("test_db"));

  ASSERT_OK(conn1.Execute("CREATE TABLE t (a INT, PRIMARY KEY (a ASC))"));
  ASSERT_OK(conn1.Execute("INSERT INTO t(a) VALUES(1)"));

  // From conn1, select the row in UPDATE row lock mode. From conn2, delete the row.
  // Ensure that conn1's transaction will detect a conflict at the time of commit.
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  auto res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t FOR UPDATE"));
  ASSERT_EQ(PQntuples(res.get()), 1);

  auto status = conn2.Execute("DELETE FROM t WHERE a = 1");
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(PgsqlError(status), YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "Conflicts with higher priority transaction");

  ASSERT_OK(conn1.CommitTransaction());

  // Ensure that reads to separate tables in a colocated database do not conflict.
  ASSERT_OK(conn1.Execute("CREATE TABLE t2 (a INT, PRIMARY KEY (a ASC))"));
  ASSERT_OK(conn1.Execute("INSERT INTO t2(a) VALUES(1)"));

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));

  res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t FOR UPDATE"));
  ASSERT_EQ(PQntuples(res.get()), 1);
  res = ASSERT_RESULT(conn2.Fetch("SELECT * FROM t2 FOR UPDATE"));
  ASSERT_EQ(PQntuples(res.get()), 1);

  ASSERT_OK(conn1.CommitTransaction());
  ASSERT_OK(conn2.CommitTransaction());
}

// Ensure tablet bootstrap doesn't crash when replaying change metadata operations
// for a deleted colocated table. This is a regression test for #6096.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ReplayDeletedTableInColocatedDB)) {
  const std::string kDatabaseName = "testdb";
  constexpr int kTimeoutSecs = 30;
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  PGConn conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 with colocated=true", kDatabaseName));

  PGConn conn_new = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn_new.Execute("CREATE TABLE foo (i int)"));
  ASSERT_OK(conn_new.Execute("INSERT INTO foo VALUES (10)"));

  // Flush tablets; requests from here on will be replayed from the WAL during bootstrap.
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, "foo"));
  ASSERT_OK(client->FlushTables(
      {table_id},
      false /* add_indexes */,
      kTimeoutSecs,
      false /* is_compaction */));

  // ALTER requires foo's table id to be in the TS raft metadata
  ASSERT_OK(conn_new.Execute("ALTER TABLE foo ADD c char"));
  ASSERT_OK(conn_new.Execute("ALTER TABLE foo RENAME COLUMN c to d"));
  // but DROP will remove foo's table id from the TS raft metadata
  ASSERT_OK(conn_new.Execute("DROP TABLE foo"));
  ASSERT_OK(conn_new.Execute("CREATE TABLE bar (c char)"));

  // Restart a TS that serves this tablet so we do a local bootstrap and replay WAL files.
  // Ensure we don't crash here due to missing table info in metadata when replaying the ALTER.
  ASSERT_NO_FATALS(cluster_->tablet_server(0)->Shutdown());

  LOG(INFO) << "Start tserver";
  ASSERT_OK(cluster_->tablet_server(0)->Restart());
  ASSERT_OK(cluster_->WaitForTabletsRunning(cluster_->tablet_server(0),
      MonoDelta::FromSeconds(60)));

  // Ensure the rest of the WAL replayed successfully.
  PGConn conn_after = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  auto res = ASSERT_RESULT(conn_after.FetchValue<int64_t>("SELECT COUNT(*) FROM bar"));
  ASSERT_EQ(res, 0);
}

class PgLibPqTablegroupTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Enable tablegroup beta feature
    options->extra_tserver_flags.push_back("--ysql_beta_feature_tablegroup=true");
    options->extra_master_flags.push_back("--ysql_beta_feature_tablegroup=true");
  }
};

namespace {

struct TableGroupInfo {
  int oid;
  std::string id;
  TabletId tablet_id;
  std::shared_ptr<client::YBTable> table;
};

Result<TableGroupInfo> SelectTablegroup(
    client::YBClient* client, PGConn* conn, const std::string& database_name,
    const std::string& group_name) {
  TableGroupInfo group_info;
  auto res = VERIFY_RESULT(
      conn->FetchFormat("SELECT oid FROM pg_database WHERE datname=\'$0\'", database_name));
  const int database_oid = VERIFY_RESULT(GetInt32(res.get(), 0, 0));
  res = VERIFY_RESULT(
      conn->FetchFormat("SELECT oid FROM pg_yb_tablegroup WHERE grpname=\'$0\'", group_name));
  group_info.oid = VERIFY_RESULT(GetInt32(res.get(), 0, 0));

  group_info.id = GetPgsqlTablegroupId(database_oid, group_info.oid);
  group_info.tablet_id = VERIFY_RESULT(GetTablegroupTabletLocations(
      client,
      database_name,
      group_info.id,
      30s))
    .tablet_id();
  group_info.table = VERIFY_RESULT(client->OpenTable(GetTablegroupParentTableId(group_info.id)));
  SCHECK(VERIFY_RESULT(client->TablegroupExists(database_name, group_info.id)),
         InternalError,
         "YBClient::TablegroupExists couldn't find a tablegroup!");
  return group_info;
}

} // namespace

TEST_F_EX(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ColocatedTablegroups),
          PgLibPqTablegroupTest) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const string kDatabaseName ="tgroup_test_db";
  const string kTablegroupName ="test_tgroup";
  const string kTablegroupAltName ="test_alt_tgroup";
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_bar_index;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupName));

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
  ASSERT_OK(WaitFor(
      [&] {
        for (int i = 0; i < tablets_bar_index.size(); ++i) {
          client->LookupTabletById(
              tablets_bar_index[i].tablet_id(),
              table_bar_index,
              master::IncludeInactive::kFalse,
              CoarseMonoClock::Now() + 30s,
              [&, i](const Result<client::internal::RemoteTabletPtr>& result) {
                tablet_founds[i] = result.ok();
              },
              client::UseCache::kFalse);
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
  int rpc_calls = 0;
  ASSERT_OK(WaitFor(
      [&] {
        rpc_calls++;
        client->LookupTabletById(
            tablegroup_alt.tablet_id,
            tablegroup_alt.table,
            master::IncludeInactive::kFalse,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              alt_tablet_found = result.ok();
              rpc_calls--;
            },
            client::UseCache::kFalse);
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
  ASSERT_OK(WaitFor(
      [&] {
        rpc_calls++;
        client->LookupTabletById(
            tablegroup.tablet_id,
            tablegroup.table,
            master::IncludeInactive::kFalse,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              orig_tablet_found = result.ok();
              rpc_calls--;
            },
            client::UseCache::kFalse);
        return !orig_tablet_found;
      },
      30s, "Drop database with tablegroup"));

  // The second tablegroup tablet should also be deleted after dropping the database.
  bool second_tablet_found = true;
  ASSERT_OK(WaitFor(
      [&] {
        rpc_calls++;
        client->LookupTabletById(
            tablegroup_alt.tablet_id,
            tablegroup_alt.table,
            master::IncludeInactive::kFalse,
            CoarseMonoClock::Now() + 30s,
            [&](const Result<client::internal::RemoteTabletPtr>& result) {
              second_tablet_found = result.ok();
              rpc_calls--;
            },
            client::UseCache::kFalse);
        return !second_tablet_found;
      },
      30s, "Drop database with tablegroup"));

  // To prevent an "AddressSanitizer: stack-use-after-scope", do not return from this function until
  // all callbacks are done.
  ASSERT_OK(WaitFor(
      [&rpc_calls] {
        LOG(INFO) << "Waiting for " << rpc_calls << " RPCs to run callbacks";
        return rpc_calls == 0;
      },
      30s, "Drop database with tablegroup (wait for RPCs to finish)"));
}

namespace {

class PgLibPqTestRF1: public PgLibPqTest {
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
TEST_F_EX(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(NumberOfInitialRpcs), PgLibPqTestRF1) {
  auto get_master_inbound_rpcs_created = [this]() -> Result<int64_t> {
    int64_t m_in_created = 0;
    for (const auto* master : this->cluster_->master_daemons()) {
      m_in_created += VERIFY_RESULT(master->GetInt64Metric(
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

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(RangePresplit)) {
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
    options->extra_master_flags.push_back("--tserver_unresponsive_timeout_ms=8000");
    options->extra_master_flags.push_back("--unresponsive_ts_rpc_timeout_ms=10000");
    options->extra_tserver_flags.push_back("--follower_unavailable_considered_failed_sec=8");
  }
};

// Test that adding a tserver and removing a tserver causes the colocation tablet to adjust raft
// configuration off the old tserver and onto the new tserver.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(LoadBalanceSingleColocatedDB),
          PgLibPqTestSmallTSTimeout) {
  const std::string kDatabaseName = "co";
  const auto kTimeout = 60s;
  const auto starting_num_tablet_servers = cluster_->num_tablet_servers();
  ExternalMiniClusterOptions opts;
  std::map<std::string, int> ts_loads;
  static const int tserver_unresponsive_timeout_ms = 8000;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", kDatabaseName));

  // Collect colocation tablet replica locations.
  {
    master::TabletLocationsPB tablet_locations = ASSERT_RESULT(GetColocatedTabletLocations(
        client.get(),
        kDatabaseName,
        kTimeout));
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
  }

  // Ensure each tserver has exactly one colocation tablet replica.
  ASSERT_EQ(ts_loads.size(), starting_num_tablet_servers);
  for (const auto& entry : ts_loads) {
    ASSERT_NOTNULL(cluster_->tablet_server_by_uuid(entry.first));
    ASSERT_EQ(entry.second, 1);
    LOG(INFO) << "found ts " << entry.first << " has " << entry.second << " replicas";
  }

  // Add a tablet server.
  UpdateMiniClusterOptions(&opts);
  ASSERT_OK(cluster_->AddTabletServer(ExternalMiniClusterOptions::kDefaultStartCqlProxy,
                                      opts.extra_tserver_flags));
  ASSERT_OK(cluster_->WaitForTabletServerCount(starting_num_tablet_servers + 1, kTimeout));

  // Wait for load balancing.  This should move some tablet-peers (e.g. of the colocation tablet,
  // system.transactions tablets) to the new tserver.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        bool is_idle = VERIFY_RESULT(client->IsLoadBalancerIdle());
        return !is_idle;
      },
      kTimeout,
      "wait for load balancer to be active"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return client->IsLoadBalancerIdle();
      },
      kTimeout,
      "wait for load balancer to be idle"));

  // Remove a tablet server.
  cluster_->tablet_server(0)->Shutdown();

  // Wait for the master leader to mark it dead.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return cluster_->is_ts_stale(0);
  },
  MonoDelta::FromMilliseconds(2 * tserver_unresponsive_timeout_ms),
  "Is TS dead",
  MonoDelta::FromSeconds(1)));

  // Collect colocation tablet replica locations and verify that load has been moved off
  // from the dead TS.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    master::TabletLocationsPB tablet_locations = VERIFY_RESULT(GetColocatedTabletLocations(
        client.get(),
        kDatabaseName,
        kTimeout));
    ts_loads.clear();
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
    // Ensure each colocation tablet replica is on the three tablet servers excluding the first one,
    // which is shut down.
    if (ts_loads.size() != starting_num_tablet_servers) {
      return false;
    }
    for (const auto& entry : ts_loads) {
      ExternalTabletServer* ts = cluster_->tablet_server_by_uuid(entry.first);
      if (ts == nullptr || ts == cluster_->tablet_server(0) || entry.second != 1) {
        return false;
      }
    }
    return true;
  },
  kTimeout,
  "Wait for load to be moved off from tserver 0"));
}

// Test that adding a tserver causes colocation tablets to offload tablet-peers to the new tserver.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(LoadBalanceMultipleColocatedDB)) {
  constexpr int kNumDatabases = 3;
  const auto kTimeout = 60s;
  const size_t starting_num_tablet_servers = cluster_->num_tablet_servers();
  const std::string kDatabasePrefix = "co";
  std::map<std::string, int> ts_loads;

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto conn = ASSERT_RESULT(Connect());

  for (int i = 0; i < kNumDatabases; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0$1 WITH colocated = true", kDatabasePrefix, i));
  }

  // Add a tablet server.
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(starting_num_tablet_servers + 1, kTimeout));

  // Wait for load balancing.  This should move some tablet-peers (e.g. of the colocation tablets,
  // system.transactions tablets) to the new tserver.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        bool is_idle = VERIFY_RESULT(client->IsLoadBalancerIdle());
        return !is_idle;
      },
      kTimeout,
      "wait for load balancer to be active"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return client->IsLoadBalancerIdle();
      },
      kTimeout,
      "wait for load balancer to be idle"));

  // Collect colocation tablets' replica locations.
  for (int i = 0; i < kNumDatabases; ++i) {
    master::TabletLocationsPB tablet_locations = ASSERT_RESULT(GetColocatedTabletLocations(
        client.get(),
        Format("$0$1", kDatabasePrefix, i),
        kTimeout));
    for (const auto& replica : tablet_locations.replicas()) {
      ts_loads[replica.ts_info().permanent_uuid()]++;
    }
  }

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
          YB_DISABLE_TEST_IN_TSAN(CacheRefreshRetryDisabled),
          PgLibPqTestNoRetry) {
  TestCacheRefreshRetry(true /* is_retry_disabled */);
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(CacheRefreshRetryEnabled)) {
  TestCacheRefreshRetry(false /* is_retry_disabled */);
}

class PgLibPqDatabaseTimeoutTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--TEST_user_ddl_operation_timeout_sec=1");
    options->extra_master_flags.push_back("--ysql_transaction_bg_task_wait_ms=5000");
  }
};

TEST_F(PgLibPqDatabaseTimeoutTest, YB_DISABLE_TEST_IN_TSAN(TestDatabaseTimeoutGC)) {
  NamespaceName test_name = "test_pgsql";
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Create Database: will timeout because the admin setting is lower than the DB create latency.
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_NOK(conn.Execute("CREATE DATABASE " + test_name));
  }

  // Verify DocDB Database creation, even though it failed in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    Result<bool> ret = client->NamespaceExists(test_name, YQLDatabase::YQL_DATABASE_PGSQL);
    WARN_NOT_OK(ResultToStatus(ret), "" /* prefix */);
    return ret.ok() && ret.get();
  }, MonoDelta::FromSeconds(60),
     "Verify Namespace was created in DocDB"));

  // After bg_task_wait, DocDB will notice the PG layer failure because the transaction aborts.
  // Confirm that DocDB async deletes the namespace.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    Result<bool> ret = client->NamespaceExists(test_name, YQLDatabase::YQL_DATABASE_PGSQL);
    WARN_NOT_OK(ResultToStatus(ret), "ret");
    return ret.ok() && ret.get() == false;
  }, MonoDelta::FromSeconds(20), "Verify Namespace was removed by Transaction GC"));
}

TEST_F(PgLibPqDatabaseTimeoutTest, YB_DISABLE_TEST_IN_TSAN(TestDatabaseTimeoutAndRestartGC)) {
  NamespaceName test_name = "test_pgsql";
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Create Database: will timeout because the admin setting is lower than the DB create latency.
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_NOK(conn.Execute("CREATE DATABASE " + test_name));
  }

  // Verify DocDB Database creation, even though it fails in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    Result<bool> ret = client->NamespaceExists(test_name, YQLDatabase::YQL_DATABASE_PGSQL);
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == true;
  }, MonoDelta::FromSeconds(60),
      "Verify Namespace was created in DocDB"));

  LOG(INFO) << "Restarting Master.";

  // Restart the master before the BG task can kick in and GC the failed transaction.
  auto master = cluster_->GetLeaderMaster();
  master->Shutdown();
  ASSERT_OK(master->Restart());
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    auto s = cluster_->GetIsMasterLeaderServiceReady(master);
    return s.ok();
  }, MonoDelta::FromSeconds(20), "Wait for Master to be ready."));

  // Confirm that Catalog Loader deletes the namespace on master restart.
  client = ASSERT_RESULT(cluster_->CreateClient()); // Reinit the YBClient after restart.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    Result<bool> ret = client->NamespaceExists(test_name, YQLDatabase::YQL_DATABASE_PGSQL);
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == false;
  }, MonoDelta::FromSeconds(20), "Verify Namespace was removed by Transaction GC"));
}

class PgLibPqTableTimeoutTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Use small clock skew, to decrease number of read restarts.
    options->extra_tserver_flags.push_back("--TEST_user_ddl_operation_timeout_sec=1");
    options->extra_master_flags.push_back("--TEST_simulate_slow_table_create_secs=2");
    options->extra_master_flags.push_back("--ysql_transaction_bg_task_wait_ms=3000");
  }
};

TEST_F(PgLibPqTableTimeoutTest, YB_DISABLE_TEST_IN_TSAN(TestTableTimeoutGC)) {
  const string kDatabaseName ="yugabyte";
  NamespaceName test_name = "test_pgsql_table";
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Create Table: will timeout because the admin setting is lower than the DB create latency.
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_NOK(conn.Execute("CREATE TABLE " + test_name + " (key INT PRIMARY KEY)"));
  }

  // Wait for DocDB Table creation, even though it will fail in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    LOG(INFO) << "Requesting TableExists";
    auto ret = client->TableExists(
        client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, test_name));
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == true;
  }, MonoDelta::FromSeconds(20), "Verify Table was created in DocDB"));

  // DocDB will notice the PG layer failure because the transaction aborts.
  // Confirm that DocDB async deletes the namespace.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    auto ret = client->TableExists(
        client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, test_name));
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == false;
  }, MonoDelta::FromSeconds(20), "Verify Table was removed by Transaction GC"));
}

TEST_F(PgLibPqTableTimeoutTest, YB_DISABLE_TEST_IN_TSAN(TestTableTimeoutAndRestartGC)) {
  const string kDatabaseName ="yugabyte";
  NamespaceName test_name = "test_pgsql_table";
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Create Table: will timeout because the admin setting is lower than the DB create latency.
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_NOK(conn.Execute("CREATE TABLE " + test_name + " (key INT PRIMARY KEY)"));
  }

  // Wait for DocDB Table creation, even though it will fail in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    LOG(INFO) << "Requesting TableExists";
    auto ret = client->TableExists(
        client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, test_name));
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == true;
  }, MonoDelta::FromSeconds(20), "Verify Table was created in DocDB"));

  LOG(INFO) << "Restarting Master.";

  // Restart the master before the BG task can kick in and GC the failed transaction.
  auto master = cluster_->GetLeaderMaster();
  master->Shutdown();
  ASSERT_OK(master->Restart());
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    auto s = cluster_->GetIsMasterLeaderServiceReady(master);
    return s.ok();
  }, MonoDelta::FromSeconds(20), "Wait for Master to be ready."));

  // Confirm that Catalog Loader deletes the namespace on master restart.
  client = ASSERT_RESULT(cluster_->CreateClient()); // Reinit the YBClient after restart.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    auto ret = client->TableExists(
        client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, test_name));
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == false;
  }, MonoDelta::FromSeconds(20), "Verify Table was removed by Transaction GC"));
}

class PgLibPqIndexTableTimeoutTest : public PgLibPqTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--TEST_user_ddl_operation_timeout_sec=10");
  }
};

TEST_F(PgLibPqIndexTableTimeoutTest, YB_DISABLE_TEST_IN_TSAN(TestIndexTableTimeoutGC)) {
  const string kDatabaseName ="yugabyte";
  NamespaceName test_name = "test_pgsql_table";
  NamespaceName test_name_idx = test_name + "_idx";

  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Lower the delays so we successfully create this first table.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "10"));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_simulate_slow_table_create_secs", "0"));

  // Create Table that Index will be set on.
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("CREATE TABLE " + test_name + " (key INT PRIMARY KEY)"));
  }

  // After successfully creating the first table, set to flags similar to: PgLibPqTableTimeoutTest.
  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_transaction_bg_task_wait_ms", "13000"));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_simulate_slow_table_create_secs", "12"));

  // Create Index: will timeout because the admin setting is lower than the DB create latency.
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_NOK(conn.Execute("CREATE INDEX " + test_name_idx + " ON " + test_name + "(key)"));
  }

  // Wait for DocDB Table creation, even though it will fail in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    LOG(INFO) << "Requesting TableExists";
    auto ret = client->TableExists(
        client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, test_name_idx));
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == true;
  }, MonoDelta::FromSeconds(40), "Verify Index Table was created in DocDB"));

  // DocDB will notice the PG layer failure because the transaction aborts.
  // Confirm that DocDB async deletes the namespace.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    auto ret = client->TableExists(
        client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, test_name_idx));
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == false;
  }, MonoDelta::FromSeconds(40), "Verify Index Table was removed by Transaction GC"));
}

class PgLibPqTestEnumType: public PgLibPqTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--TEST_do_not_add_enum_sort_order=true");
  }
};

// Make sure that enum type backfill works.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(EnumType),
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
  PGResultPtr res = ASSERT_RESULT(conn->Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 3);
  ASSERT_EQ(PQnfields(res.get()), 1);
  std::vector<string> values = {
    ASSERT_RESULT(GetString(res.get(), 0, 0)),
    ASSERT_RESULT(GetString(res.get(), 1, 0)),
    ASSERT_RESULT(GetString(res.get(), 2, 0)),
  };
  ASSERT_EQ(values[0], "b");
  ASSERT_EQ(values[1], "c");
  ASSERT_EQ(values[2], "a");

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
  res = ASSERT_RESULT(conn->Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 6);
  ASSERT_EQ(PQnfields(res.get()), 1);
  values = {
    ASSERT_RESULT(GetString(res.get(), 0, 0)),
    ASSERT_RESULT(GetString(res.get(), 1, 0)),
    ASSERT_RESULT(GetString(res.get(), 2, 0)),
    ASSERT_RESULT(GetString(res.get(), 3, 0)),
    ASSERT_RESULT(GetString(res.get(), 4, 0)),
    ASSERT_RESULT(GetString(res.get(), 5, 0)),
  };
  ASSERT_EQ(values[0], "b");
  ASSERT_EQ(values[1], "e");
  ASSERT_EQ(values[2], "f");
  ASSERT_EQ(values[3], "c");
  ASSERT_EQ(values[4], "a");
  ASSERT_EQ(values[5], "d");

  // Create an index on the enum table column.
  ASSERT_OK(conn->ExecuteFormat("CREATE INDEX ON $0 (id ASC)", kTableName));

  // Index only scan to verify contents of index table.
  ASSERT_TRUE(ASSERT_RESULT(conn->HasIndexScan(query)));
  res = ASSERT_RESULT(conn->Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 6);
  ASSERT_EQ(PQnfields(res.get()), 1);
  values = {
    ASSERT_RESULT(GetString(res.get(), 0, 0)),
    ASSERT_RESULT(GetString(res.get(), 1, 0)),
    ASSERT_RESULT(GetString(res.get(), 2, 0)),
    ASSERT_RESULT(GetString(res.get(), 3, 0)),
    ASSERT_RESULT(GetString(res.get(), 4, 0)),
    ASSERT_RESULT(GetString(res.get(), 5, 0)),
  };
  ASSERT_EQ(values[0], "b");
  ASSERT_EQ(values[1], "e");
  ASSERT_EQ(values[2], "f");
  ASSERT_EQ(values[3], "c");
  ASSERT_EQ(values[4], "a");
  ASSERT_EQ(values[5], "d");

  // Test where clause.
  const std::string query2 = Format("SELECT * FROM $0 where id = 'b'", kTableName);
  ASSERT_TRUE(ASSERT_RESULT(conn->HasIndexScan(query2)));
  res = ASSERT_RESULT(conn->Fetch(query2));
  ASSERT_EQ(PQntuples(res.get()), 1);
  ASSERT_EQ(PQnfields(res.get()), 1);
  const string value = ASSERT_RESULT(GetString(res.get(), 0, 0));
  ASSERT_EQ(value, "b");
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
          YB_DISABLE_TEST_IN_TSAN(LargeOid),
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
  std::string query = "SELECT oid FROM pg_enum";
  PGResultPtr res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 3);
  ASSERT_EQ(PQnfields(res.get()), 1);
  std::vector<int32> enum_oids = {
    ASSERT_RESULT(GetInt32(res.get(), 0, 0)),
    ASSERT_RESULT(GetInt32(res.get(), 1, 0)),
    ASSERT_RESULT(GetInt32(res.get(), 2, 0)),
  };
  // Ensure that we do see large OIDs in pg_enum table.
  LOG(INFO) << "enum_oids: " << (Oid)enum_oids[0] << ","
            << (Oid)enum_oids[1] << "," << (Oid)enum_oids[2];
  ASSERT_GT((Oid)enum_oids[0], kOidAdjustment);
  ASSERT_GT((Oid)enum_oids[1], kOidAdjustment);
  ASSERT_GT((Oid)enum_oids[2], kOidAdjustment);

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
  res = ASSERT_RESULT(conn.Fetch(query));
  ASSERT_EQ(PQntuples(res.get()), 3);
  ASSERT_EQ(PQnfields(res.get()), 1);
  std::vector<string> enum_values = {
    ASSERT_RESULT(GetString(res.get(), 0, 0)),
    ASSERT_RESULT(GetString(res.get(), 1, 0)),
    ASSERT_RESULT(GetString(res.get(), 2, 0)),
  };
  ASSERT_EQ(enum_values[0], "a");
  ASSERT_EQ(enum_values[1], "b");
  ASSERT_EQ(enum_values[2], "c");
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

bool RetryableError(const Status& status) {
  const auto msg = status.message().ToBuffer();
  const std::string expected_errors[] = {"Try again",
                                         "Catalog Version Mismatch",
                                         "Restart read required at",
                                         "schema version mismatch for table"};
  for (const auto& expected : expected_errors) {
    if (msg.find(expected) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(PagingReadRestart)) {
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
          return (res.ok() || RetryableError(res.status())) ? Status::OK() : res.status();
    });
  }
  CoordinatedRunner runner(std::move(commands));
  std::this_thread::sleep_for(10s);
  runner.Stop();
  ASSERT_FALSE(runner.HasError());
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(CollationRangePresplit)) {
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
        Format("--yb_backend_oom_score_adj=" + expected_oom_score));
  }

 protected:
  const std::string expected_oom_score = "123";
};

TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(TestLWPgBackendKillAfterLWLockAcquire),
          PgLibPqYSQLBackendCrash) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_NOK(conn1.FetchFormat("SELECT pg_stat_statements_reset()"));
  ASSERT_NOK(conn2.FetchFormat("SELECT 1"));

  // validate that this query is added to yb_terminated_queries
  auto conn3 = ASSERT_RESULT(Connect());
  const string get_yb_terminated_queries =
    "SELECT query_text, termination_reason FROM yb_pg_stat_get_queries(NULL)";
  auto row_str = ASSERT_RESULT(conn3.FetchAllAsString(get_yb_terminated_queries));
  LOG(INFO) << "Result string: " << row_str;
  ASSERT_EQ(row_str, "SELECT pg_stat_statements_reset(), Terminated by SIGKILL");
}

#ifdef __linux__
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(TestOomScoreAdjPGBackend),
          PgLibPqYSQLBackendCrash) {

  auto conn = ASSERT_RESULT(Connect());
  auto res = ASSERT_RESULT(conn.Fetch("SELECT pg_backend_pid()"));

  auto backend_pid = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
  std::string file_name = "/proc/" + std::to_string(backend_pid) + "/oom_score_adj";
  std::ifstream fPtr(file_name);
  std::string oom_score_adj;
  getline(fPtr, oom_score_adj);
  ASSERT_EQ(oom_score_adj, expected_oom_score);
}
#endif

// The motive of this test is to prove that when a postgres backend errors out
// while possessing an LWLock, the lock is released.
// TEST_yb_lwlock_error_after_acquire_pg_stat_statements_reset when set true
// will error out a postgres backend after acquiring a LWLock. Specifically in
// this example, when pg_stat_statements_reset() function is called when this
// flag is set, it errors out after acquiring a lock on pgss->lock.
// We verify that future commands on the same connection do not deadlock as the
// lock should have been released after error.
class PgLibPqYSQLBackendError: public PgLibPqTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        Format("--TEST_yb_lwlock_error_after_acquire_pg_stat_statements_reset=true"));
  }
};

TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(TestLWPgBackendErrorAfterLWLockAcquire),
          PgLibPqYSQLBackendError) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_NOK(conn.FetchFormat("SELECT pg_stat_statements_reset()"));

  // Verify that future commands on the same connection works.
  EXPECT_OK(conn.FetchFormat("SELECT 1"));
}

class PgLibPqRefreshMatviewFailure: public PgLibPqTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        Format("--TEST_yb_test_fail_matview_refresh_after_creation=true"));
    options->extra_master_flags.push_back("--ysql_transaction_bg_task_wait_ms=3000");
  }
};

// Test that an orphaned table left after a failed refresh on a materialized view is cleaned up
// by transaction GC.
TEST_F_EX(PgLibPqTest,
          YB_DISABLE_TEST_IN_TSAN(TestRefreshMatviewFailure),
          PgLibPqRefreshMatviewFailure) {

  const string kDatabaseName = "yugabyte";

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t(id int)"));
  ASSERT_OK(conn.ExecuteFormat("CREATE MATERIALIZED VIEW mv AS SELECT * FROM t"));
  auto res = ASSERT_RESULT(conn.Fetch("SELECT oid FROM pg_class WHERE relname = 'mv'"));
  ASSERT_EQ(PQntuples(res.get()), 1);
  auto matview_oid = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
  auto pg_temp_table_name = "pg_temp_" + std::to_string(matview_oid);
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_user_ddl_operation_timeout_sec", "1"));
  ASSERT_NOK(conn.ExecuteFormat("REFRESH MATERIALIZED VIEW mv"));

  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Wait for DocDB Table (materialized view) creation, even though it will fail in PG layer.
  // 'ysql_transaction_bg_task_wait_ms' setting ensures we can finish this before the GC.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    LOG(INFO) << "Requesting TableExists";
    auto ret = client->TableExists(
        client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, pg_temp_table_name));
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == true;
  }, MonoDelta::FromSeconds(20), "Verify Table was created in DocDB"));

  // DocDB will notice the PG layer failure because the transaction aborts.
  // Confirm that DocDB async deletes the orphaned materialized view.
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    auto ret = client->TableExists(
        client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, pg_temp_table_name));
    WARN_NOT_OK(ResultToStatus(ret), "");
    return ret.ok() && ret.get() == false;
  }, MonoDelta::FromSeconds(40), "Verify Table was removed by Transaction GC"));
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(NonBreakingDDLMode)) {
  const string kDatabaseName = "yugabyte";

  auto conn1 = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  auto conn2 = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn1.Execute("CREATE TABLE t1(a int)"));
  ASSERT_OK(conn1.Execute("CREATE TABLE t2(a int)"));
  ASSERT_OK(conn1.Execute("BEGIN"));
  auto res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t1"));
  ASSERT_EQ(0, PQntuples(res.get()));
  ASSERT_OK(conn2.Execute("REVOKE ALL ON t2 FROM public"));
  // Wait for the new catalog version to propagate to TServers.
  std::this_thread::sleep_for(2s);
  // REVOKE is a breaking catalog change, the running transaction on conn1 is aborted.
  auto result = conn1.Fetch("SELECT * FROM t1");
  ASSERT_NOK(result);
  auto status = ResultToStatus(result);
  const string msg = "catalog snapshot used for this transaction has been invalidated";
  ASSERT_TRUE(status.ToString().find(msg) != std::string::npos);
  ASSERT_OK(conn1.Execute("ABORT"));

  // Let's start over, but this time use yb_make_next_ddl_statement_nonbreaking to suppress the
  // breaking catalog change and the SELECT command on conn1 runs successfully.
  ASSERT_OK(conn1.Execute("BEGIN"));
  res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t1"));
  ASSERT_EQ(0, PQntuples(res.get()));
  ASSERT_OK(conn2.Execute("SET yb_make_next_ddl_statement_nonbreaking TO TRUE"));
  ASSERT_OK(conn2.Execute("REVOKE ALL ON t2 FROM public"));
  // Wait for the new catalog version to propagate to TServers.
  std::this_thread::sleep_for(2s);
  res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t1"));
  ASSERT_EQ(0, PQntuples(res.get()));

  // Verify that the session variable yb_make_next_ddl_statement_nonbreaking auto-resets to false.
  // As a result, the running transaction on conn1 is aborted.
  ASSERT_OK(conn2.Execute("REVOKE ALL ON t2 FROM public"));
  // Wait for the new catalog version to propagate to TServers.
  std::this_thread::sleep_for(2s);
  result = conn1.Fetch("SELECT * FROM t1");
  ASSERT_NOK(result);
  status = ResultToStatus(result);
  ASSERT_TRUE(status.ToString().find(msg) != std::string::npos);
  ASSERT_OK(conn1.Execute("ABORT"));
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(AggrSystemColumn)) {
  const string kDatabaseName = "yugabyte";
  auto conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  // Count oid column which is a system column.
  auto res = ASSERT_RESULT(conn.Fetch("SELECT COUNT(oid) FROM pg_type"));
  auto lines = PQntuples(res.get());
  ASSERT_EQ(lines, 1);
  auto columns = PQnfields(res.get());
  ASSERT_EQ(columns, 1);
  int64 count_oid = static_cast<uint32>(CHECK_RESULT(GetInt64(res.get(), 0, 0)));
  // Should get a positive count.
  ASSERT_GT(count_oid, 0);

  // Count oid column which is a system column, but cast oid to int.
  res = ASSERT_RESULT(conn.Fetch("SELECT COUNT(oid::int) FROM pg_type"));
  lines = PQntuples(res.get());
  ASSERT_EQ(lines, 1);
  columns = PQnfields(res.get());
  ASSERT_EQ(columns, 1);
  int64 count_oid_int = static_cast<uint32>(CHECK_RESULT(GetInt64(res.get(), 0, 0)));
  // Should get the same count.
  ASSERT_EQ(count_oid_int, count_oid);

  // Count typname column which is a regular column.
  res = ASSERT_RESULT(conn.Fetch("SELECT COUNT(typname) FROM pg_type"));
  lines = PQntuples(res.get());
  ASSERT_EQ(lines, 1);
  columns = PQnfields(res.get());
  ASSERT_EQ(columns, 1);
  int64 count_typname = static_cast<uint32>(CHECK_RESULT(GetInt64(res.get(), 0, 0)));
  // Should get the same count.
  ASSERT_EQ(count_oid, count_typname);

  // Test unsupported system columns which would otherwise get the same count as shown
  // in vanilla Postgres.
  auto result = conn.Fetch("SELECT COUNT(ctid) FROM pg_type");
  ASSERT_NOK(result);
  result = conn.Fetch("SELECT COUNT(cmin) FROM pg_type");
  ASSERT_NOK(result);
  result = conn.Fetch("SELECT COUNT(cmax) FROM pg_type");
  ASSERT_NOK(result);
  result = conn.Fetch("SELECT COUNT(xmin) FROM pg_type");
  ASSERT_NOK(result);
  result = conn.Fetch("SELECT COUNT(xmax) FROM pg_type");
  ASSERT_NOK(result);

  // Test SUM(oid) results in error.
  result = conn.Fetch("SELECT SUM(oid) FROM pg_type");
  ASSERT_NOK(result);
}

} // namespace pgwrapper
} // namespace yb

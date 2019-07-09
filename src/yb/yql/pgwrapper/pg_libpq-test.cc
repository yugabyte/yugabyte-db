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

#include <boost/scope_exit.hpp>

#include "yb/util/random_util.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

using namespace std::literals;

DECLARE_int64(retryable_rpc_single_call_timeout_ms);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

namespace yb {
namespace pgwrapper {

class PgLibPqTest : public PgWrapperTestBase {
 protected:
  Result<PGConnPtr> Connect() {
    PGConnPtr result(PQconnectdb(Format(
        "host=$0 port=$1 user=postgres", pg_ts->bind_host(), pg_ts->pgsql_rpc_port()).c_str()));
    auto status = PQstatus(result.get());
    if (status != ConnStatusType::CONNECTION_OK) {
      return STATUS_FORMAT(NetworkError, "Connect failed: $0", status);
    }
    return result;
  }

  void TestMultiBankAccount(const std::string& isolation_level);
};

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(Simple)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(Execute(conn.get(), "CREATE TABLE t (key INT, value TEXT)"));
  ASSERT_OK(Execute(conn.get(), "INSERT INTO t (key, value) VALUES (1, 'hello')"));

  auto res = ASSERT_RESULT(Fetch(conn.get(), "SELECT * FROM t"));

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
  static const std::string kTryAgain = "Try again.";
  constexpr auto kKeys = RegularBuildVsSanitizers(10, 20);
  constexpr auto kColors = 2;
  constexpr auto kIterations = 20;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(Execute(conn.get(), "CREATE TABLE t (key INT PRIMARY KEY, color INT)"));

  auto iterations_left = kIterations;

  for (int iteration = 0; iterations_left > 0; ++iteration) {
    SCOPED_TRACE(Format("Iteration: $0", iteration));

    auto status = Execute(conn.get(), "DELETE FROM t");
    if (!status.ok()) {
      ASSERT_STR_CONTAINS(status.ToString(), kTryAgain);
      continue;
    }
    for (int k = 0; k != kKeys; ++k) {
      int32_t color = RandomUniformInt(0, kColors - 1);
      ASSERT_OK(Execute(conn.get(),
          Format("INSERT INTO t (key, color) VALUES ($0, $1)", k, color)));
    }

    std::atomic<int> complete{ 0 };
    std::vector<std::thread> threads;
    for (int i = 0; i != kColors; ++i) {
      int32_t color = i;
      threads.emplace_back([this, color, kKeys, &complete] {
        auto conn = ASSERT_RESULT(Connect());

        ASSERT_OK(Execute(conn.get(), "BEGIN"));
        ASSERT_OK(Execute(conn.get(), "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE"));

        auto res = Fetch(conn.get(), "SELECT * FROM t");
        if (!res.ok()) {
          auto msg = res.status().message().ToBuffer();
          ASSERT_STR_CONTAINS(res.status().ToString(), kTryAgain);
          return;
        }
        auto columns = PQnfields(res->get());
        ASSERT_EQ(2, columns);

        auto lines = PQntuples(res->get());
        ASSERT_EQ(kKeys, lines);
        for (int i = 0; i != lines; ++i) {
          if (ASSERT_RESULT(GetInt32(res->get(), i, 1)) == color) {
            continue;
          }

          auto key = ASSERT_RESULT(GetInt32(res->get(), i, 0));
          auto status = Execute(
              conn.get(), Format("UPDATE t SET color = $1 WHERE key = $0", key, color));
          if (!status.ok()) {
            auto msg = status.message().ToBuffer();
            // Missing metadata means that transaction was aborted and cleaned.
            ASSERT_TRUE(msg.find("Try again.") != std::string::npos ||
                        msg.find("Missing metadata") != std::string::npos) << status;
            break;
          }
        }

        auto status = Execute(conn.get(), "COMMIT");
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

    auto res = ASSERT_RESULT(Fetch(conn.get(), "SELECT * FROM t"));
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

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(Execute(conn.get(), "CREATE TABLE t (key INT PRIMARY KEY)"));

  size_t reads_won = 0, writes_won = 0;
  for (int i = 0; i != kKeys; ++i) {
    auto read_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(Execute(read_conn.get(), "BEGIN ISOLATION LEVEL SERIALIZABLE"));
    auto res = Fetch(read_conn.get(), Format("SELECT * FROM t WHERE key = $0", i));
    auto read_status = ResultToStatus(res);

    auto write_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(Execute(write_conn.get(), "BEGIN ISOLATION LEVEL SERIALIZABLE"));
    auto write_status = Execute(write_conn.get(), Format("INSERT INTO t (key) VALUES ($0)", i));

    std::thread read_commit_thread([&read_conn, &read_status] {
      if (read_status.ok()) {
        read_status = Execute(read_conn.get(), "COMMIT");
      }
    });

    std::thread write_commit_thread([&write_conn, &write_status] {
      if (write_status.ok()) {
        write_status = Execute(write_conn.get(), "COMMIT");
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

  LOG(INFO) << "Reads won: " << reads_won << ", writes won: " << writes_won;
  if (RegularBuildVsSanitizers(true, false)) {
    ASSERT_GE(reads_won, kKeys / 4);
    ASSERT_GE(writes_won, kKeys / 4);
  }
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ReadRestart)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(Execute(conn.get(), "CREATE TABLE t (key INT PRIMARY KEY)"));

  std::atomic<bool> stop(false);
  std::atomic<int> last_written(0);

  std::thread write_thread([this, &stop, &last_written] {
    auto write_conn = ASSERT_RESULT(Connect());
    int write_key = 1;
    while (!stop.load(std::memory_order_acquire)) {
      SCOPED_TRACE(Format("Writing: $0", write_key));

      ASSERT_OK(Execute(write_conn.get(), "BEGIN"));
      auto status = Execute(write_conn.get(), Format("INSERT INTO t (key) VALUES ($0)", write_key));
      if (status.ok()) {
        status = Execute(write_conn.get(), "COMMIT");
      }
      if (status.ok()) {
        last_written.store(write_key, std::memory_order_release);
        ++write_key;
      } else {
        LOG(INFO) << "Write " << write_key << " failed: " << status;
      }
    }
  });

  BOOST_SCOPE_EXIT(&stop, &write_thread) {
    stop.store(true, std::memory_order_release);
    write_thread.join();
  } BOOST_SCOPE_EXIT_END;

  auto deadline = CoarseMonoClock::now() + 30s;

  while (CoarseMonoClock::now() < deadline) {
    int read_key = last_written.load(std::memory_order_acquire);
    if (read_key == 0) {
      std::this_thread::sleep_for(100ms);
      continue;
    }

    SCOPED_TRACE(Format("Reading: $0", read_key));

    ASSERT_OK(Execute(conn.get(), "BEGIN"));

    auto res = ASSERT_RESULT(Fetch(conn.get(), Format("SELECT * FROM t WHERE key = $0", read_key)));
    auto columns = PQnfields(res.get());
    ASSERT_EQ(1, columns);

    auto lines = PQntuples(res.get());
    ASSERT_EQ(1, lines);

    auto key = ASSERT_RESULT(GetInt32(res.get(), 0, 0));
    ASSERT_EQ(key, read_key);

    ASSERT_OK(Execute(conn.get(), "ROLLBACK"));
  }

  ASSERT_GE(last_written.load(std::memory_order_acquire), 100);
}

// Concurrently insert records to table with index.
TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(ConcurrentIndexInsert)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(Execute(
      conn.get(),
      "CREATE TABLE IF NOT EXISTS users(id text, ename text, age int, PRIMARY KEY(id))"));

  ASSERT_OK(Execute(
      conn.get(), "CREATE INDEX IF NOT EXISTS name_idx ON users(ename)"));

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
        ASSERT_OK(Execute(
            write_conn.get(),
            Format("INSERT INTO users (id, ename, age) VALUES ('user-$0-$1', 'name-$1', $2)",
                   tid, idx, 20 + (idx % 50))));
        ++idx;
      }
    });
  }

  BOOST_SCOPE_EXIT(&stop, &write_threads) {
    stop.store(true, std::memory_order_release);
    for (auto& thread : write_threads) {
      thread.join();
    }
  } BOOST_SCOPE_EXIT_END;

  std::this_thread::sleep_for(30s);
}

bool TransactionalFailure(const Status& status) {
  auto message = status.ToString();
  return message.find("Restart read required at") != std::string::npos ||
         message.find("Transaction expired") != std::string::npos ||
         message.find("Conflicts with committed transaction") != std::string::npos ||
         message.find("Value write after transaction start") != std::string::npos ||
         message.find("Conflicts with higher priority transaction") != std::string::npos;
}

Result<int64_t> ReadSumBalance(
    PGconn* conn, int accounts, const std::string& begin_transaction_statement,
    std::atomic<int>* counter) {
  RETURN_NOT_OK(Execute(conn, begin_transaction_statement));
  bool failed = true;
  BOOST_SCOPE_EXIT(conn, &failed) {
    if (failed) {
      EXPECT_OK(Execute(conn, "ROLLBACK"));
    }
  } BOOST_SCOPE_EXIT_END;

  int64_t sum = 0;
  for (int i = 1; i <= accounts; ++i) {
    LOG(INFO) << "Reading: " << i;
    sum += VERIFY_RESULT(FetchValue<int64_t>(
        conn, Format("SELECT balance FROM account_$0 WHERE id = $0", i)));
  }

  failed = false;
  RETURN_NOT_OK(Execute(conn, "COMMIT"));
  return sum;
}

void PgLibPqTest::TestMultiBankAccount(const std::string& isolation_level) {
  constexpr int kAccounts = RegularBuildVsSanitizers(20, 10);
  constexpr int64_t kInitialBalance = 100;

#ifndef NDEBUG
  const auto kTimeout = 180s;
  constexpr int kThreads = RegularBuildVsSanitizers(12, 5);
#else
  const auto kTimeout = 60s;
  constexpr int kThreads = 5;
#endif

  PGConnPtr conn;
  ASSERT_OK(WaitFor([this, &conn] {
    auto res = Connect();
    if (!res.ok()) {
      return false;
    }
    conn = std::move(*res);
    return true;
  }, 5s, "Initial connect"));

  const std::string begin_transaction_statement =
      "START TRANSACTION ISOLATION LEVEL " + isolation_level;

  for (int i = 1; i <= kAccounts; ++i) {
    ASSERT_OK(Execute(
        conn.get(),
        Format("CREATE TABLE account_$0 (id int, balance bigint, PRIMARY KEY(id))", i)));
    ASSERT_OK(Execute(
        conn.get(),
        Format("INSERT INTO account_$0 (id, balance) VALUES ($0, $1)", i, kInitialBalance)));
  }

  std::atomic<int> writes(0);
  std::atomic<int> reads(0);

  std::atomic<int> counter(100000);
  TestThreadHolder thread_holder;
  for (int i = 1; i <= kThreads; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &writes, &begin_transaction_statement,
         &stop_flag = thread_holder.stop_flag()]() {
      auto conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load(std::memory_order_acquire)) {
        int from = RandomUniformInt(1, kAccounts);
        int to = RandomUniformInt(1, kAccounts - 1);
        if (to >= from) {
          ++to;
        }
        int64_t amount = RandomUniformInt(1, 10);
        ASSERT_OK(Execute(conn.get(), begin_transaction_statement));
        auto status = Execute(conn.get(), Format(
              "UPDATE account_$0 SET balance = balance - $1 WHERE id = $0", from, amount));
        if (status.ok()) {
          status = Execute(conn.get(), Format(
              "UPDATE account_$0 SET balance = balance + $1 WHERE id = $0", to, amount));
        }
        if (status.ok()) {
          status = Execute(conn.get(), "COMMIT;");
        } else {
          ASSERT_OK(Execute(conn.get(), "ROLLBACK;"));
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
      [this, &counter, &reads, &begin_transaction_statement,
       &stop_flag = thread_holder.stop_flag()]() {
    auto conn = ASSERT_RESULT(Connect());
    while (!stop_flag.load(std::memory_order_acquire)) {
      auto sum = ReadSumBalance(conn.get(), kAccounts, begin_transaction_statement, &counter);
      if (!sum.ok()) {
        ASSERT_TRUE(TransactionalFailure(sum.status())) << sum.status();
      } else {
        ASSERT_EQ(*sum, kAccounts * kInitialBalance);
        ++reads;
      }
    }
  });

  constexpr auto kRequiredReads = RegularBuildVsSanitizers(5, 2);
  constexpr auto kRequiredWrites = RegularBuildVsSanitizers(1000, 500);
  auto wait_status = WaitFor([&reads, &writes, &stop = thread_holder.stop_flag()] {
    return stop.load() || (writes.load() >= kRequiredWrites && reads.load() >= kRequiredReads);
  }, kTimeout, Format("At least $0 reads and $1 writes", kRequiredReads, kRequiredWrites));

  LOG(INFO) << "Writes: " << writes.load() << ", reads: " << reads.load();

  ASSERT_OK(wait_status);

  thread_holder.Stop();

  ASSERT_OK(WaitFor([&conn, &begin_transaction_statement, &counter]() -> Result<bool> {
    auto sum = ReadSumBalance(conn.get(), kAccounts, begin_transaction_statement, &counter);
    if (!sum.ok()) {
      if (!TransactionalFailure(sum.status())) {
        return sum.status();
      }
      return false;
    }
    EXPECT_EQ(*sum, kAccounts * kInitialBalance);
    return true;
  }, 10s, "Final read"));
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(MultiBankAccountSnapshot)) {
  TestMultiBankAccount("REPEATABLE READ");
}

TEST_F(PgLibPqTest, YB_DISABLE_TEST_IN_TSAN(MultiBankAccountSerializable)) {
  TestMultiBankAccount("SERIALIZABLE");
}

} // namespace pgwrapper
} // namespace yb

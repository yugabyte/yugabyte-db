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

#include "yb/common/common.pb.h"

#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

DECLARE_int64(external_mini_cluster_max_log_bytes);

namespace yb {
namespace pgwrapper {

class PgOnConflictTest : public LibPqTestBase {
 protected:
  void TestOnConflict(bool kill_master, const MonoDelta& duration);
};

class PgFailOnConflictTest : public PgOnConflictTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    PgOnConflictTest::UpdateMiniClusterOptions(opts);
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    opts->extra_tserver_flags.push_back("--enable_wait_queues=false");
    opts->extra_tserver_flags.push_back("--yb_enable_read_committed_isolation=true");
  }
};

namespace {

struct OnConflictKey {
  int key;
  size_t operation_index = 0;
};

constexpr int kMaxBatchSize = 5;

struct BatchInfo {
  int key;
  char append_char; // Zero means read request
  std::string read_value;

  std::string ToString() const {
    if (append_char) {
      char x[2] = {append_char, 0};
      return Format("[$0+$1]", key, x);
    } else {
      return Format("[$0 $1]", key, read_value);
    }
  }

  bool ComesBefore(const BatchInfo& rhs) const {
    if (key != rhs.key) {
      return false;
    }
    if (append_char) {
      if (rhs.append_char) {
        return false;
      }
      // rhs see our append
      return rhs.read_value.find(append_char) != std::string::npos;
    } else if (!rhs.append_char) {
      // rhs has larger list
      return read_value.length() < rhs.read_value.length();
    } else {
      // we don't see the result of rhs
      return read_value.find(rhs.append_char) == std::string::npos;
    }
  }
};

struct TransactionInfo {
  typedef std::array<BatchInfo, kMaxBatchSize> Batches;
  typedef Batches::const_iterator const_iterator;

  int batch_size = 0;
  Batches batches;
  int last_visit = 0; // Used to check whether this vertex was visited by current DFS run.

  const_iterator begin() const {
    return batches.begin();
  }

  const_iterator end() const {
    return batches.begin() + batch_size;
  }

  bool ComesBefore(const TransactionInfo& rhs) const {
    for (const auto& lbatch : *this) {
      for (const auto& rbatch : rhs) {
        if (lbatch.ComesBefore(rbatch)) {
          return true;
        }
      }
    }
    return false;
  }
};

class OnConflictHelper {
 public:
  explicit OnConflictHelper(size_t concurrent_keys)
      : concurrent_keys_(concurrent_keys), active_keys_(concurrent_keys) {
    for(size_t i = 0; i != concurrent_keys; ++i) {
      active_keys_[i].key = ++next_key_;
    }
    for (auto i = 'A'; i <= 'Z'; ++i) {
      chars_.push_back(i);
    }
  }

  std::pair<int, char> RandomPair() {
    size_t i = RandomUniformInt<size_t>(0, concurrent_keys_ - 1);
    std::lock_guard<std::mutex> lock(mutex_);
    auto& key = active_keys_[i];
    char append_char;
    if (RandomUniformBool()) {
      append_char = 0; // Read key
    } else {
      append_char = chars_[key.operation_index];
      if (++key.operation_index == chars_.size()) {
        key.key = ++next_key_;
        key.operation_index = 0;
      }
    }
    return std::make_pair(key.key, append_char);
  }

  void Committed(TransactionInfo&& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    committed_.push_back(std::move(info));
  }

  void Report() {
    LOG(INFO) << "Committed transactions:";

    ordered_.reserve(committed_.size());
    // Iteration order does not matter here, so we iterate from end to have lower keys at the start
    // of the list.
    for (auto it = committed_.rbegin(); it != committed_.rend(); ++it) {
      if (it->last_visit == 0) {
        DepthFirstSearch(&*it, nullptr /* dest */);
      }
    }

    std::reverse(ordered_.begin(), ordered_.end());

    for (const auto* info : ordered_) {
      LOG(INFO) << "  " << yb::ToString(*info);
    }

    int inversions = 0;
    for (auto it = ordered_.begin(); it != ordered_.end(); ++it) {
      for (auto j = ordered_.begin(); j != it; ++j) {
        if ((**it).ComesBefore(**j)) {
          LOG(INFO) << "Order inversion: " << yb::ToString(**it) << " and " << yb::ToString(**j);
          ++inversions;
          ++query_;
          DepthFirstSearch(*j, *it);
        }
      }
    }

    ASSERT_EQ(inversions, 0);
  }

 private:
  // Returns true if dest was reached.
  bool DepthFirstSearch(TransactionInfo* v, TransactionInfo* dest) {
    v->last_visit = query_;
    if (v == dest) {
      LOG(INFO) << "  " << yb::ToString(*v);
      return true;
    }
    for (auto& target : committed_) {
      if (target.last_visit < query_ && v->ComesBefore(target)) {
        if (DepthFirstSearch(&target, dest)) {
          LOG(INFO) << "  " << yb::ToString(*v);
          return true;
        }
      }
    }
    if (!dest) {
      ordered_.push_back(v);
    }
    return false;
  }

  const size_t concurrent_keys_;
  std::string chars_;

  std::mutex mutex_;
  int next_key_ = 0;
  std::vector<OnConflictKey> active_keys_;
  std::vector<TransactionInfo> committed_;
  std::vector<TransactionInfo*> ordered_;
  // Number of depth-first search run, used to filter visited vertexes.
  int query_ = 1;
};

}  // anonymous namespace

// Check that INSERT .. ON CONFLICT .. does not generate duplicate key errors.
void PgOnConflictTest::TestOnConflict(bool kill_master, const MonoDelta& duration) {
#ifndef NDEBUG
  constexpr int kWriters = RegularBuildVsSanitizers(15, 5);
#else
  constexpr int kWriters = 25;
#endif
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (k int PRIMARY KEY, v TEXT)"));

  std::atomic<int> processed(0);
  TestThreadHolder thread_holder;
  OnConflictHelper helper(3);
  for (int i = 0; i != kWriters; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &processed, &helper] {
      SetFlagOnExit set_flag_on_exit(&stop);
      auto connection = ASSERT_RESULT(Connect());
      char value[2] = "0";
      while (!stop.load(std::memory_order_acquire)) {
        int batch_size = RandomUniformInt(2, kMaxBatchSize);
        TransactionInfo transaction_info;
        transaction_info.batch_size = batch_size;
        bool ok = false;
        if (batch_size != 1) {
          ASSERT_OK(connection.Execute("START TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
        }
        auto se = ScopeExit([&connection, batch_size, &ok, &processed, &helper, &transaction_info] {
          if (batch_size != 1) {
            if (ok) {
              auto status = connection.Execute("COMMIT");
              if (status.ok()) {
                ++processed;
                helper.Committed(std::move(transaction_info));
                return;
              }
              auto msg = status.message().ToBuffer();
              if (msg.find("expired or aborted by a conflict") == std::string::npos &&
                  msg.find("Transaction aborted") == std::string::npos) {
                ASSERT_OK(status);
              }
            }
            ASSERT_OK(connection.Execute("ROLLBACK"));
          } else if (ok) {
            // To re-enable this we need to decrease the lower bound of batch_size to 1.
            ++processed;
          }
        });
        ok = true;
        for (int j = 0; j != batch_size; ++j) {
          auto key_and_appended_char = helper.RandomPair();
          Status status;
          auto& current_batch = transaction_info.batches[j];
          current_batch.key = key_and_appended_char.first;
          current_batch.append_char = key_and_appended_char.second;
          if (key_and_appended_char.second) {
            value[0] = key_and_appended_char.second;
            status = connection.ExecuteFormat(
                "INSERT INTO test (k, v) VALUES ($0, '$1') ON CONFLICT (K) DO "
                "UPDATE SET v = CONCAT(test.v, '$1')",
                key_and_appended_char.first, value);
          } else {
            auto result = connection.FetchFormat(
                "SELECT v FROM test WHERE k = $0", key_and_appended_char.first);
            if (!result.ok()) {
              status = result.status();
            } else {
              auto tuples = PQntuples(result->get());
              if (tuples == 1) {
                ASSERT_EQ(PQnfields(result->get()), 1);
                current_batch.read_value = ASSERT_RESULT(
                    GetString(result->get(), 0, 0));
              } else {
                ASSERT_EQ(tuples, 0);
              }
            }
          }
          if (status.ok()) {
            continue;
          }
          ok = false;
          if (TransactionalFailure(status)) {
            break;
          }
          auto msg = status.message().ToBuffer();
          if (msg.find("Snapshot too old: Snapshot too old.") != std::string::npos ||
              msg.find("Commit of expired transaction") != std::string::npos ||
              msg.find("Catalog Version Mismatch") != std::string::npos ||
              msg.find("Soft memory limit exceeded") != std::string::npos ||
              msg.find("timed out after deadline expired") != std::string::npos) {
            break;
          }

          ASSERT_OK(status);
        }
      }
    });
  }

  if (!kill_master) {
    thread_holder.WaitAndStop(duration.ToSteadyDuration());
  } else {
    // Every 15 seconds, pick a random master, then kill it if it is running, otherwise resume it.
    auto deadline = CoarseMonoClock::now() + duration;
    auto num_masters = cluster_->num_masters();
    while (!thread_holder.stop_flag().load(std::memory_order_acquire)) {
      MonoDelta left(deadline - CoarseMonoClock::now());
      if (left < MonoDelta::kZero) {
        break;
      }
      auto* master = cluster_->master(RandomUniformInt<size_t>(0, num_masters - 1));
      if (master->IsProcessAlive()) {
        std::this_thread::sleep_for(
            std::min(left, MonoDelta(20s) * kTimeMultiplier).ToSteadyDuration());
        LOG(INFO) << "Killing: " << master->uuid();
        master->Shutdown();
      } else {
        std::this_thread::sleep_for(
            std::min(left, MonoDelta(15s)).ToSteadyDuration());
        LOG(INFO) << "Resuming: " << master->uuid();
        ASSERT_OK(master->Start());
      }
      int live_masters = 0;
      for (size_t i = 0; i != num_masters; ++i) {
        if (cluster_->master(i)->IsProcessAlive()) {
          ++live_masters;
        }
      }
      LOG(INFO) << "Live masters: " << live_masters;
    }

    for (size_t i = 0; i != num_masters; ++i) {
      if (!cluster_->master(i)->IsProcessAlive()) {
        ASSERT_OK(cluster_->master(i)->Start());
      }
    }

    thread_holder.Stop();
  }

  for (;;) {
    auto res = conn.Fetch("SELECT * FROM test ORDER BY k");
    if (!res.ok()) {
      ASSERT_TRUE(TransactionalFailure(res.status())) << res.status();
      continue;
    }
    int cols = PQnfields(res->get());
    ASSERT_EQ(cols, 2);
    int rows = PQntuples(res->get());
    for (int i = 0; i != rows; ++i) {
      auto key = GetInt32(res->get(), i, 0);
      auto value = GetString(res->get(), i, 1);
      LOG(INFO) << "  " << key << ": " << value;
    }
    LOG(INFO) << "Total processed: " << processed.load(std::memory_order_acquire);
    break;
  }

  helper.Report();
}

TEST_F(PgOnConflictTest, YB_DISABLE_TEST_IN_TSAN(OnConflict)) {
  TestOnConflict(false /* kill_master */, 120s);
}

TEST_F(PgOnConflictTest, YB_DISABLE_TEST_IN_TSAN(OnConflictWithKillMaster)) {
  TestOnConflict(true /* kill_master */, 180s);
}

// When auto-commit fails block state switched to TBLOCK_ABORT.
// But correct state in this case is TBLOCK_DEFAULT.
// https://github.com/YugaByte/yugabyte-db/commit/73e966e5735efc21bf2ad43f9d961a488afbe050
TEST_F(PgOnConflictTest, YB_DISABLE_TEST_IN_TSAN(NoTxnOnConflict)) {
  constexpr int kWriters = 5;
  constexpr int kKeys = 20;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (k int PRIMARY KEY, v TEXT)"));

  TestThreadHolder thread_holder;
  for (int i = 0; i != kWriters; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
      SetFlagOnExit set_flag_on_exit(&stop);
      auto connection = ASSERT_RESULT(Connect());
      char value[2] = "0";
      while (!stop.load(std::memory_order_acquire)) {
        int key = RandomUniformInt(1, kKeys);
        value[0] = RandomUniformInt<uint8_t>('A', 'Z');
        auto status = connection.ExecuteFormat(
            "INSERT INTO test (k, v) VALUES ($0, '$1') ON CONFLICT (K) DO "
            "UPDATE SET v = CONCAT(test.v, '$1')",
            key, value);
        if (status.ok() || TransactionalFailure(status)) {
          continue;
        }
        ASSERT_OK(status);
      }
    });
  }

  thread_holder.WaitAndStop(30s);
  LogResult(ASSERT_RESULT(conn.Fetch("SELECT * FROM test ORDER BY k")).get());
}

TEST_F(PgOnConflictTest, YB_DISABLE_TEST_IN_TSAN(ValidSessionAfterTxnCommitConflict)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test (k int PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES(1)"));
  auto extra_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(extra_conn.Execute("INSERT INTO test VALUES(1)"));
  ASSERT_NOK(conn.Execute("COMMIT"));
  // Check connection is in valid state after failed COMMIT
  auto result_ptr = ASSERT_RESULT(conn.Fetch("SELECT * FROM test"));
  auto value = ASSERT_RESULT(GetInt32(result_ptr.get(), 0, 0));
  ASSERT_EQ(value, 1);
}

// When a single statement Read Committed transaction executed outside of a begin block faces a
// kConflict, PG backend could sleep for a while delaying the next rpc which restarts the txn.
// PgClientSession early aborts such transactions before returning kConflict to the PG backend
// so as to progress other transactions waiting on a set of locks that could have been acquired
// by the former transaction. The below test asserts that such transactions are early aborted at
// PgClientSession.
//
// Note: The test is intended to be run with Fail-On-Conflict conflict management policy because
// we only sleep between query layer retries in Fail-on-Conflict mode.
TEST_F_EX(PgOnConflictTest, EarlyAbortSingleStatementReadCommittedTxn, PgFailOnConflictTest) {
  constexpr int kClients = 3;
  constexpr int kIters = 100;
  constexpr int kStatementTimeoutMs = 10000 * kTimeMultiplier;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE foo(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO foo SELECT generate_series(1, 10), 0"));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn.Fetch("SELECT * FROM foo WHERE k=1 FOR UPDATE"));

  TestThreadHolder thread_holder;
  for (int i = 0; i < kClients; i++) {
    thread_holder.AddThreadFunctor([&]{
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.ExecuteFormat("SET statement_timeout=$0", kStatementTimeoutMs * 2));
      while (!thread_holder.stop_flag()) {
        // RPCs to different tablets would be made in parallel, so the transaction would obtain
        // locks at a few tablets and kConflict at the rest. PG backend would retry the transaction
        // for a couple of times with sleeps in between, before the statement timesout.
        ASSERT_NOK(conn.Execute("UPDATE foo SET v=1 WHERE k>=1"));
      }
    });
  }

  thread_holder.AddThreadFunctor([&]{
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("SET statement_timeout=$0", kStatementTimeoutMs / 2));
    for (int i = 0; i < kIters; i++) {
      // Since PgClientSession would early abort the above conflicting transactions before the sleep
      // amidst retries in the backend, this statement should get enough window for the updates.
      ASSERT_OK(conn.Execute("UPDATE foo SET v=1 WHERE k>=2"));
    }
  });
  thread_holder.Wait(30s * kTimeMultiplier);
}

} // namespace pgwrapper
} // namespace yb

// Copyright (c) YugabyteDB, Inc.
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

#include <regex>
#include <boost/algorithm/string.hpp>

#include "yb/common/pgsql_error.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/to_stream.h"

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(TEST_docdb_log_write_batches);
DECLARE_bool(TEST_no_schedule_remove_intents);
DECLARE_bool(TEST_skip_prefix_locks_invariance_check);
DECLARE_bool(TEST_skip_prefix_locks_skip_fast_mode_removal);
DECLARE_bool(cleanup_intents_sst_files);
DECLARE_bool(skip_prefix_locks);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_use_packed_row_v2);
DECLARE_bool(ysql_yb_enable_advisory_locks);
DECLARE_int32(replication_factor);
DECLARE_uint64(TEST_wait_inactive_transaction_cleanup_sleep_ms);

METRIC_DECLARE_counter(fast_mode_si_transactions);
METRIC_DECLARE_counter(fast_mode_rr_rc_transactions);
METRIC_DECLARE_counter(slow_mode_si_transactions);

using namespace std::literals;

namespace yb::pgwrapper {

YB_DEFINE_ENUM(PackingMode, (kV1)(kV2));
using PgSkipPrefixLockTestParams = std::tuple<PackingMode>;

PackingMode GetPackingMode(const PgSkipPrefixLockTestParams& params) {
  return std::get<0>(params);
}

class PgSkipPrefixLockTest : public PgMiniTestBase,
                             public testing::WithParamInterface<PgSkipPrefixLockTestParams> {
 public:

  void EnableSkipPrefixLock() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_skip_prefix_locks) = true;
    LOG(INFO) << "EnableSkipPrefixLock";
    RestartTServer();
  }

  std::vector<uint64_t> GetNumFastAndSlowModeTransactionsMetrics() {
    uint64_t fast_si_count = 0;
    uint64_t fast_rr_rc_count = 0;
    uint64_t slow_si_count = 0;

    for (const auto& mini_tablet_server : cluster_->mini_tablet_servers()) {
      auto peers = mini_tablet_server->server()->tablet_manager()->GetTabletPeers();
      for (const auto& peer : peers) {
        auto tablet = peer->shared_tablet_maybe_null();
        if (!tablet) {
          continue;
        }
        auto fast_si_counter = METRIC_fast_mode_si_transactions.Instantiate(
            tablet->GetTabletMetricsEntity());
        auto fast_rr_rc_counter = METRIC_fast_mode_rr_rc_transactions.Instantiate(
            tablet->GetTabletMetricsEntity());
        auto slow_si_counter = METRIC_slow_mode_si_transactions.Instantiate(
            tablet->GetTabletMetricsEntity());

        fast_si_count += fast_si_counter->value();
        fast_rr_rc_count += fast_rr_rc_counter->value();
        slow_si_count += slow_si_counter->value();
      }
    }

    return {fast_si_count, fast_rr_rc_count, slow_si_count};
  }

  // Get the current number of txns in fast mode maps.
  std::pair<uint64_t, uint64_t> GetNumFastModeTransactions() {
    uint64_t si_count = 0;
    uint64_t rr_rc_count = 0;

    for (const auto& mini_tablet_server : cluster_->mini_tablet_servers()) {
      auto peers = mini_tablet_server->server()->tablet_manager()->GetTabletPeers();
      for (const auto& peer : peers) {
        auto tablet = peer->shared_tablet_maybe_null();
        if (!tablet) {
          continue;
        }
        if (tablet->table_type() == TableType::PGSQL_TABLE_TYPE) {
          auto pair = tablet->transaction_participant()->GetNumFastModeTransactions();
          si_count += pair.first;
          rr_rc_count += pair.second;
        }
      }
    }
    return std::make_pair(si_count, rr_rc_count);
  }

 protected:
  void SetUp() override {
    // Run the test first with default skip_prefix_locks value
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_skip_prefix_locks) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_log_write_batches) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_prefix_locks_invariance_check) = true;
    auto packing_mode = GetPackingMode(GetParam());
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = packing_mode == PackingMode::kV2;

    PgMiniTestBase::SetUp();
  }

  Result<PGConn> PrepareTable(const std::string& table,
                              bool create_index = false,
                              bool insert_row = true) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("drop table if exists test"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "create table $0 (h1 text, h2 text, r1 text, r2 text, v1 text, v2 text,"
        "primary key((h1, h2), r1, r2)) SPLIT INTO 1 TABLETS", table));
    if (create_index) {
      RETURN_NOT_OK(conn.ExecuteFormat("create index $0_idx1 on $0 (v1 asc)", table));
      RETURN_NOT_OK(conn.ExecuteFormat("create index $0_idx2 on $0 (v2)", table));
      num_tablets_ = 3;
    }

    if (insert_row) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "insert into $0 values ('1', '2', '3', '4', 'a', 'b')", table));
    }
    return conn;
  }

  Result<PGConn> PrepareHashOnlyTable(const std::string& table, bool insert_row = true) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("drop table if exists test"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "create table $0 (h1 text, v1 text, "
        "primary key(h1 hash)) SPLIT INTO 1 TABLETS", table));
    if (insert_row) {
      RETURN_NOT_OK(conn.ExecuteFormat("insert into $0 values ('1', 'b')", table));
    }
    return conn;
  }

  Result<PGConn> PrepareRangeOnlyTable(const std::string& table, bool insert_row = true) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("drop table if exists test"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "create table $0 (h1 text, v1 text, primary key(h1 asc))", table));
    if (insert_row) {
      RETURN_NOT_OK(conn.ExecuteFormat("insert into $0 values ('1', 'b')", table));
    }
    return conn;
  }

  Result<PGConn> PrepareColocatedTable(const std::string& table, bool create_db = true,
                                       bool insert_row = true) {
    if (create_db) {
      auto setup_conn = VERIFY_RESULT(Connect());
      RETURN_NOT_OK(setup_conn.ExecuteFormat(
          "CREATE DATABASE test_colo_db WITH colocation = true"));
    }
    auto conn = VERIFY_RESULT(ConnectToDB("test_colo_db"));
    RETURN_NOT_OK(conn.Execute("drop table if exists test"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "create table $0 (r1 text, r2 text, r3 text, r4 text, v1 text, v2 text,"
        "primary key(r1 asc, r2 asc, r3 asc, r4 asc)) WITH (COLOCATION_ID = 20000)", table));
    if (insert_row) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "insert into $0 values ('1', '2', '3', '4', 'a', 'b')", table));
    }
    return conn;
  }

  void VerifyFastModeTransactions(IsolationLevel level,
                                  bool skip_prefix_locks,
                                  const std::vector<uint64_t>& metrics_before,
                                  int num_tablets,
                                  bool transaction_op,
                                  int num_op) {
    if (level == IsolationLevel::NON_TRANSACTIONAL) {
      auto metric_after = GetNumFastAndSlowModeTransactionsMetrics();
      ASSERT_EQ(metrics_before, metric_after);
      return;
    }

    uint64_t expected_fast_si =
        level == IsolationLevel::SERIALIZABLE_ISOLATION && !skip_prefix_locks ? 1 : 0;
    uint64_t fast_expected_rr_rc =
        level != IsolationLevel::SERIALIZABLE_ISOLATION && skip_prefix_locks ? 1 : 0;
    uint64_t expected_slow_si =
        level == IsolationLevel::SERIALIZABLE_ISOLATION && skip_prefix_locks ? 1 : 0;

    int num_peer_tablets = num_tablets * FLAGS_replication_factor;

    // For the operations in a txn, verify the current fast mode maps. Once the operation returns,
    // at least the leader has the txn tracked in fast mode maps. For the operations which is not in
    // a txn (e.g., truncate colocated table), the fast maps may have been cleared before the
    // request returns, so we skip the fast mode map check.
    if (transaction_op) {
      auto pair = GetNumFastModeTransactions();
      ASSERT_GE(pair.first, expected_fast_si);
      ASSERT_GE(pair.second, fast_expected_rr_rc);
      // After wait for a while, all peers should have the txn tracked in fast mode maps.
      SleepFor(3s);
      pair = GetNumFastModeTransactions();
      ASSERT_EQ(expected_fast_si * num_peer_tablets, pair.first);
      ASSERT_EQ(fast_expected_rr_rc * num_peer_tablets, pair.second);
    }

    // verify metrics
    std::vector<uint64_t> metrics_expected_diff = {expected_fast_si * num_peer_tablets,
        fast_expected_rr_rc * num_peer_tablets, expected_slow_si * num_peer_tablets};
    // each fast mode op increments the counter by 1
    metrics_expected_diff[0] += expected_fast_si > 0 ? num_op : 0;
    metrics_expected_diff[1] += fast_expected_rr_rc > 0 ? num_op : 0;
    auto metric_after = GetNumFastAndSlowModeTransactionsMetrics();
    auto fast_si_diff = metric_after[0] - metrics_before[0];
    auto fast_rr_rc_diff = metric_after[1] - metrics_before[1];
    auto slow_si_diff = metric_after[2] - metrics_before[2];
    ASSERT_EQ(metrics_expected_diff[0], fast_si_diff);
    ASSERT_EQ(metrics_expected_diff[1], fast_rr_rc_diff);
    ASSERT_EQ(metrics_expected_diff[2], slow_si_diff);
  }

  void TestOperation(PGConn& conn, IsolationLevel level, const std::string& query,
      bool skip_prefix_locks, bool transaction_op = true, int num_op = 1) {
    LOG(INFO) << "level:" << level << " skip_prefix_locks:" << skip_prefix_locks;
    if (transaction_op) {
      ASSERT_OK(conn.StartTransaction(level));
    }
    auto metrics_before = GetNumFastAndSlowModeTransactionsMetrics();
    if (query.find("select") != std::string::npos) {
      ASSERT_OK(conn.Fetch(query));
    } else {
      ASSERT_OK(conn.Execute(query));
    }
    VerifyFastModeTransactions(level, skip_prefix_locks, metrics_before, num_tablets_,
                               transaction_op, num_op);

    if (transaction_op) {
      if (RandomUniformBool()) {
        LOG(INFO) << "commit transaction";
        ASSERT_OK(conn.CommitTransaction());
        SleepFor(5s);
      } else {
        LOG(INFO) << "abort transaction";
        ASSERT_OK(conn.RollbackTransaction());
      }
    }

    // After commit or abort, both fast mode maps should be empty
    SleepFor(5s);
    auto pair = GetNumFastModeTransactions();
    ASSERT_EQ(0, pair.first);
    ASSERT_EQ(0, pair.second);
  }

  void RestartTServer(const std::vector<size_t>& idxs = {0, 1, 2}, bool restart = true) {
    LOG(INFO) << "restart tservers with indices: " << AsString(idxs);
    for (size_t idx : idxs) {
      ASSERT_LT(idx, cluster_->mini_tablet_servers().size())
          << "TServer index " << idx << " is out of range";
      if (restart) {
        ASSERT_OK(cluster_->mini_tablet_servers()[idx]->Restart());
      } else {
        ASSERT_OK(cluster_->mini_tablet_servers()[idx]->RestartStoppedServer());
      }
    }
    for (size_t idx : idxs) {
      ASSERT_OK(cluster_->mini_tablet_servers()[idx]->WaitStarted());
    }
    LOG(INFO) << "restart tservers done";
  }

  IsolationLevel GetFastLevel(bool skip_prefix_lock) {
    return skip_prefix_lock ? IsolationLevel::SNAPSHOT_ISOLATION
                            : IsolationLevel::SERIALIZABLE_ISOLATION;
  }

  IsolationLevel GetSlowLevel(bool skip_prefix_lock) {
    return skip_prefix_lock ? IsolationLevel::SERIALIZABLE_ISOLATION
                            : IsolationLevel::SNAPSHOT_ISOLATION;
  }

  // The test can reproduce the case where two fast-mode maps have entries simultaneously.
  void TestEnableDisableWithTxnLoad(bool initial_skip_prefix_lock) {
    LOG(INFO) << "initial_skip_prefix_lock:" << initial_skip_prefix_lock;
    const std::string query1 = "insert into test values ('1', '2', '3', '4', 'a', 'b')";
    const std::string query2 = "insert into test values ('1', '2', '33', '4', 'a', 'b')";

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_skip_prefix_locks) = initial_skip_prefix_lock;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_schedule_remove_intents) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_intents_sst_files) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_wait_inactive_transaction_cleanup_sleep_ms) = 3000;

    // step 1: Insert a row with fast-mode.
    RestartTServer();
    auto conn = ASSERT_RESULT(PrepareTable("test", /*create_index=*/ false, /*insert_row=*/ false));
    ASSERT_OK(conn.StartTransaction(GetFastLevel(FLAGS_skip_prefix_locks)));
    ASSERT_OK(conn.Execute(query1));
    ASSERT_OK(conn.CommitTransaction());

    // step 2: start a new txn with slow mode and abort it.
    ASSERT_OK(conn.StartTransaction(GetSlowLevel(FLAGS_skip_prefix_locks)));
    ASSERT_OK(conn.Execute(query2));
    ASSERT_OK(cluster_->FlushTablets());
    ASSERT_OK(conn.RollbackTransaction());
    SleepFor(5s);

    // step 3: change skip prefix lockis and restart the first 2 tservrs. In the leadeer the first
    // txn will be loaded and then cleaned up. Start another fast txn.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_skip_prefix_locks) = !initial_skip_prefix_lock;
    ASSERT_OK(cluster_->FlushTablets());
    cluster_->mini_tablet_server(2)->Shutdown();
    RestartTServer({0, 1});
    conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.StartTransaction(GetFastLevel(FLAGS_skip_prefix_locks)));
    LOG(INFO) << "Sending query";
    ASSERT_OK(conn.Execute(query2));
    ASSERT_OK(conn.CommitTransaction());
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_prefix_locks_skip_fast_mode_removal) = true;
    // step 4: Restart tserver 2. In tserver 2, both fast mode maps may contain transactions at the
    // same time, but they should not have active transactions simultaneously.
    RestartTServer({2}, false);
    SleepFor(10s);
  }

  void RunTest(const std::string query, bool insert_row = true, bool create_index = false) {
    const int num_op = create_index ? 3 : 1;
    // skip prefix lock disabled.
    auto conn = ASSERT_RESULT(PrepareTable("test", create_index, insert_row));
    TestOperation(conn, IsolationLevel::SNAPSHOT_ISOLATION, query, false, true, num_op);
    conn = ASSERT_RESULT(PrepareTable("test", create_index, insert_row));
    TestOperation(conn, IsolationLevel::SERIALIZABLE_ISOLATION, query, false, true, num_op);

    // skip prefix lock enabled
    EnableSkipPrefixLock();
    conn = ASSERT_RESULT(PrepareTable("test", create_index, insert_row));
    TestOperation(conn, IsolationLevel::SNAPSHOT_ISOLATION, query, true, true, num_op);
    conn = ASSERT_RESULT(PrepareTable("test", create_index, insert_row));
    TestOperation(conn, IsolationLevel::SERIALIZABLE_ISOLATION, query, true, true, num_op);
  }

 private:
  int num_tablets_ = 1;
};

std::string TestParamToString(
    const testing::TestParamInfo<PgSkipPrefixLockTestParams>& param_info) {
  return ToString(GetPackingMode(param_info.param)).substr(1);
}

INSTANTIATE_TEST_SUITE_P(, \
    PgSkipPrefixLockTest, \
    testing::Combine(testing::ValuesIn(kPackingModeArray)), \
    TestParamToString);

TEST_P(PgSkipPrefixLockTest, Insert) {
  const std::string query = "insert into test values ('1', '2', '3', '4', 'a', 'b')";
  RunTest(query, /*insert_row=*/ false);
}

TEST_P(PgSkipPrefixLockTest, Delete) {
  const std::string query =
      "delete from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4'";
  RunTest(query);
}

TEST_P(PgSkipPrefixLockTest, Update) {
  const std::string query =
      "update test set v1='b' where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4'";
  RunTest(query);
}

TEST_P(PgSkipPrefixLockTest, SelectForUpdate) {
  const std::string query =
      "select * from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4' for update";
  RunTest(query);
}

TEST_P(PgSkipPrefixLockTest, AdvisoryLock) {
  auto conn = ASSERT_RESULT(PrepareTable("test"));
  // Enable advisory locks
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_advisory_locks) = true;
  const std::string query = "select pg_advisory_lock(10)";

  // skip prefix lock disabled.
  TestOperation(conn, IsolationLevel::READ_COMMITTED, query, false, /*transaction_op*/ false);

  // skip prefix lock enabled. For advisory lock op, skip_prefix_locks is always disabled even the
  // gflag is enabled.
  EnableSkipPrefixLock();
  conn = ASSERT_RESULT(PrepareTable("test"));
  TestOperation(conn, IsolationLevel::READ_COMMITTED, query, false, /*transaction_op*/ false);
}

TEST_P(PgSkipPrefixLockTest, InsertWithIndex) {
  const std::string query = "insert into test values ('1', '2', '3', '4', 'a', 'b')";
  RunTest(query, /*insert_row=*/ false, /*create_index=*/ true);
}

// For truncate colocated table, the implicit isolation level is always snapshot.
TEST_P(PgSkipPrefixLockTest, TruncateColocatedTable) {
  auto conn = ASSERT_RESULT(PrepareColocatedTable("test"));
  const std::string query = "truncate table test";

  // skip prefix lock disabled.
  TestOperation(conn, IsolationLevel::SNAPSHOT_ISOLATION, query, false, /*transaction_op=*/ false);

  // skip prefix lock enabled
  EnableSkipPrefixLock();
  conn = ASSERT_RESULT(PrepareColocatedTable("test", /*create_db=*/ false));
  TestOperation(conn, IsolationLevel::SNAPSHOT_ISOLATION, query, true, /*transaction_op=*/ false);
}

// Test enable/disable skip prefix lock during loading.
TEST_P(PgSkipPrefixLockTest, YB_LINUX_RELEASE_ONLY_TEST(EnableDisableWithTxnLoad)) {
  TestEnableDisableWithTxnLoad(/*initial_skip_prefix_lock=*/ false);
  TestEnableDisableWithTxnLoad(/*initial_skip_prefix_lock=*/ true);
}

TEST_P(PgSkipPrefixLockTest, SelectForKeyShare) {
  const std::string query =
      "select * from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4' for key share";
  RunTest(query);
}

TEST_P(PgSkipPrefixLockTest, FastPathInsert) {
  auto conn = ASSERT_RESULT(PrepareTable("test", /*create_index=*/ false, /*insert_row=*/ false));
  const std::string query = "insert into test values ('1', '2', '3', '4', 'a', 'b')";

  // skip prefix lock disabled
  TestOperation(conn, IsolationLevel::NON_TRANSACTIONAL, query, false, /*transaction_op*/ false);

  // skip prefix lock enabled
  EnableSkipPrefixLock();
  conn = ASSERT_RESULT(PrepareTable("test", /*create_index=*/ false, /*insert_row=*/ false));
  TestOperation(conn, IsolationLevel::NON_TRANSACTIONAL, query, true, /*transaction_op*/ false);
}

}  // namespace yb::pgwrapper

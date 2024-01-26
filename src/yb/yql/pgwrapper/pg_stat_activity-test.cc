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

#include <chrono>
#include <map>
#include <optional>
#include <vector>

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/uuid.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

namespace yb::pgwrapper {
namespace {

constexpr auto* kTableName = "t";
constexpr int kSimulateSlowDDLSecs = 3;

struct TxnInfo {
  TxnInfo(int32_t pid_, Uuid txn_id_) : pid(pid_), txn_id(txn_id_) {}

  int32_t pid;
  Uuid txn_id;
};

class PgStatActivityTest : public LibPqTestBase {
 protected:
  void SetUp() override {
    LibPqTestBase::SetUp();
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName));
  }

  int GetNumTabletServers() const override { return 1; }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--replication_factor=1");
    LibPqTestBase::UpdateMiniClusterOptions(options);
  }

  static Result<TxnInfo> GetTransactionInfo(PGConn* conn) {
    auto opt_txn_id =
        VERIFY_RESULT(conn->FetchValue<std::optional<Uuid>>("SELECT yb_get_current_transaction()"));
    return TxnInfo{PQbackendPID(conn->get()), opt_txn_id.value_or(Uuid::Nil())};
  }

  static Result<Uuid> GetTransactionId(PGConn* conn) {
    return VERIFY_RESULT(GetTransactionInfo(conn)).txn_id;
  }

  static Result<bool> HasTransactionId(PGConn* conn) {
    return !VERIFY_RESULT(GetTransactionId(conn)).IsNil();
  }

  static Result<std::vector<TxnInfo>> FetchTxnInfoFromStatActivity(PGConn* conn) {
    auto stat_result_holder = VERIFY_RESULT(conn->Fetch(
        "SELECT pid, yb_backend_xid FROM pg_stat_activity WHERE yb_backend_xid IS NOT NULL"));
    auto* stat_result = stat_result_holder.get();
    std::vector<TxnInfo> result;
    const auto rows_count = PQntuples(stat_result);
    result.reserve(rows_count);
    for (int i = 0; i < rows_count; ++i) {
      result.emplace_back(
          VERIFY_RESULT(GetValue<int32_t>(stat_result, i, 0)),
          VERIFY_RESULT(GetValue<Uuid>(stat_result, i, 1)));
    }
    return result;
  }
};

class PgStatActivityDelayTest : public PgStatActivityTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back(
        Format("--TEST_simulate_slow_table_create_secs=$0", kSimulateSlowDDLSecs));
    PgStatActivityTest::UpdateMiniClusterOptions(options);
  }
};

}  // namespace

// The test checks that yb_get_current_transaction returns non-null transaction id in case
// distribute transaction was created.
TEST_F(PgStatActivityTest, CurrentTransaction) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_FALSE(ASSERT_RESULT(HasTransactionId(&conn)));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_FALSE(ASSERT_RESULT(HasTransactionId(&conn)));
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kTableName));
  ASSERT_FALSE(ASSERT_RESULT(HasTransactionId(&conn)));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1)", kTableName));
  ASSERT_TRUE(ASSERT_RESULT(HasTransactionId(&conn)));
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_FALSE(ASSERT_RESULT(HasTransactionId(&conn)));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 FOR KEY SHARE", kTableName));
  ASSERT_TRUE(ASSERT_RESULT(HasTransactionId(&conn)));
  ASSERT_OK(conn.RollbackTransaction());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kTableName));
  ASSERT_TRUE(ASSERT_RESULT(HasTransactionId(&conn)));
  ASSERT_OK(conn.RollbackTransaction());
}

// The test checks that the pg_stat_activity function produces result with valid
// pid <--> transaction_id mapping.
TEST_F(PgStatActivityTest, AllBackendsTransaction) {
  constexpr size_t kConnCount = 10;
  using PidToTxnMapping = std::map<int32_t, Uuid>;
  PidToTxnMapping pid_to_txn;
  std::vector<PGConn> conns;
  conns.reserve(kConnCount);

  for (size_t i = 0; i < kConnCount; ++i) {
    conns.push_back(ASSERT_RESULT(Connect()));
    auto& conn = conns.back();
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, i));
    auto txn_info = ASSERT_RESULT(GetTransactionInfo(&conn));
    pid_to_txn.emplace(txn_info.pid, txn_info.txn_id);
    ASSERT_NE(txn_info.txn_id, Uuid::Nil());
  }

  ASSERT_EQ(pid_to_txn.size(), kConnCount);
  auto conn = ASSERT_RESULT(Connect());
  PidToTxnMapping stat_pid_to_txn;
  const auto txn_infos = ASSERT_RESULT(FetchTxnInfoFromStatActivity(&conn));
  for (const auto& info : txn_infos) {
    stat_pid_to_txn.emplace(info.pid, info.txn_id);
  }
  ASSERT_EQ(stat_pid_to_txn, pid_to_txn);
}

// The test checks that DDL transaction id has higher priority over DML transaction in the output
// of the pg_stat_activity function.
TEST_F(PgStatActivityTest, DDLInsideDMLTransaction) {
  auto conn = ASSERT_RESULT(Connect());
  auto aux_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName));
  const auto dml_txn_id = ASSERT_RESULT(GetTransactionId(&conn));
  Uuid ddl_txn_id;
  {
    CountDownLatch latch(1);
    TestThreadHolder threads;
    threads.AddThreadFunctor([&aux_conn, &latch, &dml_txn_id, result = &ddl_txn_id] {
      auto info = ASSERT_RESULT(FetchTxnInfoFromStatActivity(&aux_conn));
      ASSERT_EQ(info.size(), 1);
      ASSERT_EQ(info.back().txn_id, dml_txn_id);
      latch.CountDown();
      ASSERT_OK(WaitFor(
          [&aux_conn, &dml_txn_id, result]() -> Result<bool> {
            auto info = VERIFY_RESULT(FetchTxnInfoFromStatActivity(&aux_conn));
            SCHECK_EQ(info.size(), 1, IllegalState, "Unexpected size");
            *result = info.back().txn_id;
            return *result != dml_txn_id;
          },
          5s, "Wait for txn id switch"));
    });
    latch.Wait();
    ASSERT_OK(conn.Execute("CREATE TABLE tmp AS SELECT c FROM (SELECT 1 as c, pg_sleep(5)) AS s"));
  }
  ASSERT_FALSE(ddl_txn_id.IsNil());
  ASSERT_NE(ddl_txn_id, dml_txn_id);
  ASSERT_EQ(dml_txn_id, ASSERT_RESULT(GetTransactionId(&conn)));
  ASSERT_OK(conn.RollbackTransaction());
  ASSERT_TRUE(ASSERT_RESULT(GetTransactionId(&conn)).IsNil());
}

// The test checks that the pg_stat_activity function does not get blocked
// behind a long running DDL operation.
TEST_F(PgStatActivityDelayTest, SlowDDLOperation) {
  constexpr auto* kDdlTableName = "kDdl";
  auto conn = ASSERT_RESULT(Connect());
  auto aux_conn = ASSERT_RESULT(Connect());
  CountDownLatch latch(1);
  TestThreadHolder threads;
  threads.AddThreadFunctor([&aux_conn, &latch, kDdlTableName] {
    ASSERT_OK(aux_conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kDdlTableName));
    latch.CountDown();
  });

  // Sleep to ensure that table creation thread has acquired session lock
  const auto kSleepDuration = MonoDelta::FromMilliseconds(1000);
  SleepFor(kSleepDuration);

  const auto start = MonoTime::Now();
  ASSERT_RESULT(conn.Fetch("SELECT pid, yb_backend_xid FROM pg_stat_activity"));
  const auto end = MonoTime::Now();

  ASSERT_LT((end - start).ToSeconds(), 1);
  latch.Wait();
}

}  // namespace yb::pgwrapper

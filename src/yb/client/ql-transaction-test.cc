//
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
//

#include <thread>

#include <boost/optional/optional.hpp>

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_rpc.h"
#include "yb/client/transaction_manager.h"

#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/server/hybrid_clock.h"
#include "yb/server/test_clock.h"

#include "yb/tablet/transaction_coordinator.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/random_util.h"

using namespace std::literals; // NOLINT

DECLARE_uint64(transaction_timeout_usec);
DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_uint64(transaction_table_default_num_tablets);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_bool(transaction_disable_heartbeat_in_tests);
DECLARE_double(transaction_ignore_applying_probability_in_tests);
DECLARE_uint64(transaction_check_interval_usec);
DECLARE_uint64(transaction_read_skew_usec);

namespace yb {
namespace client {

namespace {

constexpr size_t kNumRows = 5;
const auto kTransactionApplyTime = NonTsanVsTsan(3s, 15s);
const std::string kKeyColumn = "k";
const std::string kValueColumn = "v";

YB_DEFINE_ENUM(WriteOpType, (INSERT)(UPDATE));

// We use different sign to distinguish inserted and updated values for testing.
int32_t GetMultiplier(const WriteOpType op_type) {
  switch (op_type) {
    case WriteOpType::INSERT:
      return 1;
    case WriteOpType::UPDATE:
      return -1;
  }
  FATAL_INVALID_ENUM_VALUE(WriteOpType, op_type);
}

QLWriteRequestPB::QLStmtType GetQlStatementType(const WriteOpType op_type) {
  switch (op_type) {
    case WriteOpType::INSERT:
      return QLWriteRequestPB::QL_STMT_INSERT;
    case WriteOpType::UPDATE:
      return QLWriteRequestPB::QL_STMT_UPDATE;
  }
  FATAL_INVALID_ENUM_VALUE(WriteOpType, op_type);
}

int32_t KeyForTransactionAndIndex(size_t transaction, size_t index) {
  return static_cast<int32_t>(transaction * 10 + index);
}

int32_t ValueForTransactionAndIndex(size_t transaction, size_t index, const WriteOpType op_type) {
  return static_cast<int32_t>(transaction * 10 + index + 2) * GetMultiplier(op_type);
}

void SetIgnoreApplyingProbability(double value) {
  SetAtomicFlag(value, &FLAGS_transaction_ignore_applying_probability_in_tests);
}

void SetDisableHeartbeatInTests(bool value) {
  SetAtomicFlag(value, &FLAGS_transaction_disable_heartbeat_in_tests);
}

void DisableApplyingIntents() {
  SetIgnoreApplyingProbability(1.0);
}

void CommitAndResetSync(YBTransactionPtr *txn) {
  CountDownLatch latch(1);
  (*txn)->Commit([&latch](const Status& status) {
    ASSERT_OK(status);
    latch.CountDown(1);
  });
  txn->reset();
  latch.Wait();
}

} // namespace

#define VERIFY_ROW(...) VerifyRow(__LINE__, __VA_ARGS__)

class QLTransactionTest : public QLDmlTestBase {
 protected:
  void SetUp() override {
    QLDmlTestBase::SetUp();

    YBSchemaBuilder builder;
    builder.AddColumn(kKeyColumn)->Type(INT32)->HashPrimaryKey()->NotNull();
    builder.AddColumn(kValueColumn)->Type(INT32);
    TableProperties table_properties;
    table_properties.SetTransactional(true);
    builder.SetTableProperties(table_properties);

    ASSERT_OK(table_.Create(kTableName, CalcNumTablets(3), client_.get(), &builder));

    FLAGS_transaction_table_default_num_tablets = 1;
    FLAGS_log_segment_size_bytes = 128;
    FLAGS_log_min_seconds_to_retain = 5;
    server::ClockPtr clock(
        clock_ = new server::TestClock(server::ClockPtr(new server::HybridClock)));
    ASSERT_OK(clock->Init());
    HybridTime::TEST_SetBaseTimeForToString(clock->Now().GetPhysicalValueMicros());
    transaction_manager_.emplace(client_, clock);
  }

  shared_ptr<YBSession> CreateSession(const YBTransactionPtr& transaction = nullptr) {
    auto session = std::make_shared<YBSession>(client_, transaction);
    session->SetTimeout(NonTsanVsTsan(5s, 20s));
    return session;
  }

  // Insert/update a full, single row, equivalent to the statement below. Return a YB write op that
  // has been applied.
  // op_type == WriteOpType::INSERT: insert into t values (key, value);
  // op_type == WriteOpType::UPDATE: update t set v=value where k=key;
  Result<shared_ptr<YBqlWriteOp>> WriteRow(
      const YBSessionPtr& session, int32_t key, int32_t value,
      const WriteOpType op_type = WriteOpType::INSERT) {
    VLOG(4) << "Calling WriteRow key=" << key << " value=" << value << " op_type="
            << yb::ToString(op_type);
    const QLWriteRequestPB::QLStmtType stmt_type = GetQlStatementType(op_type);
    const auto op = table_.NewWriteOp(stmt_type);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table_.AddInt32ColumnValue(req, kValueColumn, value);
    RETURN_NOT_OK(session->Apply(op));
    if (op->response().status() != QLResponsePB::YQL_STATUS_OK) {
      return STATUS_FORMAT(QLError, "Error writing row: $0", op->response().error_message());
    }
    return op;
  }

  Result<shared_ptr<YBqlWriteOp>> UpdateRow(
      const YBSessionPtr& session, int32_t key, int32_t value) {
    return WriteRow(session, key, value, WriteOpType::UPDATE);
  }

  void WriteRows(
      const YBSessionPtr& session, size_t transaction = 0,
      const WriteOpType op_type = WriteOpType::INSERT) {
    for (size_t r = 0; r != kNumRows; ++r) {
      ASSERT_OK(WriteRow(
          session,
          KeyForTransactionAndIndex(transaction, r),
          ValueForTransactionAndIndex(transaction, r, op_type),
          op_type));
    }
  }

  // Select the specified columns of a row using a primary key, equivalent to the select statement
  // below. Return a YB read op that has been applied.
  //   select <columns...> from t where h1 = <h1> and h2 = <h2> and r1 = <r1> and r2 = <r2>;
  Result<int32_t> SelectRow(const YBSessionPtr& session, int32_t key,
                            const std::string& column = kValueColumn) {
    const shared_ptr<YBqlReadOp> op = table_.NewReadOp();
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table_.AddColumns({column}, req);
    auto status = session->Apply(op);
    if (status.IsIOError()) {
      for (const auto& error : session->GetPendingErrors()) {
        LOG(WARNING) << "Error: " << error->status() << ", op: " << error->failed_op().ToString();
      }
    }
    RETURN_NOT_OK(status);
    if (op->response().status() != QLResponsePB::YQL_STATUS_OK) {
      return STATUS(QLError,
                    op->response().error_message(),
                    Slice(),
                    static_cast<int64_t>(ql::QLStatusToErrorCode(op->response().status())));
    }
    auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
    if (rowblock->row_count() == 0) {
      return STATUS_FORMAT(NotFound, "Row not found for key $0", key);
    }
    return rowblock->row(0).column(0).int32_value();
  }

  void VerifyRow(int line, const YBSessionPtr& session, int32_t key, int32_t value,
                 const std::string& column = kValueColumn) {
    VLOG(4) << "Calling SelectRow";
    auto row = SelectRow(session, key, column);
    ASSERT_TRUE(row.ok()) << "Bad status: " << row << ", originator: " << __FILE__ << ":" << line;
    VLOG(4) << "SelectRow returned: " << *row;
    ASSERT_EQ(value, *row) << "Originator: " << __FILE__ << ":" << line;
  }

  void WriteData(const WriteOpType op_type = WriteOpType::INSERT) {
    CountDownLatch latch(1);
    {
      auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
      auto session = CreateSession(tc);
      WriteRows(session, 0, op_type);
      tc->Commit([&latch](const Status& status) {
          ASSERT_OK(status);
          latch.CountDown(1);
      });
    }
    latch.Wait();
    LOG(INFO) << "Committed";
  }

  void VerifyData(size_t num_transactions = 1, const WriteOpType op_type = WriteOpType::INSERT,
                  const std::string& column = kValueColumn) {
    VLOG(4) << "Verifying data..." << std::endl;
    auto session = CreateSession();
    for (size_t i = 0; i != num_transactions; ++i) {
      for (size_t r = 0; r != kNumRows; ++r) {
        VERIFY_ROW(
            session, KeyForTransactionAndIndex(i, r), ValueForTransactionAndIndex(i, r, op_type),
            column);
      }
    }
  }

  size_t CountTransactions() {
    size_t result = 0;
    for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
      auto* tablet_manager = cluster_->mini_tablet_server(i)->server()->tablet_manager();
      std::vector<tablet::TabletPeerPtr> peers;
      tablet_manager->GetTabletPeers(&peers);
      for (const auto& peer : peers) {
        if (peer->consensus()->leader_status() != consensus::Consensus::LeaderStatus::NOT_LEADER) {
          result += peer->tablet()->transaction_coordinator()->test_count_transactions();
        }
      }
    }
    return result;
  }

  TableHandle table_;
  boost::optional<TransactionManager> transaction_manager_;
  server::TestClock* clock_;
};

TEST_F(QLTransactionTest, Simple) {
  WriteData();
  VerifyData();
  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, ReadRestart) {
  google::FlagSaver saver;

  FLAGS_transaction_read_skew_usec = 250000;

  WriteData();

  server::TestClockDeltaChanger delta_changer(-100ms, clock_);
  auto txn1 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(),
                                              IsolationLevel::SNAPSHOT_ISOLATION);
  auto session = CreateSession(txn1);
  for (size_t r = 0; r != kNumRows; ++r) {
    auto row = SelectRow(session, KeyForTransactionAndIndex(0, r));
    ASSERT_NOK(row);
    ASSERT_EQ(ql::ErrorCode::RESTART_REQUIRED, ql::GetErrorCode(row.status()))
        << "Bad row: " << row;
  }
  auto txn2 = txn1->CreateRestartedTransaction();
  session->SetTransaction(txn2);
  for (size_t r = 0; r != kNumRows; ++r) {
    auto row = SelectRow(session, KeyForTransactionAndIndex(0, r));
    ASSERT_OK(row);
    ASSERT_EQ(ValueForTransactionAndIndex(0, r, WriteOpType::INSERT), *row);
  }
  VerifyData();

  CHECK_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, WriteRestart) {
  google::FlagSaver saver;

  FLAGS_transaction_read_skew_usec = 250000;

  const std::string kExtraColumn = "v2";
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  table_alterer->AddColumn(kExtraColumn)->Type(DataType::INT32);
  ASSERT_OK(table_alterer->Alter());

  ASSERT_OK(table_.Open(kTableName, client_.get())); // Reopen to update schema version.

  WriteData();

  server::TestClockDeltaChanger delta_changer(-100ms, clock_);
  auto txn1 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(),
                                              IsolationLevel::SNAPSHOT_ISOLATION);
  YBTransactionPtr txn2;
  auto session = CreateSession(txn1);
  for (bool retry : {false, true}) {
    for (size_t r = 0; r != kNumRows; ++r) {
      const auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
      auto* const req = op->mutable_request();
      auto key = KeyForTransactionAndIndex(0, r);
      auto old_value = ValueForTransactionAndIndex(0, r, WriteOpType::INSERT);
      auto value = ValueForTransactionAndIndex(0, r, WriteOpType::UPDATE);
      QLAddInt32HashValue(req, key);
      table_.AddInt32ColumnValue(req, kExtraColumn, value);
      auto cond = req->mutable_where_expr()->mutable_condition();
      table_.SetInt32Condition(cond, kValueColumn, QLOperator::QL_OP_EQUAL, old_value);
      req->mutable_column_refs()->add_ids(table_.ColumnId(kValueColumn));
      LOG(INFO) << "Updating value";
      auto status = session->Apply(op);
      ASSERT_OK(status);
      if (!retry) {
        ASSERT_EQ(QLResponsePB::YQL_STATUS_RESTART_REQUIRED_ERROR, op->response().status());
      } else {
        ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, op->response().status());
      }
    }
    if (!retry) {
      txn2 = txn1->CreateRestartedTransaction();
      session->SetTransaction(txn2);
    }
  }
  txn2->CommitFuture().wait();
  VerifyData();
  VerifyData(1, WriteOpType::UPDATE, kExtraColumn);

  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, InsertUpdate) {
  google::FlagSaver flag_saver;

  SetIgnoreApplyingProbability(1.0);
  WriteData(); // Add data
  WriteData(); // Update data
  VerifyData();
  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, Cleanup) {
  WriteData();
  VerifyData();

  // Wait transaction apply. Otherwise count could be non zero.
  ASSERT_OK(WaitFor(
      [this] { return CountTransactions() == 0; }, kTransactionApplyTime, "Transactions cleaned"));
  VerifyData();
  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, Heartbeat) {
  auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(),
                                            IsolationLevel::SNAPSHOT_ISOLATION);
  auto session = CreateSession(tc);
  WriteRows(session);
  std::this_thread::sleep_for(std::chrono::microseconds(FLAGS_transaction_timeout_usec * 2));
  CountDownLatch latch(1);
  tc->Commit([&latch](const Status& status) {
    EXPECT_OK(status);
    latch.CountDown();
  });
  latch.Wait();
  VerifyData();
}

TEST_F(QLTransactionTest, Expire) {
  google::FlagSaver flag_saver;
  SetDisableHeartbeatInTests(true);
  auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
  auto session = CreateSession(tc);
  WriteRows(session);
  std::this_thread::sleep_for(std::chrono::microseconds(FLAGS_transaction_timeout_usec * 2));
  CountDownLatch latch(1);
  tc->Commit([&latch](const Status& status) {
    EXPECT_TRUE(status.IsExpired()) << "Bad status: " << status.ToString();
    latch.CountDown();
  });
  latch.Wait();
  std::this_thread::sleep_for(std::chrono::microseconds(FLAGS_transaction_heartbeat_usec * 2));
  cluster_->CleanTabletLogs();
  ASSERT_EQ(0, CountTransactions());
}

TEST_F(QLTransactionTest, PreserveLogs) {
  google::FlagSaver flag_saver;
  SetDisableHeartbeatInTests(true);
  FLAGS_transaction_timeout_usec = std::chrono::microseconds(60s).count();
  std::vector<std::shared_ptr<YBTransaction>> transactions;
  constexpr size_t kTransactions = 20;
  for (size_t i = 0; i != kTransactions; ++i) {
    auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
    auto session = CreateSession(tc);
    WriteRows(session, i);
    transactions.push_back(std::move(tc));
    std::this_thread::sleep_for(100ms);
  }
  LOG(INFO) << "Request clean";
  cluster_->CleanTabletLogs();
  ASSERT_OK(cluster_->RestartSync());
  CountDownLatch latch(kTransactions);
  for (auto& transaction : transactions) {
    transaction->Commit([&latch](const Status& status) {
      EXPECT_OK(status);
      latch.CountDown();
    });
  }
  latch.Wait();
  VerifyData(kTransactions);
}

TEST_F(QLTransactionTest, ResendApplying) {
  google::FlagSaver flag_saver;

  SetIgnoreApplyingProbability(1.0);
  WriteData();
  std::this_thread::sleep_for(5s); // Transaction should not be applied here.
  ASSERT_NE(0, CountTransactions());

  SetIgnoreApplyingProbability(0.0);

  ASSERT_OK(WaitFor(
      [this] { return CountTransactions() == 0; }, kTransactionApplyTime, "Transactions cleaned"));
  VerifyData();
  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, ConflictResolution) {
  google::FlagSaver flag_saver;

  constexpr size_t kTotalTransactions = 5;
  constexpr size_t kNumRows = 10;
  std::vector<YBTransactionPtr> transactions;
  std::vector<YBSessionPtr> sessions;

  CountDownLatch latch(kTotalTransactions);
  for (size_t i = 0; i != kTotalTransactions; ++i) {
    transactions.push_back(std::make_shared<YBTransaction>(transaction_manager_.get_ptr(),
                                                           SNAPSHOT_ISOLATION));
    auto session = CreateSession(transactions.back());
    sessions.push_back(session);
    ASSERT_OK(session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
    for (size_t r = 0; r != kNumRows; ++r) {
      ASSERT_OK(WriteRow(sessions.back(), r, i));
    }
    session->FlushAsync(MakeYBStatusFunctorCallback([&latch](const Status& status) {
      latch.CountDown();
    }));
  }
  latch.Wait();

  latch.Reset(transactions.size());
  std::atomic<size_t> successes(0);
  std::atomic<size_t> failures(0);

  for (auto& transaction : transactions) {
    transaction->Commit([&latch, &successes, &failures](const Status& status) {
      if (status.ok()) {
        successes.fetch_add(1, std::memory_order_release);
      } else {
        failures.fetch_add(1, std::memory_order_release);
      }
      latch.CountDown(1);
    });
  }

  latch.Wait();
  LOG(INFO) << "Committed, successes: " << successes.load() << ", failures: " << failures.load();

  ASSERT_GE(successes.load(std::memory_order_acquire), 1);

  auto session = CreateSession();
  std::vector<int32_t> values;
  for (size_t r = 0; r != kNumRows; ++r) {
    auto row = SelectRow(session, r);
    ASSERT_OK(row);
    values.push_back(*row);
  }
  for (const auto& value : values) {
    ASSERT_EQ(values.front(), value) << "Values: " << yb::ToString(values);
  }
}

TEST_F(QLTransactionTest, SimpleWriteConflict) {
  google::FlagSaver flag_saver;

  auto transaction = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(),
                                                     SNAPSHOT_ISOLATION);

  WriteRows(CreateSession(transaction));
  WriteRows(CreateSession());

  ASSERT_NOK(transaction->CommitFuture().get());
}

TEST_F(QLTransactionTest, ResolveIntentsWriteReadUpdateRead) {
  google::FlagSaver flag_saver;
  DisableApplyingIntents();

  WriteData();
  VerifyData();

  WriteData(WriteOpType::UPDATE);
  VerifyData(1, WriteOpType::UPDATE);

  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, ResolveIntentsWriteReadWithinTransactionAndRollback) {
  google::FlagSaver flag_saver;
  DisableApplyingIntents();

  // Write { 1 -> 1, 2 -> 2 }.
  {
    auto session = CreateSession();
    ASSERT_OK(WriteRow(session, 1, 1));
    ASSERT_OK(WriteRow(session, 2, 2));
  }

  {
    // Start T1.
    auto txn = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
    auto session = CreateSession(txn);

    // T1: Update { 1 -> 11, 2 -> 12 }.
    ASSERT_OK(UpdateRow(session, 1, 11));
    ASSERT_OK(UpdateRow(session, 2, 12));

    // T1: Should read { 1 -> 11, 2 -> 12 }.
    VERIFY_ROW(session, 1, 11);
    VERIFY_ROW(session, 2, 12);

    txn->Abort();
  }

  ASSERT_OK(WaitFor(
      [this] { return CountTransactions() == 0; }, kTransactionApplyTime, "Transactions cleaned"));

  // Should read { 1 -> 1, 2 -> 2 }, since T1 has been aborted.
  {
    auto session = CreateSession();
    VERIFY_ROW(session, 1, 1);
    VERIFY_ROW(session, 2, 2);
  }

  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, ResolveIntentsWriteReadBeforeAndAfterCommit) {
  google::FlagSaver flag_saver;
  DisableApplyingIntents();

  // Write { 1 -> 1, 2 -> 2 }.
  {
    auto session = CreateSession();
    ASSERT_OK(WriteRow(session, 1, 1));
    ASSERT_OK(WriteRow(session, 2, 2));
  }

  // Start T1.
  auto txn1 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
  auto session1 = CreateSession(txn1);

  // T1: Update { 1 -> 11, 2 -> 12 }.
  ASSERT_OK(UpdateRow(session1, 1, 11));
  ASSERT_OK(UpdateRow(session1, 2, 12));

  // Start T2.
  auto txn2 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
  auto session2 = CreateSession(txn2);

  // T2: Should read { 1 -> 1, 2 -> 2 }.
  VERIFY_ROW(session2, 1, 1);
  VERIFY_ROW(session2, 2, 2);

  // T1: Commit
  CommitAndResetSync(&txn1);

  // T2: Should still read { 1 -> 1, 2 -> 2 }, because it should read at the time of it's start.
  VERIFY_ROW(session2, 1, 1);
  VERIFY_ROW(session2, 2, 2);

  // Simple read should get { 1 -> 11, 2 -> 12 }, since T1 has been already committed.
  {
    auto session = CreateSession();
    VERIFY_ROW(session, 1, 11);
    VERIFY_ROW(session, 2, 12);
  }

  CommitAndResetSync(&txn2);

  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, ResolveIntentsCheckConsistency) {
  google::FlagSaver flag_saver;
  FLAGS_transaction_read_skew_usec = 0; // To avoid read restart in this test.
  DisableApplyingIntents();

  // Write { 1 -> 1, 2 -> 2 }.
  {
    auto session = CreateSession();
    ASSERT_OK(WriteRow(session, 1, 1));
    ASSERT_OK(WriteRow(session, 2, 2));
  }

  // Start T1.
  auto txn1 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);

  // T1: Update { 1 -> 11, 2 -> 12 }.
  {
    auto session = CreateSession(txn1);
    ASSERT_OK(UpdateRow(session, 1, 11));
    ASSERT_OK(UpdateRow(session, 2, 12));
  }

  // T1: Request commit.
  CountDownLatch commit_latch(1);
  txn1->Commit([&commit_latch](const Status& status) {
    ASSERT_OK(status);
    commit_latch.CountDown(1);
  });

  // Start T2.
  auto txn2 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);

  // T2: Should read { 1 -> 1, 2 -> 2 } even in case T1 is committed between reading k1 and k2.
  {
    auto session = CreateSession(txn2);
    VERIFY_ROW(session, 1, 1);
    commit_latch.Wait();
    VERIFY_ROW(session, 2, 2);
  }

  // Simple read should get { 1 -> 11, 2 -> 12 }, since T1 has been already committed.
  {
    auto session = CreateSession();
    VERIFY_ROW(session, 1, 11);
    VERIFY_ROW(session, 2, 12);
  }

  CommitAndResetSync(&txn2);

  ASSERT_OK(cluster_->RestartSync());
}

struct TransactionState {
  YBTransactionPtr transaction;
  std::shared_future<TransactionMetadata> metadata_future;
  std::future<Status> commit_future;
  std::future<Result<tserver::GetTransactionStatusResponsePB>> status_future;
  TransactionMetadata metadata;
  HybridTime status_time = HybridTime::kMin;
  TransactionStatus last_status = TransactionStatus::PENDING;

  void CheckStatus() {
    ASSERT_TRUE(status_future.valid());
    ASSERT_EQ(status_future.wait_for(NonTsanVsTsan(1s, 5s)), std::future_status::ready);
    auto resp = status_future.get();
    ASSERT_OK(resp);

    if (resp->status() == TransactionStatus::ABORTED) {
      ASSERT_TRUE(commit_future.valid());
      transaction = nullptr;
      return;
    }

    auto new_time = HybridTime(resp->status_hybrid_time());
    if (last_status == TransactionStatus::PENDING) {
      if (resp->status() == TransactionStatus::PENDING) {
        ASSERT_GE(new_time, status_time);
      } else {
        ASSERT_EQ(TransactionStatus::COMMITTED, resp->status());
        ASSERT_GT(new_time, status_time);
      }
    } else {
      ASSERT_EQ(last_status, TransactionStatus::COMMITTED);
      ASSERT_EQ(resp->status(), TransactionStatus::COMMITTED)
          << "Bad transaction status: " << TransactionStatus_Name(resp->status());
      ASSERT_EQ(status_time, new_time);
    }
    status_time = new_time;
    last_status = resp->status();
  }
};

// Test transaction status evolution.
// The following should happen:
// If both previous and new transaction state are PENDING, then the new time of status is >= the
// old time of status.
// Previous - PENDING, new - COMMITTED, new_time > old_time.
// Previous - COMMITTED, new - COMMITTED, new_time == old_time.
// All other cases are invalid.
TEST_F(QLTransactionTest, StatusEvolution) {
  // We don't care about exact probability of create/commit operations.
  // Just create rate should be higher than commit one.
  const int kTransactionCreateChance = 10;
  const int kTransactionCommitChance = 20;
  size_t transactions_to_create = 10;
  size_t active_transactions = 0;
  std::vector<TransactionState> states;
  rpc::Rpcs rpcs;
  states.reserve(transactions_to_create);

  while (transactions_to_create || active_transactions) {
    if (transactions_to_create &&
        (!active_transactions || RandomWithChance(kTransactionCreateChance))) {
      LOG(INFO) << "Create transaction";
      auto txn =
          std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
      {
        auto session = CreateSession(txn);
        // Insert using different keys to avoid conflicts.
        ASSERT_OK(WriteRow(session, states.size(), states.size()));
      }
      states.push_back({ txn, txn->TEST_GetMetadata() });
      ++active_transactions;
      --transactions_to_create;
    }
    if (active_transactions && RandomWithChance(kTransactionCommitChance)) {
      LOG(INFO) << "Destroy transaction";
      size_t idx = RandomUniformInt<size_t>(1, active_transactions);
      for (auto& state : states) {
        if (!state.transaction) {
          continue;
        }
        if (!--idx) {
          state.commit_future = state.transaction->CommitFuture();
          break;
        }
      }
    }

    for (auto& state : states) {
      if (!state.transaction) {
        continue;
      }
      if (state.metadata.isolation == IsolationLevel::NON_TRANSACTIONAL) {
        if (state.metadata_future.wait_for(0s) != std::future_status::ready) {
          continue;
        }
        state.metadata = state.metadata_future.get();
      }
      tserver::GetTransactionStatusRequestPB req;
      req.set_tablet_id(state.metadata.status_tablet);
      req.set_transaction_id(state.metadata.transaction_id.data,
                             state.metadata.transaction_id.size());
      state.status_future = rpc::WrapRpcFuture<tserver::GetTransactionStatusResponsePB>(
          GetTransactionStatus, &rpcs)(
              TransactionRpcDeadline(), nullptr /* tablet */, client_.get(), &req);
    }
    for (auto& state : states) {
      if (!state.transaction) {
        continue;
      }
      state.CheckStatus();
      if (!state.transaction) {
        --active_transactions;
      }
    }
  }

  for (auto& state : states) {
    ASSERT_EQ(state.commit_future.wait_for(NonTsanVsTsan(3s, 15s)), std::future_status::ready);
  }
}

} // namespace client
} // namespace yb

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
#include <boost/scope_exit.hpp>

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_rpc.h"
#include "yb/client/transaction_manager.h"

#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/rpc/rpc.h"

#include "yb/server/hybrid_clock.h"
#include "yb/server/skewed_clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/transaction_coordinator.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/random_util.h"

using namespace std::literals; // NOLINT

DECLARE_uint64(transaction_timeout_usec);
DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_uint64(transaction_table_num_tablets);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_bool(transaction_disable_heartbeat_in_tests);
DECLARE_double(transaction_ignore_applying_probability_in_tests);
DECLARE_uint64(transaction_check_interval_usec);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_bool(transaction_allow_rerequest_status_in_tests);
DECLARE_uint64(transaction_delay_status_reply_usec_in_tests);
DECLARE_string(time_source);

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
    server::SkewedClock::Register();
    FLAGS_time_source = server::SkewedClock::kName;
    QLDmlTestBase::SetUp();

    YBSchemaBuilder builder;
    builder.AddColumn(kKeyColumn)->Type(INT32)->HashPrimaryKey()->NotNull();
    builder.AddColumn(kValueColumn)->Type(INT32);
    TableProperties table_properties;
    table_properties.SetTransactional(true);
    builder.SetTableProperties(table_properties);

    ASSERT_OK(table_.Create(kTableName, CalcNumTablets(3), client_.get(), &builder));

    FLAGS_transaction_table_num_tablets = 1;
    FLAGS_log_segment_size_bytes = 128;
    FLAGS_log_min_seconds_to_retain = 5;

    HybridTime::TEST_SetPrettyToString(true);

    ASSERT_OK(clock_->Init());
    transaction_manager_.emplace(client_, clock_);

    server::ClockPtr clock2(new server::HybridClock(skewed_clock_));
    ASSERT_OK(clock2->Init());
    transaction_manager2_.emplace(client_, clock2);
  }

  shared_ptr<YBSession> CreateSession(const YBTransactionPtr& transaction = nullptr) {
    auto session = std::make_shared<YBSession>(client_, transaction);
    session->SetTimeout(NonTsanVsTsan(15s, 60s));
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
    auto txn = CreateTransaction();
    WriteRows(CreateSession(txn), 0, op_type);
    ASSERT_OK(txn->CommitFuture().get());
    LOG(INFO) << "Committed";
  }

  void WriteDataWithRepetition() {
    auto txn = CreateTransaction();
    auto session = CreateSession(txn);
    for (size_t r = 0; r != kNumRows; ++r) {
      for (int j = 10; j--;) {
        ASSERT_OK(WriteRow(
            session,
            KeyForTransactionAndIndex(0, r),
            ValueForTransactionAndIndex(0, r, WriteOpType::INSERT) + j));
      }
    }
    ASSERT_OK(txn->CommitFuture().get());
  }

  YBTransactionPtr CreateTransaction() {
    return std::make_shared<YBTransaction>(
        transaction_manager_.get_ptr(), IsolationLevel::SNAPSHOT_ISOLATION);
  }

  YBTransactionPtr CreateTransaction2() {
    return std::make_shared<YBTransaction>(
        transaction_manager2_.get_ptr(), IsolationLevel::SNAPSHOT_ISOLATION);
  }

  void VerifyRows(const YBSessionPtr& session,
                  size_t transaction = 0,
                  const WriteOpType op_type = WriteOpType::INSERT,
                  const std::string& column = kValueColumn) {
    ASSERT_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));

    std::vector<client::YBqlReadOpPtr> ops;
    for (size_t r = 0; r != kNumRows; ++r) {
      ops.push_back(ReadRow(session, KeyForTransactionAndIndex(transaction, r), column));
    }
    ASSERT_OK(session->Flush());
    for (size_t r = 0; r != kNumRows; ++r) {
      SCOPED_TRACE(Format("Row: $0, key: $1", r, KeyForTransactionAndIndex(transaction, r)));
      auto& op = ops[r];
      ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
      auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
      ASSERT_EQ(rowblock->row_count(), 1);
      const auto& first_column = rowblock->row(0).column(0);
      ASSERT_EQ(QLValue::InternalType::kInt32Value, first_column.type());
      ASSERT_EQ(first_column.int32_value(), ValueForTransactionAndIndex(transaction, r, op_type));
    }
  }

  YBqlReadOpPtr ReadRow(const YBSessionPtr& session,
                        int32_t key,
                        const std::string& column = kValueColumn) {
    auto op = table_.NewReadOp();
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, key);
    table_.AddColumns({column}, req);
    EXPECT_OK(session->Apply(op));
    return op;
  }

  void VerifyData(size_t num_transactions = 1, const WriteOpType op_type = WriteOpType::INSERT,
                  const std::string& column = kValueColumn) {
    VLOG(4) << "Verifying data..." << std::endl;
    auto session = CreateSession();
    for (size_t i = 0; i != num_transactions; ++i) {
      VerifyRows(session, i, op_type, column);
    }
  }

  size_t CountTransactions() {
    size_t result = 0;
    for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
      auto* tablet_manager = cluster_->mini_tablet_server(i)->server()->tablet_manager();
      auto peers = tablet_manager->GetTabletPeers();
      for (const auto& peer : peers) {
        if (peer->consensus()->leader_status() != consensus::Consensus::LeaderStatus::NOT_LEADER) {
          result += peer->tablet()->transaction_coordinator()->test_count_transactions();
        }
      }
    }
    return result;
  }

  // We write data with first transaction then try to read it another one.
  // If commit is true, then first transaction is committed and second should be restarted.
  // Otherwise second transaction would see pending intents from first one and should not restart.
  void TestReadRestart(bool commit = true);

  TableHandle table_;
  std::shared_ptr<server::SkewedClock> skewed_clock_{
      std::make_shared<server::SkewedClock>(WallClock())};
  server::ClockPtr clock_{new server::HybridClock(skewed_clock_)};
  boost::optional<TransactionManager> transaction_manager_;
  boost::optional<TransactionManager> transaction_manager2_;
};

TEST_F(QLTransactionTest, Simple) {
  WriteData();
  VerifyData();
  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, WriteSameKey) {
  ASSERT_NO_FATALS(WriteDataWithRepetition());
  std::this_thread::sleep_for(1s); // Wait some time for intents to apply.
  ASSERT_NO_FATALS(VerifyData());
  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, WriteSameKeyWithIntents) {
  google::FlagSaver saver;
  DisableApplyingIntents();

  ASSERT_NO_FATALS(WriteDataWithRepetition());
  ASSERT_NO_FATALS(VerifyData());
  ASSERT_OK(cluster_->RestartSync());
}

// Commit flags says whether we should commit write txn during this test.
void QLTransactionTest::TestReadRestart(bool commit) {
  google::FlagSaver saver;

  FLAGS_max_clock_skew_usec = 250000;

  {
    auto write_txn = CreateTransaction();
    WriteRows(CreateSession(write_txn));
    if (commit) {
      ASSERT_OK(write_txn->CommitFuture().get());
    }
    BOOST_SCOPE_EXIT(write_txn, commit) {
      if (!commit) {
        write_txn->Abort();
      }
    } BOOST_SCOPE_EXIT_END;

    server::SkewedClockDeltaChanger delta_changer(-100ms, skewed_clock_);

    auto txn1 = CreateTransaction2();
    BOOST_SCOPE_EXIT(txn1, commit) {
      if (!commit) {
        txn1->Abort();
      }
    } BOOST_SCOPE_EXIT_END;
    auto session = CreateSession(txn1);
    if (commit) {
      for (size_t r = 0; r != kNumRows; ++r) {
        auto row = SelectRow(session, KeyForTransactionAndIndex(0, r));
        ASSERT_NOK(row);
        ASSERT_EQ(ql::ErrorCode::RESTART_REQUIRED, ql::GetErrorCode(row.status()))
                      << "Bad row: " << row;
      }
      auto txn2 = txn1->CreateRestartedTransaction();
      BOOST_SCOPE_EXIT(txn2) {
        txn2->Abort();
      } BOOST_SCOPE_EXIT_END;
      session->SetTransaction(txn2);
      for (size_t r = 0; r != kNumRows; ++r) {
        auto row = SelectRow(session, KeyForTransactionAndIndex(0, r));
        ASSERT_OK(row);
        ASSERT_EQ(ValueForTransactionAndIndex(0, r, WriteOpType::INSERT), *row);
      }
      VerifyData();
    } else {
      for (size_t r = 0; r != kNumRows; ++r) {
        auto row = SelectRow(session, KeyForTransactionAndIndex(0, r));
        ASSERT_TRUE(!row.ok() && row.status().IsNotFound()) << "Bad row: " << row;
      }
    }
  }

  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, ReadRestart) {
  TestReadRestart();
}

TEST_F(QLTransactionTest, ReadRestartWithIntents) {
  google::FlagSaver saver;
  DisableApplyingIntents();
  TestReadRestart();
}

TEST_F(QLTransactionTest, ReadRestartWithPendingIntents) {
  google::FlagSaver saver;
  FLAGS_transaction_allow_rerequest_status_in_tests = false;
  DisableApplyingIntents();
  TestReadRestart(false /* commit */);
}

// Non transactional r estart happens in server, so we just checking that we read correct values.
// Skewed clocks are used because there could be case when applied intents or commit transaction
// has time greater than max safetime to read, that causes restart.
TEST_F(QLTransactionTest, ReadRestartNonTransactional) {
  const auto kClockSkew = 500ms;
  google::FlagSaver saver;

  FLAGS_max_clock_skew_usec = 1000000;
  FLAGS_transaction_table_num_tablets = 3;
  FLAGS_transaction_timeout_usec = FLAGS_max_clock_skew_usec * 1000;

  auto delta_changers = SkewClocks(cluster_.get(), kClockSkew);
  constexpr size_t kTotalTransactions = 10;

  for (size_t i = 0; i != kTotalTransactions; ++i) {
    SCOPED_TRACE(Format("Transaction $0", i));
    auto txn = CreateTransaction();
    WriteRows(CreateSession(txn), i);
    ASSERT_OK(txn->CommitFuture().get());
    ASSERT_NO_FATALS(VerifyRows(CreateSession(), i));

    // We propagate hybrid time, so when commit and read finishes, all servers has about the same
    // physical component. We are waiting double skew, until time on servers became skewed again.
    std::this_thread::sleep_for(kClockSkew * 2);
  }

  cluster_->Shutdown(); // Need to shutdown cluster before resetting clock back.
  cluster_.reset();
}

TEST_F(QLTransactionTest, WriteRestart) {
  google::FlagSaver saver;

  FLAGS_max_clock_skew_usec = 250000;

  const std::string kExtraColumn = "v2";
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  table_alterer->AddColumn(kExtraColumn)->Type(DataType::INT32);
  ASSERT_OK(table_alterer->Alter());

  ASSERT_OK(table_.Open(kTableName, client_.get())); // Reopen to update schema version.

  WriteData();

  server::SkewedClockDeltaChanger delta_changer(-100ms, skewed_clock_);
  auto txn1 = CreateTransaction2();
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

TEST_F(QLTransactionTest, Child) {
  auto txn = CreateTransaction();
  TransactionManager manager2(client_, clock_);
  auto data_pb = txn->PrepareChildFuture().get();
  ASSERT_OK(data_pb);
  auto data = ChildTransactionData::FromPB(*data_pb);
  ASSERT_OK(data);
  auto txn2 = std::make_shared<YBTransaction>(&manager2, std::move(*data));

  WriteRows(CreateSession(txn2), 0);
  auto result = txn2->FinishChild();
  ASSERT_OK(result);
  ASSERT_OK(txn->ApplyChildResult(*result));

  ASSERT_OK(txn->CommitFuture().get());

  ASSERT_NO_FATALS(VerifyData());
  ASSERT_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, ChildReadRestart) {
  google::FlagSaver saver;

  FLAGS_max_clock_skew_usec = 250000;

  {
    auto write_txn = CreateTransaction();
    WriteRows(CreateSession(write_txn));
    ASSERT_OK(write_txn->CommitFuture().get());
  }

  server::SkewedClockDeltaChanger delta_changer(-100ms, skewed_clock_);
  auto parent_txn = CreateTransaction2();

  auto data_pb = parent_txn->PrepareChildFuture().get();
  ASSERT_OK(data_pb);
  auto data = ChildTransactionData::FromPB(*data_pb);
  ASSERT_OK(data);

  server::ClockPtr clock3(new server::HybridClock(skewed_clock_));
  ASSERT_OK(clock3->Init());
  TransactionManager manager3(client_, clock3);
  auto child_txn = std::make_shared<YBTransaction>(&manager3, std::move(*data));

  auto session = CreateSession(child_txn);
  for (size_t r = 0; r != kNumRows; ++r) {
    auto row = SelectRow(session, KeyForTransactionAndIndex(0, r));
    ASSERT_NOK(row);
    ASSERT_EQ(ql::ErrorCode::RESTART_REQUIRED, ql::GetErrorCode(row.status()))
                  << "Bad row: " << row;
  }

  auto result = child_txn->FinishChild();
  ASSERT_OK(result);
  ASSERT_OK(parent_txn->ApplyChildResult(*result));

  auto master2_txn = parent_txn->CreateRestartedTransaction();
  session->SetTransaction(master2_txn);
  for (size_t r = 0; r != kNumRows; ++r) {
    auto row = SelectRow(session, KeyForTransactionAndIndex(0, r));
    ASSERT_OK(row);
    ASSERT_EQ(ValueForTransactionAndIndex(0, r, WriteOpType::INSERT), *row);
  }
  ASSERT_NO_FATALS(VerifyData());

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
  auto txn = CreateTransaction();
  auto session = CreateSession(txn);
  WriteRows(session);
  std::this_thread::sleep_for(std::chrono::microseconds(FLAGS_transaction_timeout_usec * 2));
  CountDownLatch latch(1);
  txn->Commit([&latch](const Status& status) {
    EXPECT_OK(status);
    latch.CountDown();
  });
  latch.Wait();
  VerifyData();
}

TEST_F(QLTransactionTest, Expire) {
  google::FlagSaver flag_saver;
  SetDisableHeartbeatInTests(true);
  auto txn = CreateTransaction();
  auto session = CreateSession(txn);
  WriteRows(session);
  std::this_thread::sleep_for(std::chrono::microseconds(FLAGS_transaction_timeout_usec * 2));
  CountDownLatch latch(1);
  txn->Commit([&latch](const Status& status) {
    EXPECT_TRUE(status.IsExpired()) << "Bad status: " << status.ToString();
    latch.CountDown();
  });
  latch.Wait();
  std::this_thread::sleep_for(std::chrono::microseconds(FLAGS_transaction_heartbeat_usec * 2));
  ASSERT_OK(cluster_->CleanTabletLogs());
  ASSERT_EQ(0, CountTransactions());
}

TEST_F(QLTransactionTest, PreserveLogs) {
  google::FlagSaver flag_saver;
  SetDisableHeartbeatInTests(true);
  FLAGS_transaction_timeout_usec = std::chrono::microseconds(60s).count();
  std::vector<std::shared_ptr<YBTransaction>> transactions;
  constexpr size_t kTransactions = 20;
  for (size_t i = 0; i != kTransactions; ++i) {
    auto txn = CreateTransaction();
    auto session = CreateSession(txn);
    WriteRows(session, i);
    transactions.push_back(std::move(txn));
    std::this_thread::sleep_for(100ms);
  }
  LOG(INFO) << "Request clean";
  ASSERT_OK(cluster_->CleanTabletLogs());
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
    transactions.push_back(CreateTransaction());
    auto session = CreateSession(transactions.back());
    sessions.push_back(session);
    ASSERT_OK(session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
    for (size_t r = 0; r != kNumRows; ++r) {
      ASSERT_OK(WriteRow(sessions.back(), r, i));
    }
    session->FlushAsync([&latch](const Status& status) { latch.CountDown(); });
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

  auto transaction = CreateTransaction();
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
  FLAGS_max_clock_skew_usec = 0; // To avoid read restart in this test.
  DisableApplyingIntents();

  // Write { 1 -> 1, 2 -> 2 }.
  {
    auto session = CreateSession();
    ASSERT_OK(WriteRow(session, 1, 1));
    ASSERT_OK(WriteRow(session, 2, 2));
  }

  {
    // Start T1.
    auto txn = CreateTransaction();
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
  FLAGS_max_clock_skew_usec = 0; // To avoid read restart in this test.
  DisableApplyingIntents();

  // Write { 1 -> 1, 2 -> 2 }.
  {
    auto session = CreateSession();
    ASSERT_OK(WriteRow(session, 1, 1));
    ASSERT_OK(WriteRow(session, 2, 2));
  }

  // Start T1.
  auto txn1 = CreateTransaction();
  auto session1 = CreateSession(txn1);

  // T1: Update { 1 -> 11, 2 -> 12 }.
  ASSERT_OK(UpdateRow(session1, 1, 11));
  ASSERT_OK(UpdateRow(session1, 2, 12));

  // Start T2.
  auto txn2 = CreateTransaction();
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
  FLAGS_max_clock_skew_usec = 0; // To avoid read restart in this test.
  DisableApplyingIntents();

  // Write { 1 -> 1, 2 -> 2 }.
  {
    auto session = CreateSession();
    ASSERT_OK(WriteRow(session, 1, 1));
    ASSERT_OK(WriteRow(session, 2, 2));
  }

  // Start T1.
  auto txn1 = CreateTransaction();

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
  auto txn2 = CreateTransaction();

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

// This test launches write thread, that writes increasing value to key using transaction.
// Then it launches multiple read threads, each of them tries to read this key and
// verifies that its value is at least the same like it was written before read was started.
//
// It is don't for multiple keys sequentially. So those keys are located on different tablets
// and tablet servers, and we test different cases of clock skew.
TEST_F(QLTransactionTest, CorrectStatusRequestBatching) {
  const auto kClockSkew = 100ms;
  constexpr auto kMinWrites = RegularBuildVsSanitizers(25, 1);
  constexpr auto kMinReads = 10;
  constexpr size_t kConcurrentReads = RegularBuildVsSanitizers<size_t>(20, 5);
  google::FlagSaver saver;

  FLAGS_transaction_delay_status_reply_usec_in_tests = 200000;
  FLAGS_transaction_table_num_tablets = 3;
  FLAGS_log_segment_size_bytes = 0;
  FLAGS_max_clock_skew_usec = std::chrono::microseconds(kClockSkew).count() * 3;

  auto delta_changers = SkewClocks(cluster_.get(), kClockSkew);

  for (int32_t key = 0; key != 10; ++key) {
    std::atomic<bool> stop(false);
    std::atomic<int32_t> value(0);

    std::thread write_thread([this, key, &stop, &value] {
      auto session = CreateSession();
      while (!stop) {
        auto txn = CreateTransaction();
        session->SetTransaction(txn);
        WriteRow(session, key, value + 1);
        auto status = txn->CommitFuture().get();
        if (status.ok()) {
          ++value;
        }
      }
    });

    std::vector<std::thread> read_threads;
    std::array<std::atomic<size_t>, kConcurrentReads> reads;
    for (auto& read : reads) {
      read.store(0);
    }

    for (size_t i = 0; i != kConcurrentReads; ++i) {
      read_threads.emplace_back([this, key, &stop, &value, &read = reads[i]] {
        auto session = CreateSession();
        bool ok = false;
        BOOST_SCOPE_EXIT(&ok, &stop) {
          if (!ok) {
            stop = true;
          }
        } BOOST_SCOPE_EXIT_END;
        while (!stop) {
          auto value_before_start = value.load();
          YBqlReadOpPtr op = ReadRow(session, key);
          ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK)
                        << op->response().ShortDebugString();
          auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
          int32_t current_value;
          if (rowblock->row_count() == 0) {
            current_value = 0;
          } else {
            current_value = rowblock->row(0).column(0).int32_value();
          }
          ASSERT_GE(current_value, value_before_start);
          ++read;
        }
        ok = true;
      });
    }

    auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!stop && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(100ms);
    }

    // Already failed
    bool failed = stop.exchange(true);
    write_thread.join();

    for (auto& thread : read_threads) {
      thread.join();
    }

    if (failed) {
      break;
    }

    LOG(INFO) << "Writes: " << value.load() << ", reads: " << yb::ToString(reads);

    EXPECT_GE(value.load(), kMinWrites);
    for (auto& read : reads) {
      EXPECT_GE(read.load(), kMinReads);
    }
  }

  cluster_->Shutdown(); // Need to shutdown cluster before resetting clock back.
  cluster_.reset();
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
      auto txn = CreateTransaction();
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

// Writing multiple keys concurrently, each key is increasing by 1 at each step.
// At the same time concurrently execute several transactions that read all those keys.
// Suppose two transactions have read values t1_i and t2_i respectively.
// And t1_j > t2_j for some j, then we expect that t1_i >= t2_i for all i.
//
// Suppose we have 2 transactions, both reading k1 (from tablet1) and k2 (from tablet2).
// ht1 - read time of first transaction, and ht2 - read time of second transaction.
// Suppose ht1 <= ht2 for simplicity.
// Old value of k1 is v1before, and after ht_k1 it has v1after.
// Old value of k2 is v2before, and after ht_k2 it has v2after.
// ht_k1 <= ht1, ht_k2 <= ht1.
//
// Suppose following sequence of read requests:
// 1) The read request for the first transaction arrives at tablet1 when it has safe read
//    time < ht1. But it is already replicating k1 (with ht_k1). Then it would read v1before for k1.
// 2) The read request for the second transaction arrives at tablet2 when it has safe read
//    time < ht2. But it is already replicating k2 (with ht_k2). So it reads v2before for k2.
// 3) The remaining read request requests arrive after the appropriate operations have replicated.
//    So we get v2after in the first transaction and v1after for the second.
// The read result for the first transaction (v1before, v2after), for the second is is
// (v1after, v2before).
//
// Such read is inconsistent.
//
// This test addresses this issue.
TEST_F(QLTransactionTest, WaitRead) {
  google::FlagSaver saver;

  constexpr size_t kWriteThreads = 10;
  constexpr size_t kCycles = 100;
  constexpr size_t kConcurrentReads = 4;

  FLAGS_max_clock_skew_usec = 0; // To avoid read restart in this test.

  std::atomic<bool> stop(false);
  std::vector<std::thread> threads;

  for (size_t i = 0; i != kWriteThreads; ++i) {
    threads.emplace_back([this, i, &stop] {
      auto session = CreateSession();
      int32_t value = 0;
      while (!stop) {
        WriteRow(session, i, ++value);
      }
    });
  }

  CountDownLatch latch(kConcurrentReads);

  std::vector<std::vector<YBqlReadOpPtr>> reads(kConcurrentReads);
  std::vector<std::shared_future<Status>> futures(kConcurrentReads);
  // values[i] contains values read by i-th transaction.
  std::vector<std::vector<int32_t>> values(kConcurrentReads);

  for (size_t i = 0; i != kCycles; ++i) {
    latch.Reset(kConcurrentReads);
    for (size_t j = 0; j != kConcurrentReads; ++j) {
      values[j].clear();
      auto session = CreateSession(CreateTransaction());
      ASSERT_OK(session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
      for (size_t key = 0; key != kWriteThreads; ++key) {
        reads[j].push_back(ReadRow(session, key));
      }
      session->FlushAsync([&latch](const Status& status) {
        ASSERT_OK(status);
        latch.CountDown();
      });
    }
    latch.Wait();
    for (size_t j = 0; j != kConcurrentReads; ++j) {
      values[j].clear();
      for (auto& op : reads[j]) {
        ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK)
            << op->response().ShortDebugString();
        auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
        if (rowblock->row_count() == 1) {
          values[j].push_back(rowblock->row(0).column(0).int32_value());
        } else {
          values[j].push_back(0);
        }
      }
    }
    std::sort(values.begin(), values.end());
    for (size_t j = 1; j != kConcurrentReads; ++j) {
      for (size_t k = 0; k != values[j].size(); ++k) {
        ASSERT_GE(values[j][k], values[j - 1][k]);
      }
    }
  }

  stop = true;
  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace client
} // namespace yb

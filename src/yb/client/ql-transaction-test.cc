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

#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/ql-dml-test-base.h"

#include "yb/ql/util/statement_result.h"

#include "yb/tablet/transaction_coordinator.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tablet_server.h"

using namespace std::literals; // NOLINT

DECLARE_uint64(transaction_timeout_usec);
DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_uint64(transaction_table_default_num_tablets);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_bool(transaction_disable_heartbeat_in_tests);
DECLARE_double(transaction_ignore_applying_probability_in_tests);
DECLARE_uint64(transaction_check_interval_usec);

namespace yb {
namespace client {

namespace {

constexpr size_t kNumRows = 5;

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

class QLTransactionTest : public QLDmlTestBase {
 protected:
  void SetUp() override {
    QLDmlTestBase::SetUp();

    YBSchemaBuilder builder;
    builder.AddColumn("k")->Type(INT32)->HashPrimaryKey()->NotNull();
    builder.AddColumn("v")->Type(INT32);
    TableProperties table_properties;
    table_properties.SetTransactional(true);
    builder.SetTableProperties(table_properties);

    table_.Create(kTableName, client_.get(), &builder);

    FLAGS_transaction_table_default_num_tablets = 1;
    FLAGS_log_segment_size_bytes = 128;
    FLAGS_log_min_seconds_to_retain = 5;
    scoped_refptr<server::Clock> clock(new server::HybridClock);
    ASSERT_OK(clock->Init());
    transaction_manager_.emplace(client_, clock);
  }

  shared_ptr<YBSession> CreateSession(const bool read_only,
                                      const YBTransactionPtr& transaction = nullptr) {
    auto session = std::make_shared<YBSession>(client_, read_only, transaction);
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
    table_.SetInt32Expression(req->add_hashed_column_values(), key);
    table_.SetInt32ColumnValue(req->add_column_values(), "v", value);
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
      ASSERT_OK(WriteRow(session,
          KeyForTransactionAndIndex(transaction, r),
          ValueForTransactionAndIndex(transaction, r, op_type),
          op_type));
    }
  }

  // Select the specified columns of a row using a primary key, equivalent to the select statement
  // below. Return a YB read op that has been applied.
  //   select <columns...> from t where h1 = <h1> and h2 = <h2> and r1 = <r1> and r2 = <r2>;
  Result<int32_t> SelectRow(const YBSessionPtr& session, int32_t key) {
    const shared_ptr<YBqlReadOp> op = table_.NewReadOp();
    auto* const req = op->mutable_request();
    table_.SetInt32Expression(req->add_hashed_column_values(), key);
    table_.AddColumns({"v"}, req);
    auto status = session->Apply(op);
    if (status.IsIOError()) {
      for (const auto& error : session->GetPendingErrors()) {
        LOG(WARNING) << "Error: " << error->status() << ", op: " << error->failed_op().ToString();
      }
    }
    RETURN_NOT_OK(status);
    if (op->response().status() != QLResponsePB::YQL_STATUS_OK) {
      return STATUS_FORMAT(QLError, "Error selecting row: $0", op->response().error_message());
    }
    auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
    if (rowblock->row_count() == 0) {
      return STATUS_FORMAT(NotFound, "Row not found for key $0", key);
    }
    return rowblock->row(0).column(0).int32_value();
  }

  void VerifyRow(const YBSessionPtr& session, int32_t key, int32_t value) {
    VLOG(4) << "Calling SelectRow";
    auto row = SelectRow(session, key);
    ASSERT_OK(row);
    VLOG(4) << "SelectRow returned: " << *row;
    ASSERT_EQ(value, *row);
  }

  void WriteData(const WriteOpType op_type = WriteOpType::INSERT) {
    CountDownLatch latch(1);
    {
      auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
      auto session = CreateSession(false /* read_only */, tc);
      WriteRows(session, 0, op_type);
      tc->Commit([&latch](const Status& status) {
          ASSERT_OK(status);
          latch.CountDown(1);
      });
    }
    latch.Wait();
    LOG(INFO) << "Committed";
  }

  void VerifyData(size_t num_transactions = 1, const WriteOpType op_type = WriteOpType::INSERT) {
    VLOG(4) << "Verifying data..." << std::endl;
    auto session = CreateSession(true /* read_only */);
    for (size_t i = 0; i != num_transactions; ++i) {
      for (size_t r = 0; r != kNumRows; ++r) {
        VerifyRow(
            session, KeyForTransactionAndIndex(i, r), ValueForTransactionAndIndex(i, r, op_type));
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
};

TEST_F(QLTransactionTest, Simple) {
  WriteData();
  VerifyData();
  CHECK_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, InsertUpdate) {
  google::FlagSaver flag_saver;

  SetIgnoreApplyingProbability(1.0);
  WriteData(); // Add data
  WriteData(); // Update data
  SetIgnoreApplyingProbability(0.0);
  std::this_thread::sleep_for(5s); // TODO(dtxn) Wait for intents to apply
  VerifyData();
  CHECK_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, Cleanup) {
  WriteData();
  std::this_thread::sleep_for(1s); // TODO(dtxn)
  ASSERT_EQ(0, CountTransactions());
  VerifyData();
  CHECK_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, Heartbeat) {
  auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(),
                                            IsolationLevel::SNAPSHOT_ISOLATION);
  auto session = CreateSession(false /* read_only */, tc);
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
  DontVerifyClusterBeforeNextTearDown(); // TODO(dtxn) temporary

  google::FlagSaver flag_saver;
  SetDisableHeartbeatInTests(true);
  auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
  auto session = CreateSession(false /* read_only */, tc);
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
    auto session = CreateSession(false /* read_only */, tc);
    WriteRows(session, i);
    transactions.push_back(std::move(tc));
    std::this_thread::sleep_for(100ms);
  }
  LOG(INFO) << "Request clean";
  cluster_->CleanTabletLogs();
  CHECK_OK(cluster_->RestartSync());
  CountDownLatch latch(kTransactions);
  for (auto& transaction : transactions) {
    transaction->Commit([&latch](const Status& status) {
      EXPECT_OK(status);
      latch.CountDown();
    });
  }
  latch.Wait();
  // TODO(dtxn) - without this sleep "Row not found" could happen. Most probably it has to do with
  // concurrent removal of intents and replacing them with regular key-value pairs.
  // Solution should be similar to https://yugabyte.atlassian.net/browse/ENG-2271.
  std::this_thread::sleep_for(3s); // Wait long enough for transaction to be applied.
  VerifyData(kTransactions);
}

TEST_F(QLTransactionTest, ResendApplying) {
  google::FlagSaver flag_saver;

  SetIgnoreApplyingProbability(1.0);
  WriteData();
  std::this_thread::sleep_for(5s); // Transaction should not be applied here.
  ASSERT_NE(0, CountTransactions());

  SetIgnoreApplyingProbability(0.0);
  std::this_thread::sleep_for(1s); // Wait long enough for transaction to be applied.
  ASSERT_EQ(0, CountTransactions());
  VerifyData();
  CHECK_OK(cluster_->RestartSync());
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
    auto session = CreateSession(false /* read_only */, transactions.back());
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

  auto session = CreateSession(true /* read_only */);
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

  WriteRows(CreateSession(false /* read_only */, transaction));

  WriteRows(CreateSession(false /* read_only */));

  ASSERT_NOK(transaction->CommitFuture().get());
}

TEST_F(QLTransactionTest, ResolveIntentsWriteReadUpdateRead) {
  google::FlagSaver flag_saver;
  DisableApplyingIntents();

  WriteData();
  VerifyData();

  WriteData(WriteOpType::UPDATE);
  VerifyData(1, WriteOpType::UPDATE);

  CHECK_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, ResolveIntentsWriteReadWithinTransactionAndRollback) {
  DontVerifyClusterBeforeNextTearDown(); // TODO(dtxn) temporary - uncomment once crash is fixed.
  google::FlagSaver flag_saver;
  DisableApplyingIntents();

  // Write { 1 -> 1, 2 -> 2 }.
  {
    auto session = CreateSession(false /* read_only */);
    ASSERT_OK(WriteRow(session, 1, 1));
    ASSERT_OK(WriteRow(session, 2, 2));
  }

  CountDownLatch latch(1);
  {
    // Start T1.
    auto txn1 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);

    // T1: Update { 1 -> 11, 2 -> 12 }.
    {
      auto session = CreateSession(false /* read_only */, txn1);
      ASSERT_OK(UpdateRow(session, 1, 11));
      ASSERT_OK(UpdateRow(session, 2, 12));
    }

    // T1: Should read { 1 -> 11, 2 -> 12 }.
    {
      auto session = CreateSession(true /* read_only */, txn1);
      VerifyRow(session, 1, 11);
      VerifyRow(session, 2, 12);
    }

    // T1: Abort.
    // TODO(dtxn) - temporary workaround to abort the transaction, to be replaced with commented
    // code below after YBTransaction::Abort is implemented.
    SetDisableHeartbeatInTests(true);
    std::this_thread::sleep_for(std::chrono::microseconds(FLAGS_transaction_timeout_usec * 2));
    txn1->Commit([&latch](const Status& status) {
      ASSERT_TRUE(status.IsExpired()) << "Bad status: " << status.ToString();
      latch.CountDown();
    });

    // TODO(dtxn) - uncomment after YBTransaction::Abort is implemented.
//    txn->Abort([&latch](const Status& status) {
//      ASSERT_OK(status);
//      latch.CountDown(1);
//    });
  }
  latch.Wait();

  // Should read { 1 -> 1, 2 -> 2 }, since T1 has been aborted.
  {
    auto session = CreateSession(true /* read_only */);
    VerifyRow(session, 1, 1);
    VerifyRow(session, 2, 2);
  }

  // TODO(dtxn) - uncomment once fixed, currently TransactionParticipant doesn't know about the
  // transaction, however there are intents in DB with this transaction ID.
  // https://yugabyte.atlassian.net/browse/ENG-2271.
//  CHECK_OK(cluster_->RestartSync());
}

TEST_F(QLTransactionTest, ResolveIntentsWriteReadBeforeAndAfterCommit) {
  google::FlagSaver flag_saver;
  DisableApplyingIntents();

  // Write { 1 -> 1, 2 -> 2 }.
  {
    auto session = CreateSession(false /* read_only */);
    ASSERT_OK(WriteRow(session, 1, 1));
    ASSERT_OK(WriteRow(session, 2, 2));
  }

  // Start T1.
  auto txn1 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);

  // T1: Update { 1 -> 11, 2 -> 12 }.
  {
    auto session = CreateSession(false /* read_only */, txn1);
    ASSERT_OK(UpdateRow(session, 1, 11));
    ASSERT_OK(UpdateRow(session, 2, 12));
  }

  // Start T2.
  auto txn2 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);

  // T2: Should read { 1 -> 1, 2 -> 2 }.
  // TODO(dtxn) currently fails with "Cannot determine transaction status". Uncomment once fixed.
//  {
//    auto session = CreateSession(true /* read_only */, txn2);
//    VerifyRow(session, 1, 1);
//    VerifyRow(session, 2, 2);
//  }

  // T1: Commit
  CommitAndResetSync(&txn1);

  // T2: Should still read { 1 -> 1, 2 -> 2 }, because it should read at the time of it's start.
  // TODO(dtxn) currently fails, because read time selected incorrectly. Uncomment once fixed.
//  {
//    auto session = CreateSession(true /* read_only */, txn2);
//    VerifyRow(session, 1, 1);
//    VerifyRow(session, 2, 2);
//  }

  // Simple read should get { 1 -> 11, 2 -> 12 }, since T1 has been already committed.
  {
    auto session = CreateSession(true /* read_only */);
    VerifyRow(session, 1, 11);
    VerifyRow(session, 2, 12);
  }

  CommitAndResetSync(&txn2);

  CHECK_OK(cluster_->RestartSync());
}

// TODO(dtxn) enable test as soon as we support this case.
TEST_F(QLTransactionTest, DISABLED_ResolveIntentsCheckConsistency) {
  google::FlagSaver flag_saver;
  DisableApplyingIntents();

  // Write { 1 -> 1, 2 -> 2 }.
  {
    auto session = CreateSession(false /* read_only */);
    ASSERT_OK(WriteRow(session, 1, 1));
    ASSERT_OK(WriteRow(session, 2, 2));
  }

  // Start T1.
  auto txn1 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);

  // T1: Update { 1 -> 11, 2 -> 12 }.
  {
    auto session = CreateSession(false /* read_only */, txn1);
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
  // TODO - to test efficiently we need T2_start_time > T1_commit_time, but T1 should not be
  // committed yet when T2 is reading k1.
  // How can we do that?
  auto txn2 = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);

  // T2: Should read { 1 -> 1, 2 -> 2 } even in case T1 is committed between reading k1 and k2.
  {
    auto session = CreateSession(true /* read_only */, txn2);
    VerifyRow(session, 1, 1);
    // Need T1 to be not committed yet at this point.
    // Wait T1 for commit.
    commit_latch.Wait();
    VerifyRow(session, 2, 2);
  }

  // Simple read should get { 1 -> 11, 2 -> 12 }, since T1 has been already committed.
  {
    auto session = CreateSession(true /* read_only */);
    VerifyRow(session, 1, 11);
    VerifyRow(session, 2, 12);
  }

  CommitAndResetSync(&txn2);

  CHECK_OK(cluster_->RestartSync());
}

} // namespace client
} // namespace yb

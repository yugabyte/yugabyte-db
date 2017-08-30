//
// Copyright (c) YugaByte, Inc.
//

#include <thread>

#include <boost/optional/optional.hpp>

#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/yql-dml-test-base.h"

#include "yb/sql/util/statement_result.h"

#include "yb/tablet/transaction_coordinator.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tablet_server.h"

using namespace std::literals;

DECLARE_uint64(transaction_timeout_usec);
DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_uint64(transaction_table_default_num_tablets);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_bool(transaction_disable_heartbeat_in_tests);
DECLARE_double(transaction_ignore_appying_probability_in_tests);
DECLARE_uint64(transaction_check_interval_usec);

namespace yb {
namespace client {

constexpr size_t kNumRows = 5;

int32_t KeyForTransactionAndIndex(size_t transaction, size_t index) {
  return static_cast<int32_t>(transaction * 10 + index);
}

int32_t ValueForTransactionAndIndex(size_t transaction, size_t index) {
  return static_cast<int32_t>(transaction * 10 + index + 2);
}

class YqlTransactionTest : public YqlDmlTestBase {
 protected:
  void SetUp() override {
    YqlDmlTestBase::SetUp();
    DontVerifyClusterBeforeNextTearDown(); // TODO(dtxn) temporary

    YBSchemaBuilder builder;
    builder.AddColumn("k")->Type(INT32)->HashPrimaryKey()->NotNull();
    builder.AddColumn("v")->Type(INT32);

    table_.Create(kTableName, client_.get(), &builder);

    FLAGS_transaction_table_default_num_tablets = 1;
    FLAGS_log_segment_size_bytes = 128;
    FLAGS_log_min_seconds_to_retain = 5;
    transaction_manager_.emplace(client_);
  }

  // Insert a full, single row, equivalent to the insert statement below. Return a YB write op that
  // has been applied.
  //   insert into t values (key, value);
  shared_ptr<YBqlWriteOp> InsertRow(const YBSessionPtr& session, int32_t key, int32_t value) {
    const auto op = table_.NewWriteOp(YQLWriteRequestPB::YQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    table_.SetInt32ColumnValue(req->add_hashed_column_values(), "k", key, prow, 0);
    table_.SetInt32ColumnValue(req->add_column_values(), "v", value);
    CHECK_OK(session->Apply(op));
    return op;
  }

  void InsertRows(const YBSessionPtr& session, size_t transaction = 0) {
    for (size_t r = 0; r != kNumRows; ++r) {
      InsertRow(session,
                KeyForTransactionAndIndex(transaction, r),
                ValueForTransactionAndIndex(transaction, r));
    }
  }

  // Select the specified columns of a row using a primary key, equivalent to the select statement
  // below. Return a YB read op that has been applied.
  //   select <columns...> from t where h1 = <h1> and h2 = <h2> and r1 = <r1> and r2 = <r2>;
  Result<int32_t> SelectRow(const YBSessionPtr& session, int32_t key) {
    const shared_ptr<YBqlReadOp> op = table_.NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    table_.SetInt32ColumnValue(req->add_hashed_column_values(), "k", key, prow, 0);
    table_.AddColumns({"v"}, req);
    RETURN_NOT_OK(session->Apply(op));
    auto rowblock = yb::sql::RowsResult(op.get()).GetRowBlock();
    if (rowblock->row_count() == 0) {
      return STATUS_FORMAT(NotFound, "Row not found for key $0", key);
    }
    return rowblock->row(0).column(0).int32_value();
  }

  void WriteData() {
    CountDownLatch latch(1);
    {
      auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
      auto session = std::make_shared<YBSession>(client_, false /* read_only */, tc);
      session->SetTimeout(5s);
      InsertRows(session);
      tc->Commit([&latch](const Status& status) {
          ASSERT_OK(status);
          latch.CountDown(1);
      });
    }
    latch.Wait();
    LOG(INFO) << "Committed";
  }

  void VerifyData(size_t num_transactions = 1) {
    std::this_thread::sleep_for(1s); // TODO(dtxn) wait for intents to apply
    auto session = client_->NewSession(true /* read_only */);
    session->SetTimeout(5s);
    for (size_t i = 0; i != num_transactions; ++i) {
      for (size_t r = 0; r != kNumRows; ++r) {
        auto row = SelectRow(session, KeyForTransactionAndIndex(i, r));
        ASSERT_OK(row);
        ASSERT_EQ(ValueForTransactionAndIndex(i, r), *row);
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

TEST_F(YqlTransactionTest, Simple) {
  WriteData();
  VerifyData();
  CHECK_OK(cluster_->RestartSync());
}

TEST_F(YqlTransactionTest, Cleanup) {
  WriteData();
  std::this_thread::sleep_for(1s); // TODO(dtxn)
  ASSERT_EQ(0, CountTransactions());
  VerifyData();
  CHECK_OK(cluster_->RestartSync());
}

TEST_F(YqlTransactionTest, Heartbeat) {
  auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(),
                                            IsolationLevel::SNAPSHOT_ISOLATION);
  auto session = std::make_shared<YBSession>(client_, false /* read_only */, tc);
  session->SetTimeout(5s);
  InsertRows(session);
  std::this_thread::sleep_for(std::chrono::microseconds(FLAGS_transaction_timeout_usec * 2));
  CountDownLatch latch(1);
  tc->Commit([&latch](const Status& status) {
    EXPECT_OK(status);
    latch.CountDown();
  });
  latch.Wait();
  VerifyData();
}

TEST_F(YqlTransactionTest, Expire) {
  google::FlagSaver flag_saver;
  FLAGS_transaction_disable_heartbeat_in_tests = true;
  auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
  auto session = std::make_shared<YBSession>(client_, false /* read_only */, tc);
  session->SetTimeout(5s);
  InsertRows(session);
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

TEST_F(YqlTransactionTest, PreserveLogs) {
  google::FlagSaver flag_saver;
  FLAGS_transaction_disable_heartbeat_in_tests = true;
  FLAGS_transaction_timeout_usec = std::chrono::microseconds(60s).count();
  std::vector<std::shared_ptr<YBTransaction>> transactions;
  constexpr size_t kTransactions = 20;
  for (size_t i = 0; i != kTransactions; ++i) {
    auto tc = std::make_shared<YBTransaction>(transaction_manager_.get_ptr(), SNAPSHOT_ISOLATION);
    auto session = std::make_shared<YBSession>(client_, false /* read_only */, tc);
    session->SetTimeout(5s);
    InsertRows(session, i);
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
  VerifyData(kTransactions);
}

TEST_F(YqlTransactionTest, ResendApplying) {
  google::FlagSaver flag_saver;
  std::atomic<double>& atomic_flag = *pointer_cast<std::atomic<double>*>(
      &FLAGS_transaction_ignore_appying_probability_in_tests);

  atomic_flag.store(1.0);
  WriteData();
  std::this_thread::sleep_for(5s); // Transaction should not be applied here.
  ASSERT_NE(0, CountTransactions());

  atomic_flag.store(0.0);
  std::this_thread::sleep_for(1s); // Wait long enough for transaction to be applied.
  ASSERT_EQ(0, CountTransactions());
  VerifyData();
  CHECK_OK(cluster_->RestartSync());
}

} // namespace client
} // namespace yb

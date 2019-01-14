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

#include "yb/client/txn-test-base.h"

#include "yb/client/transaction.h"

using namespace std::literals;

namespace yb {
namespace client {

class SerializableTxnTest : public TransactionTestBase {
 protected:
  IsolationLevel GetIsolationLevel() override {
    return IsolationLevel::SERIALIZABLE_ISOLATION;
  }

  void TestIncrement(int key);
};

TEST_F(SerializableTxnTest, NonConflictingWrites) {
  const auto kTransactions = 10;
  const auto kKey = 0;

  struct Entry {
    YBTransactionPtr txn;
    YBqlWriteOpPtr op;
    std::future<Status> flush_future;
    std::future<Status> commit_future;
    bool done = false;
  };

  std::vector<Entry> entries;
  for (int i = 0; i != kTransactions; ++i) {
    entries.emplace_back();
    auto& entry = entries.back();
    entry.txn = CreateTransaction();
    auto session = CreateSession(entry.txn);
    entry.op = ASSERT_RESULT(WriteRow(session, kKey, i));
    entry.flush_future = session->FlushFuture();
  }

  ASSERT_OK(WaitFor([&entries]() -> Result<bool> {
    for (auto& entry : entries) {
      if (entry.flush_future.valid() &&
          entry.flush_future.wait_for(0s) == std::future_status::ready) {
        LOG(INFO) << "Flush done";
        RETURN_NOT_OK(entry.flush_future.get());
        entry.commit_future = entry.txn->CommitFuture();
      }
    }

    for (auto& entry : entries) {
      if (entry.commit_future.valid() &&
          entry.commit_future.wait_for(0s) == std::future_status::ready) {
        LOG(INFO) << "Commit done";
        RETURN_NOT_OK(entry.commit_future.get());
        entry.done = true;
      }
    }

    for (const auto& entry : entries) {
      if (!entry.done) {
        return false;
      }
    }

    return true;
  }, 10s, "Complete all operations"));

  for (const auto& entry : entries) {
    ASSERT_EQ(entry.op->response().status(), QLResponsePB::YQL_STATUS_OK);
  }
}

TEST_F(SerializableTxnTest, ReadWriteConflict) {
  const auto kKeys = 20;

  size_t reads_won = 0, writes_won = 0;
  for (int i = 0; i != kKeys; ++i) {
    auto read_txn = CreateTransaction();
    auto read_session = CreateSession(read_txn);
    auto read = ReadRow(read_session, i);
    ASSERT_OK(read_session->Flush());

    auto write_txn = CreateTransaction();
    auto write_session = CreateSession(write_txn);
    auto write_status = ResultToStatus(WriteRow(
        write_session, i, i, WriteOpType::INSERT, Flush::kTrue));

    auto read_commit_future = read_txn->CommitFuture();
    if (write_status.ok()) {
      write_status = write_txn->CommitFuture().get();
    }
    auto read_status = read_commit_future.get();

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
  ASSERT_GE(reads_won, kKeys / 4);
  ASSERT_GE(writes_won, kKeys / 4);
}

// Execute UPDATE table SET value = value + 1 WHERE key = kKey in parallel, using
// serializable isolation.
// With retries the resulting value should be equal to number of increments.
void SerializableTxnTest::TestIncrement(int key) {
  const auto kIncrements = RegularBuildVsSanitizers(100, 20);

  {
    auto session = CreateSession();
    auto op = ASSERT_RESULT(WriteRow(session, key, 0));
    ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
  }

  struct Entry {
    YBqlWriteOpPtr op;
    YBTransactionPtr txn;
    YBSessionPtr session;
    std::shared_future<Status> write_future;
    std::shared_future<Status> commit_future;
  };

  std::vector<Entry> entries;

  auto value_column_id = table_.ColumnId(kValueColumn);
  for (int i = 0; i != kIncrements; ++i) {
    Entry entry;
    entry.txn = CreateTransaction();
    entry.session = CreateSession(entry.txn);
    entries.push_back(entry);
  }

  // For each of entries we do the following:
  // 1) Write increment operation.
  // 2) Wait until write complete and commit transaction of this entry.
  // 3) Wait until commit complete.
  // When failure happens on any step - retry from step 1.
  // Exit from loop when all entries successfully committed their transactions.
  // We do all actions in busy loop to get most possible concurrency for operations.
  for (;;) {
    bool incomplete = false;
    for (auto& entry : entries) {
      bool entry_complete = false;
      if (!entry.op) {
        // Execute UPDATE table SET value = value + 1 WHERE key = kKey
        entry.op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
        auto* const req = entry.op->mutable_request();
        QLAddInt32HashValue(req, key);
        req->mutable_column_refs()->add_ids(value_column_id);
        auto* column_value = req->add_column_values();
        column_value->set_column_id(value_column_id);
        auto* bfcall = column_value->mutable_expr()->mutable_bfcall();
        bfcall->set_opcode(to_underlying(bfql::BFOpcode::OPCODE_ConvertI64ToI32_18));
        bfcall = bfcall->add_operands()->mutable_bfcall();

        bfcall->set_opcode(to_underlying(bfql::BFOpcode::OPCODE_AddI64I64_80));
        auto column_op = bfcall->add_operands()->mutable_bfcall();
        column_op->set_opcode(to_underlying(bfql::BFOpcode::OPCODE_ConvertI32ToI64_13));
        column_op->add_operands()->set_column_id(value_column_id);
        bfcall->add_operands()->mutable_value()->set_int64_value(1);

        entry.session->SetTransaction(entry.txn);
        ASSERT_OK(entry.session->Apply(entry.op));
        entry.write_future = entry.session->FlushFuture();
      } else if (entry.write_future.valid()) {
        if (entry.write_future.wait_for(0s) == std::future_status::ready) {
          auto write_status = entry.write_future.get();
          entry.write_future = std::shared_future<Status>();
          if (!write_status.ok()) {
            ASSERT_TRUE(write_status.IsIOError()) << write_status;
            auto errors = entry.session->GetPendingErrors();
            ASSERT_EQ(errors.size(), 1);
            auto status = errors.front()->status();
            ASSERT_TRUE(status.IsTryAgain() || status.IsTimedOut()) << status;
            entry.txn = CreateTransaction();
            entry.op = nullptr;
          } else {
            if (entry.op->response().status() == QLResponsePB::YQL_STATUS_RESTART_REQUIRED_ERROR) {
              auto old_txn = entry.txn;
              entry.txn = entry.txn->CreateRestartedTransaction();
              entry.op = nullptr;
            } else {
              ASSERT_EQ(entry.op->response().status(), QLResponsePB::YQL_STATUS_OK);
              entry.commit_future = entry.txn->CommitFuture();
            }
          }
        }
      } else if (entry.commit_future.valid()) {
        if (entry.commit_future.wait_for(0s) == std::future_status::ready) {
          auto status = entry.commit_future.get();
          if (status.IsExpired() || status.IsTimedOut()) {
            entry.txn = CreateTransaction();
            entry.op = nullptr;
          } else {
            ASSERT_OK(status);
            entry.commit_future = std::shared_future<Status>();
          }
        }
      } else {
        entry_complete = true;
      }
      incomplete = incomplete || !entry_complete;
    }
    if (!incomplete) {
      break;
    }
  }

  auto value = ASSERT_RESULT(SelectRow(CreateSession(), key));
  ASSERT_EQ(value, kIncrements);
}

// Execute UPDATE table SET value = value + 1 WHERE key = kKey in parallel, using
// serializable isolation.
// With retries the resulting value should be equal to number of increments.
TEST_F(SerializableTxnTest, Increment) {
  const auto kThreads = 3;

  std::vector<std::thread> threads;
  while (threads.size() != kThreads) {
    int key = threads.size();
    threads.emplace_back([this, key] {
      TestIncrement(key);
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace client
} // namespace yb

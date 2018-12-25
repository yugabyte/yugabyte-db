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

namespace yb {
namespace client {

class SerializableTxnTest : public TransactionTestBase {
 protected:
  IsolationLevel GetIsolationLevel() override {
    return IsolationLevel::SERIALIZABLE_ISOLATION;
  }
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

} // namespace client
} // namespace yb

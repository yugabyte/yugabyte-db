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

#ifndef YB_CLIENT_TXN_TEST_BASE_H
#define YB_CLIENT_TXN_TEST_BASE_H

#include "yb/client/ql-dml-test-base.h"

#include "yb/server/hybrid_clock.h"
#include "yb/server/skewed_clock.h"

namespace yb {
namespace client {

constexpr size_t kNumRows = 5;
extern const MonoDelta kTransactionApplyTime;

// We use different sign to distinguish inserted and updated values for testing.
int32_t GetMultiplier(const WriteOpType op_type);

int32_t KeyForTransactionAndIndex(size_t transaction, size_t index);

int32_t ValueForTransactionAndIndex(size_t transaction, size_t index, const WriteOpType op_type);

void SetIgnoreApplyingProbability(double value);

void SetDisableHeartbeatInTests(bool value);

void DisableApplyingIntents();

void CommitAndResetSync(YBTransactionPtr *txn);

void DisableTransactionTimeout();

#define VERIFY_ROW(...) VerifyRow(__LINE__, __VA_ARGS__)

class TransactionTestBase : public KeyValueTableTest {
 protected:
  void SetUp() override;

  virtual uint64_t log_segment_size_bytes() const;

  void WriteRows(
      const YBSessionPtr& session, size_t transaction = 0,
      const WriteOpType op_type = WriteOpType::INSERT);

  void VerifyRow(int line, const YBSessionPtr& session, int32_t key, int32_t value,
                 const std::string& column = kValueColumn);

  void WriteData(const WriteOpType op_type = WriteOpType::INSERT, size_t transaction = 0);

  void WriteDataWithRepetition();

  YBTransactionPtr CreateTransaction();

  YBTransactionPtr CreateTransaction2();

  void VerifyRows(const YBSessionPtr& session,
                  size_t transaction = 0,
                  const WriteOpType op_type = WriteOpType::INSERT,
                  const std::string& column = kValueColumn);

  YBqlReadOpPtr ReadRow(const YBSessionPtr& session,
                        int32_t key,
                        const std::string& column = kValueColumn);

  void VerifyData(size_t num_transactions = 1, const WriteOpType op_type = WriteOpType::INSERT,
                  const std::string& column = kValueColumn);

  size_t CountTransactions();

  size_t CountIntents();

  void CheckNoRunningTransactions();

  bool CheckAllTabletsRunning();

  virtual IsolationLevel GetIsolationLevel() = 0;

  std::shared_ptr<server::SkewedClock> skewed_clock_{
      std::make_shared<server::SkewedClock>(WallClock())};
  server::ClockPtr clock_{new server::HybridClock(skewed_clock_)};
  boost::optional<TransactionManager> transaction_manager_;
  boost::optional<TransactionManager> transaction_manager2_;
};

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TXN_TEST_BASE_H

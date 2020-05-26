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

#ifndef YB_COMMON_TRANSACTION_TEST_UTIL_H
#define YB_COMMON_TRANSACTION_TEST_UTIL_H

#include <gtest/gtest.h>

#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/util/test_macros.h"

namespace yb {

class TransactionStatusManagerMock : public TransactionStatusManager {
 public:
  HybridTime LocalCommitTime(const TransactionId &id) override {
    return HybridTime::kInvalid;
  }

  void RequestStatusAt(const StatusRequest& request) override;

  void Commit(const TransactionId& txn_id, HybridTime commit_time) {
    ASSERT_TRUE(txn_commit_time_.emplace(txn_id, commit_time).second) << "Transaction " << txn_id
        << " has been already committed.";
  }

  Result<TransactionMetadata> PrepareMetadata(const TransactionMetadataPB& pb) override {
    return STATUS(Expired, "");
  }

  void Abort(const TransactionId& id, TransactionStatusCallback callback) override {
  }

  void Cleanup(TransactionIdSet&& set) override {
  }

  int64_t RegisterRequest() override {
    return 0;
  }

  void UnregisterRequest(int64_t) override {
  }

  void FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) override {}

  HybridTime MinRunningHybridTime() const override {
    return HybridTime::kMin;
  }

 private:
  std::unordered_map<TransactionId, HybridTime, TransactionIdHash> txn_commit_time_;
};

} // namespace yb

#endif // YB_COMMON_TRANSACTION_TEST_UTIL_H

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

#pragma once

#include <functional>
#include <type_traits>

#include <gtest/gtest.h>

#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/result.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

namespace yb {

class TransactionStatusManagerMock : public TransactionStatusManager {
 public:
  HybridTime LocalCommitTime(const TransactionId& id) override {
    return HybridTime::kInvalid;
  }

  boost::optional<TransactionLocalState> LocalTxnData(const TransactionId& id) override {
    return boost::none;
  }

  void RequestStatusAt(const StatusRequest& request) override;

  void Commit(const TransactionId& txn_id, HybridTime commit_time) {
    ASSERT_TRUE(txn_commit_time_.emplace(txn_id, commit_time).second) << "Transaction " << txn_id
        << " has been already committed.";
  }

  Result<TransactionMetadata> PrepareMetadata(const LWTransactionMetadataPB& pb) override {
    return STATUS(Expired, "");
  }

  void Abort(const TransactionId& id, TransactionStatusCallback callback) override {
  }

  Status Cleanup(TransactionIdSet&& set) override {
    return Status::OK();
  }

  Result<int64_t> RegisterRequest() override {
    return 0;
  }

  void UnregisterRequest(int64_t) override {
  }

  Status FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) override {
    return Status::OK();
  }

  Result<boost::optional<TabletId>> FindStatusTablet(const TransactionId& id) override {
    return boost::none;
  }

  HybridTime MinRunningHybridTime() const override {
    return HybridTime::kMin;
  }

  Result<HybridTime> WaitForSafeTime(HybridTime safe_time, CoarseTimePoint deadline) override {
    return STATUS(NotSupported, "WaitForSafeTime not implemented");
  }

  const TabletId& tablet_id() const override {
    static TabletId tablet_id;
    return tablet_id;
  }

  void RecordConflictResolutionKeysScanned(int64_t num_keys) override {}

  void RecordConflictResolutionScanLatency(MonoDelta latency) override {}

 private:
  std::unordered_map<TransactionId, HybridTime, TransactionIdHash> txn_commit_time_;
};

} // namespace yb

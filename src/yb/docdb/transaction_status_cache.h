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

#pragma once

#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

namespace yb {
namespace docdb {

// Caches transaction statuses fetched by single IntentAwareIterator.
// Thread safety is not required, because IntentAwareIterator is used in a single thread only.
class TransactionStatusCache {
 public:
  TransactionStatusCache(const TransactionOperationContext& txn_context_opt,
                         const ReadHybridTime& read_time,
                         CoarseTimePoint deadline)
      : txn_context_opt_(txn_context_opt), read_time_(read_time), deadline_(deadline) {}

  // Returns transaction commit time if already committed by the specified time or HybridTime::kMin
  // otherwise.
  Result<TransactionLocalState> GetTransactionLocalState(const TransactionId& transaction_id);

 private:
  struct GetCommitDataResult;

  boost::optional<TransactionLocalState> GetLocalCommitData(const TransactionId& transaction_id);
  Result<GetCommitDataResult> DoGetCommitData(const TransactionId& transaction_id);

  const TransactionOperationContext& txn_context_opt_;
  ReadHybridTime read_time_;
  CoarseTimePoint deadline_;
  std::unordered_map<TransactionId, TransactionLocalState, TransactionIdHash> cache_;
};

} // namespace docdb
} // namespace yb

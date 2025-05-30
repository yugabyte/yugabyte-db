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

#include <span>
#include <type_traits>

#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/dockv/dockv_fwd.h"
#include "yb/rocksdb/rocksdb_fwd.h"
#include "yb/tablet/tablet_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb::tablet {

YB_DEFINE_ENUM(RemoveReason,
               (kApplied)(kLargeApplied)(kProcessCleanup)(kStatusReceived)(kAbortReceived)
               (kShutdown)(kSetDB)(kCleanupAborts)(kNotFound)(kUnlock));

// Interface to object that should apply intents in RocksDB when transaction is applying.
class TransactionIntentApplier {
 public:
  virtual docdb::ApplyTransactionState ApplyIntents(const TransactionApplyData& data) = 0;
  virtual Status RemoveIntents(
      const RemoveIntentsData& data, RemoveReason reason,
      const TransactionId& transaction_id) = 0;
  virtual Status RemoveIntents(
      const RemoveIntentsData& data, RemoveReason reason,
      const TransactionIdSet& transactions) = 0;
  virtual Status WritePostApplyMetadata(
      std::span<const PostApplyTransactionMetadata> metadatas) = 0;

  virtual Status RemoveAdvisoryLocks(
      const TransactionId& transaction_id, rocksdb::DirectWriteHandler& handler) = 0;
  virtual Status RemoveAdvisoryLock(
      const TransactionId& transaction_id, const Slice& key,
      const dockv::IntentTypeSet& intent_types, rocksdb::DirectWriteHandler& handler) = 0;

  virtual HybridTime ApplierSafeTime(HybridTime min_allowed, CoarseTimePoint deadline) = 0;

  // See TransactionParticipant::WaitMinRunningHybridTime below
  virtual void MinRunningHybridTimeSatisfied() = 0;

 protected:
  ~TransactionIntentApplier() {}
};

}  // namespace yb::tablet

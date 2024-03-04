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

#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction.pb.h"
#include "yb/dockv/intent.h"
#include "yb/util/kv_util.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status.h"

namespace yb::docdb {

struct IntentData {
  dockv::IntentTypeSet types;
  bool full_doc_key;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(types, full_doc_key);
  }
};
// Container holding intent type and key type info for each intent within a specific transaction.
using IntentTypesContainer = std::map<KeyBuffer, IntentData>;

// Key and intent type info for a lock or conflicting write.
struct LockInfo {
  const RefCntPrefix doc_path;
  const dockv::IntentTypeSet intent_types;
};

// Represents the conflicts found from a specific subtransaction.
struct SubTransactionConflictInfo {
  bool has_non_lock_conflict;
  std::vector<LockInfo> locks;
};

// Represents the conflicts found from a specific transaction. May be updated based on the aborted
// subtransactions of that transaction.
struct TransactionConflictInfo {
  std::unordered_map<SubTransactionId, SubTransactionConflictInfo> subtransactions;

  // Remove any entries from subtransactions which appear to be aborted according to
  // aborted_subtxn_set.
  void RemoveAbortedSubtransactions(const SubtxnSet& aborted_subtxn_set);

  std::string ToString() const;
};
using TransactionConflictInfoPtr = std::shared_ptr<TransactionConflictInfo>;

// Represents the state of a transaction found to be in conflict with a request during conflict
// resolution. Used throughout the lifecycle of conflict resolution, including in the wait queue and
// in registering wait-for relationships to send to coordinators for deadlock detection.
struct TransactionConflictData {
  TransactionId id;
  TransactionConflictInfoPtr conflict_info;
  TransactionStatus status;
  HybridTime commit_time;
  uint64_t priority;
  Status failure;
  TabletId status_tablet = "";

  TransactionConflictData(TransactionId id_, const TransactionConflictInfoPtr& conflict_info_,
                          const TabletId& status_tablet_);

  // Update the state of this transaction based on result, including status, commit_time, failure,
  // and aborted subtransactions.
  void ProcessStatus(const TransactionStatusResult& result);

  std::string ToString() const;
};

// This class is responsible for managing conflicting transaction status during conflict resolution
// and providing convenient access to TransactionData for all active conflicting transactions
// as a request moves from conflict resolution, to the wait queue, and reports wait-for information
// to deadlock detection via LocalWaitingTxnRegistry.
//
// When using this class, all transactions should be added before any calls to
// FilterInactiveTransactions. Once FilterInactiveTransactions is called, it is invalid to call
// AddTransaction. The typical lifecycle is as follows:
// 1. Construct a ConflictDataManager instance
// 2. Add transactions
// 3. Filter out transactions based on local participant status or as transaction status RPCs
//    return their status
// 4. Pass around ConflictDataManager instance to enable easy access to remaining transaction data
//
// Deviating from this pattern by e.g. adding transactions after some have been filtered will cause
// crashes in debug builds and incorrect behavior in release builds.
class ConflictDataManager {
 public:
  explicit ConflictDataManager(size_t capacity);

  void AddTransaction(
      const TransactionId& id, const TransactionConflictInfoPtr& conflict_info,
      const TabletId& status_tablet = "");

  boost::iterator_range<TransactionConflictData*> RemainingTransactions();

  size_t NumActiveTransactions() const;

  // Apply specified functor to all active (i.e. not resolved) transactions.
  // If functor returns true, it means that transaction was resolved.
  // So such transaction is moved out of active transactions range.
  // Returns true if there are no active transaction left.
  using InactiveTxnFilter = std::function<Result<bool>(TransactionConflictData*)>;
  Result<bool> FilterInactiveTransactions(const InactiveTxnFilter& filter);

  // Output a Slice of data representing the state of this instance, as expected by the
  // YB_TRANSACTION_DUMP macro.
  Slice DumpConflicts() const;

  std::string ToString() const;

 private:
  std::vector<TransactionConflictData> transactions_;
  size_t remaining_transactions_;
#ifndef NDEBUG
  std::atomic_bool has_filtered_ = false;
#endif // NDEBUG
};

std::ostream& operator<<(std::ostream& out, const ConflictDataManager& data);

} // namespace yb::docdb

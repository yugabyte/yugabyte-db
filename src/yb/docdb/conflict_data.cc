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

#include "yb/docdb/conflict_data.h"
#include "yb/common/transaction.h"

namespace yb::docdb {

void TransactionConflictInfo::RemoveAbortedSubtransactions(const SubtxnSet& aborted_subtxn_set) {
  auto it = subtransactions.begin();
  while (it != subtransactions.end()) {
    if (aborted_subtxn_set.Test(it->first)) {
      it = subtransactions.erase(it);
    } else {
      it++;
    }
  }
}

std::string TransactionConflictInfo::ToString() const {
  return Format("{ subtransactions: $0 }", subtransactions.size());
}

TransactionConflictData::TransactionConflictData(
    TransactionId id_, const TransactionConflictInfoPtr& conflict_info_,
    const TabletId& status_tablet_):
  id(id_), conflict_info(DCHECK_NOTNULL(conflict_info_)), status_tablet(status_tablet_) {}

void TransactionConflictData::ProcessStatus(const TransactionStatusResult& result) {
  status = result.status;
  if (status == TransactionStatus::COMMITTED) {
    LOG_IF(DFATAL, !result.status_time.is_valid())
        << "Status time not specified for committed transaction: " << id;
    commit_time = result.status_time;
  }
  if (status != TransactionStatus::ABORTED) {
    DCHECK(status == TransactionStatus::PENDING || status == TransactionStatus::COMMITTED);
    conflict_info->RemoveAbortedSubtransactions(result.aborted_subtxn_set);
  }
}

std::string TransactionConflictData::ToString() const {
  return Format("{ id: $0 status: $1 commit_time: $2 priority: $3 failure: $4 }",
                id, TransactionStatus_Name(status), commit_time, priority, failure);
}


ConflictDataManager::ConflictDataManager(size_t capacity) {
  transactions_.reserve(capacity);
}

void ConflictDataManager::AddTransaction(
    const TransactionId& id, const TransactionConflictInfoPtr& conflict_info,
    const TabletId& status_tablet) {
#ifndef NDEBUG
  DCHECK(!has_filtered_);
#endif // NDEBUG
  transactions_.emplace_back(id, conflict_info, status_tablet);
  remaining_transactions_ = transactions_.size();
}

boost::iterator_range<TransactionConflictData*> ConflictDataManager::RemainingTransactions() {
  auto begin = transactions_.data();
  return boost::make_iterator_range(begin, begin + remaining_transactions_);
}

size_t ConflictDataManager::NumActiveTransactions() const {
  return remaining_transactions_;
}

// Apply specified functor to all active (i.e. not resolved) transactions.
// If functor returns true, it means that transaction was resolved.
// So such transaction is moved out of active transactions range.
// Returns true if there are no active transaction left.
Result<bool> ConflictDataManager::FilterInactiveTransactions(const InactiveTxnFilter& filter) {
#ifndef NDEBUG
  has_filtered_.exchange(true);
#endif // NDEBUG

  auto end = transactions_.begin() + remaining_transactions_;
  for (auto transaction_it = transactions_.begin(); transaction_it != end;) {
    if (!VERIFY_RESULT(filter(&*transaction_it))) {
      ++transaction_it;
      continue;
    }
    if (--end == transaction_it) {
      break;
    }
    std::swap(*transaction_it, *end);
  }
  remaining_transactions_ = end - transactions_.begin();

  return remaining_transactions_ == 0;
}

Slice ConflictDataManager::DumpConflicts() const {
  return Slice(pointer_cast<const uint8_t*>(transactions_.data()),
               transactions_.size() * sizeof(transactions_[0]));
}

std::string ConflictDataManager::ToString() const {
  return AsString(transactions_);
}

std::ostream& operator<<(std::ostream& out, const ConflictDataManager& data) {
  return out << data.ToString();
}

} // namespace yb::docdb

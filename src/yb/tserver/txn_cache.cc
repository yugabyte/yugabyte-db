// Copyright (c) Yugabyte, Inc.
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

#include <set>

#include "yb/tserver/txn_cache.h"

namespace yb::tserver {

void TransactionCache::InsertPlainTransaction(uint64_t session_id, const TransactionId& txn_id) {
  std::lock_guard lock(mutex_);
  auto emplace_result = cache_.emplace(session_id, SessionTransactionState());
  emplace_result.first->second.plain_txn_ = txn_id;
}

void TransactionCache::InsertDdlTransaction(uint64_t session_id, const TransactionId& txn_id) {
  std::lock_guard lock(mutex_);
  auto emplace_result = cache_.emplace(session_id, SessionTransactionState());
  emplace_result.first->second.ddl_txn_ = txn_id;
}

void TransactionCache::ErasePlainTransaction(uint64_t session_id) {
  std::lock_guard lock(mutex_);
  if (!cache_.contains(session_id)) {
    return;
  }

  cache_[session_id].plain_txn_ = TransactionId::Nil();
}

void TransactionCache::EraseDdlTransaction(uint64_t session_id) {
  std::lock_guard lock(mutex_);
  if (!cache_.contains(session_id)) {
    return;
  }

  cache_[session_id].ddl_txn_ = TransactionId::Nil();
}

void TransactionCache::EraseTransactionById(const TransactionId& txn_id) {
  std::lock_guard lock(mutex_);
  for (auto& entry : cache_) {
    auto& session_state = entry.second;
    if (session_state.ddl_txn_ == txn_id) {
      session_state.ddl_txn_ = TransactionId::Nil();
      return;
    }

    if (session_state.plain_txn_ == txn_id) {
      session_state.plain_txn_ = TransactionId::Nil();
      return;
    }
  }
}

void TransactionCache::EraseTransactionEntries(uint64_t session_id) {
  std::lock_guard lock(mutex_);
  cache_.erase(session_id);
}

void TransactionCache::ClearCache() {
  std::lock_guard lock(mutex_);
  cache_.clear();
}

const TransactionId* TransactionCache::DoGetTransactionInfo(
    const TransactionCacheConstIter& session_iter) REQUIRES(mutex_) {
  const auto& txn_state = session_iter->second;

  // TODO(kramanathan): Return the DDL txn in preference to the plain txn until GHI #18451 is
  // resolved.
  if (txn_state.ddl_txn_) {
    return &txn_state.ddl_txn_;
  }

  if (txn_state.plain_txn_) {
    return &txn_state.plain_txn_;
  }

  return nullptr;
}

void TransactionCache::DoCopyTransactionInfo(
    const TransactionCacheConstIter& session_iter, PgGetActiveTransactionListResponsePB* resp)
    REQUIRES(mutex_) {
  const auto& txn_id = DoGetTransactionInfo(session_iter);
  if (!txn_id) {
    return;
  }

  auto& entry = *(resp->add_entries());
  entry.set_session_id(session_iter->first);
  txn_id->AsSlice().CopyToBuffer(entry.mutable_txn_id());
}

void TransactionCache::CopyTransactionInfo(
    uint64_t session_id, PgGetActiveTransactionListResponsePB* resp) {
  std::lock_guard lock(mutex_);
  const auto& session_iter = cache_.find(session_id);
  if (session_iter == cache_.cend()) {
    return;
  }

  DoCopyTransactionInfo(session_iter, resp);
}

void TransactionCache::CopyTransactionInfoForAllSessions(
    PgGetActiveTransactionListResponsePB* resp) {
  std::lock_guard lock(mutex_);

  for (auto iter = cache_.cbegin(); iter != cache_.cend(); ++iter) {
    DoCopyTransactionInfo(iter, resp);
  }
}

}  // namespace yb::tserver

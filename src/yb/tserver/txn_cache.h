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

#pragma once

#include <map>
#include <set>

#include "yb/common/transaction.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/util/locks.h"

namespace yb::tserver {

// TransactionCache caches transaction information of all sessions known to the t-server.
// The main purpose of this class is to provide fast access to transaction info (to a requesting
// client like pg_stat_activity) without having to acquire exclusive per-session locks.
class TransactionCache {
 public:
  class SessionTransactionState {
   public:
    SessionTransactionState() : plain_txn_(TransactionId::Nil()), ddl_txn_(TransactionId::Nil()) {}
    TransactionId plain_txn_;
    TransactionId ddl_txn_;
  };

  using TransactionCacheConstIter =
      const std::map<uint64_t, SessionTransactionState>::const_iterator;

  void InsertPlainTransaction(uint64_t session_id, const TransactionId& txn_id);
  void InsertDdlTransaction(uint64_t session_id, const TransactionId& txn_id);
  void ErasePlainTransaction(uint64_t session_id);
  void EraseDdlTransaction(uint64_t session_id);
  void EraseTransactionById(const TransactionId& txn_id);
  void EraseTransactionEntries(uint64_t session_id);
  void ClearCache();

  void CopyTransactionInfo(uint64_t session_id, PgGetActiveTransactionListResponsePB* resp);
  void CopyTransactionInfoForAllSessions(PgGetActiveTransactionListResponsePB* resp);

 private:
  const TransactionId* DoGetTransactionInfo(const TransactionCacheConstIter& session_iter)
      REQUIRES(mutex_);
  void DoCopyTransactionInfo(
      const TransactionCacheConstIter& session_iter, PgGetActiveTransactionListResponsePB* resp)
      REQUIRES(mutex_);

  mutable simple_spinlock mutex_;
  std::map<uint64_t, SessionTransactionState> cache_ GUARDED_BY(mutex_);
};

}  // namespace yb::tserver

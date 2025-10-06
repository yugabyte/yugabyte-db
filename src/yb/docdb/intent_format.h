// Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/transaction_status_cache.h"

#include "yb/dockv/intent.h"
#include "yb/dockv/key_bytes.h"

#include "yb/rocksdb/db.h"

#include "yb/util/status_fwd.h"

namespace yb::docdb {

struct IntentFetchKeyResult {
  Slice key;
  Slice encoded_key;
  DocHybridTime write_time;
  bool same_transaction;
};

struct DecodeStrongWriteIntentResult {
  Slice intent_prefix;
  Slice intent_value;
  EncodedDocHybridTime intent_time;
  EncodedDocHybridTime value_time;
  dockv::IntentTypeSet intent_types;

  // Whether this intent from the same transaction as specified in context.
  bool same_transaction = false;

  std::string ToString() const;

  // Returns the upper limit for the "value time" of an intent in order for the intent to be visible
  // in the read results. The "value time" is defined as follows:
  //   - For uncommitted transactions, the "value time" is the time when the intent was written.
  //     Note that same_transaction or in_txn_limit could only be set for uncommited transactions.
  //   - For committed transactions, the "value time" is the commit time.
  //
  // The logic here is as follows:
  //   - When a transaction is reading its own intents, the in_txn_limit allows a statement to
  //     avoid seeing its own partial results. This is necessary for statements such as INSERT ...
  //     SELECT to avoid reading rows that the same statement generated and going into an infinite
  //     loop.
  //   - If an intent's hybrid time is greater than the tablet's local limit, then this intent
  //     cannot lead to a read restart and we only need to see it if its commit time is less than or
  //     equal to read_time.
  //   - If an intent's hybrid time is <= than the tablet's local limit, then we cannot claim that
  //     the intent was written after the read transaction began based on the local limit, and we
  //     must compare the intent's commit time with global_limit and potentially perform a read
  //     restart, because the transaction that wrote the intent might have been committed before our
  //     read transaction begin.
  const EncodedDocHybridTime& MaxAllowedValueTime(const EncodedReadHybridTime& read_time) const;
};

inline std::ostream& operator<<(std::ostream& out, const DecodeStrongWriteIntentResult& result) {
  return out << result.ToString();
}

// Decodes intent based on intent_iterator and its transaction commit time if intent is a strong
// write intent, intent is not for row locking, and transaction is already committed at specified
// time or is current transaction.
// Returns HybridTime::kMin as value_time otherwise.
// For current transaction returns intent record hybrid time as value_time.
// Consumes intent from value_slice leaving only value itself.
Result<DecodeStrongWriteIntentResult> DecodeStrongWriteIntent(
    const TransactionOperationContext& txn_op_context,
    rocksdb::Iterator* intent_iter,
    TransactionStatusCache* transaction_status_cache);

Status EnumerateIntents(
    const ArenaList<LWKeyValuePairPB>& kv_pairs,
    const dockv::EnumerateIntentsCallback& functor,
    dockv::PartialRangeKeyIntents partial_range_key_intents,
    dockv::SkipPrefixLocks skip_prefix_locks = dockv::SkipPrefixLocks::kFalse);

// Class that is used while combining external intents into single key value pair.
class ExternalIntentsProvider {
 public:
  // Set output key.
  virtual void SetKey(const Slice& slice) = 0;

  // Set output value.
  virtual void SetValue(const Slice& slice) = 0;

  // Get next external intent, returns false when there are no more intents.
  virtual std::optional<std::pair<Slice, Slice>> Next() = 0;

  virtual const Uuid& InvolvedTablet() = 0;

  virtual ~ExternalIntentsProvider() = default;
};

// Combine external intents into single key value pair.
void CombineExternalIntents(
    const TransactionId& txn_id,
    SubTransactionId subtransaction_id,
    ExternalIntentsProvider* provider);

}  // namespace yb::docdb

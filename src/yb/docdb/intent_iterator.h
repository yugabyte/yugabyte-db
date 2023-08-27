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

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/dockv/intent.h"
#include "yb/docdb/intent_aware_iterator_interface.h"
#include "yb/dockv/key_bytes.h"
#include "yb/docdb/transaction_status_cache.h"

#include "yb/rocksdb/db.h"

#include "yb/util/status_fwd.h"

namespace yb::docdb {

struct IntentFetchKeyResult {
  Slice key;
  Slice encoded_key;
  DocHybridTime write_time;
  bool same_transaction;
};

// IntentIterator is a wrapper on Intents RocksDB instance which filters out
// any weak operations except when they are part of same transaction.
//
// For intents from the same transaction, intents with maximum HT would be picked -- ignoring
// read_time for filtering purposes -- and returned as key with time equal to read_time.
// Intent data format:
//   SubDocKey (no HybridTime) + IntentType + HybridTime -> TxnId + value.
// TxnId, IntentType, HybridTime are all prefixed with their respective value types.
//
// TODO: currently to resolve an intent value, we need to iterate over all the intents
// for a subkey. Since its possible that older HT value in an intent is the latest value
// since TX was committed later. Figure out if there is a better way to extract the latest
// value. Open question: can we stop the scan if we have found a version of subdockey within the
// same transaction, presuming there won't be any other conflicting writes in progress?
class IntentIterator {
 public:
  IntentIterator(
      rocksdb::DB* intents_db,
      const KeyBounds* docdb_key_bounds,
      CoarseTimePoint deadline,
      const ReadHybridTime& read_time,
      const TransactionOperationContext& txn_op_context,
      const Slice* iterate_upper_bound);

  bool Initialized() const { return intent_iter_.Initialized(); }

  bool valid() const;

  void Seek(const Slice& target);

  void Seek(dockv::KeyBytes* target);

  void SeekForward(dockv::KeyBytes* target);

  void Next();

  Result<IntentFetchKeyResult> FetchKey() const;

  Slice value() const;

  void SeekOutOfSubKey(dockv::KeyBytes* target);

  HybridTime max_seen_ht() { return max_seen_ht_; }

  void SetUpperbound(const Slice& upperbound) {
    upperbound_ = upperbound;
  }

  void DebugDump();

  std::string DebugPosToString();

 private:
  // Returns true of key is within the specified bound.
  bool SatisfyBounds(const Slice& key);

  // Seek intent iterator forward (backward) to latest suitable intent for first available
  // key. Updates resolved_intent_XXX fields.
  // intent_iter_ will be positioned to first intent for the smallest (biggest) key
  // greater (smaller) than resolved_intent_sub_doc_key_encoded_.
  template <Direction direction>
  void SeekToSuitableIntent();

  // Seek the intent iterator out of the Txn region.
  template <Direction direction>
  inline void SkipTxnRegion();

  // Decodes intent at intent_iter_ position and updates resolved_intent_* fields if that intent
  // matches all following conditions:
  // 1. It is strong write intent.
  // 2. It is either (a) written at value_time by transaction associated with txn_op_context_
  // (current transaction) earlier than high_ht_ or (b) its transaction is already committed and
  // value_time = commit time is earlier than high_ht_.
  // 3. It has value_time earlier than current resolved intent if we already have resolved intent.
  //
  // Preconditions:
  // 1. If has_resolved_intent_ is true then intent should be exactly for the same key as
  // resolved_intent_key_prefix_.
  //
  // Note: For performance reasons resolved_intent_sub_doc_key_encoded_ is not updated by this
  // function and should be updated after we have found latest suitable intent for the key by
  // calling UpdateResolvedIntentSubDocKeyEncoded.
  void ProcessIntent();

  void UpdateResolvedIntentSubDocKeyEncoded();

  inline void AppendStrongWriteAndPerformSeek(
      dockv::KeyBytes* key_bytes, bool seek_forward = false);

  void HandleStatus(const Status& status);

 private:
  const ReadHybridTime read_time_;

  const TransactionOperationContext txn_op_context_;
  TransactionStatusCache transaction_status_cache_;

  docdb::BoundedRocksDbIterator intent_iter_;

  Status status_;

  // Upperbound for seek. If we see intent record past this bound, it will be ignored.
  Slice upperbound_;

  // SubDocKey (no HT).
  dockv::KeyBytes resolved_intent_key_prefix_;

  // DocHybridTime of resolved_intent_sub_doc_key_encoded_ is set to commit time or intent time in
  // case of intent is written by current transaction (stored in txn_op_context_).
  DocHybridTime resolved_intent_txn_dht_;
  DocHybridTime intent_dht_from_same_txn_ = DocHybridTime::kMin;
  dockv::KeyBytes resolved_intent_sub_doc_key_encoded_;

  mutable HybridTime max_seen_ht_;

  dockv::KeyBytes resolved_intent_value_;
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

Slice StrongWriteSuffix(Slice key);

inline Slice StrongWriteSuffix(const dockv::KeyBytes& key) {
  return StrongWriteSuffix(key.AsSlice());
}

void AppendStrongWrite(dockv::KeyBytes* out);

}  // namespace yb::docdb

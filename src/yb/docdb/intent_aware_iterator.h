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

#ifndef YB_DOCDB_INTENT_AWARE_ITERATOR_H_
#define YB_DOCDB_INTENT_AWARE_ITERATOR_H_

#include <boost/optional/optional.hpp>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/key_bytes.h"

namespace yb {

class HybridTime;
class DocHybridTime;
class TransactionStatusProvider;

namespace docdb {

class Value;

// Provides a way to iterate over DocDB (sub)keys with respect to committed intents transparently
// for caller. Implementation relies on intents order in RocksDB, which is determined by intent key
// format. If (sub)key A goes before/after (sub)key B, all intents for A should go before/after all
// intents for (sub)key B.
// Might ignore values with HT higher than high_ht for performance optimization.
// Intent data format:
//   kIntentPrefix + SubDocKey (no HybridTime) + IntentType + HybridTime -> TxnId + value.
// TxnId, IntentType, HybridTime all prefixed with appropriate value type.
class IntentAwareIterator {
 public:
  IntentAwareIterator(
      rocksdb::DB* rocksdb,
      const rocksdb::ReadOptions& read_opts,
      HybridTime high_ht,
      const TransactionOperationContextOpt& txn_op_context)
      : high_ht_(high_ht), txn_op_context_(txn_op_context),
        iter_(rocksdb->NewIterator(read_opts)),
        intent_iter_(txn_op_context.is_initialized() ? rocksdb->NewIterator(read_opts) : nullptr) {
  }
  IntentAwareIterator(const IntentAwareIterator& other) = delete;
  void operator=(const IntentAwareIterator& other) = delete;

  // Seek to the smallest key which is greater or equal than doc_key.
  CHECKED_STATUS Seek(const DocKey& doc_key);

  // Seek to the smallest which is greater or equal than doc_key. If we seek to exact doc_key,
  // it will have time no later than hybrid_time.
  CHECKED_STATUS Seek(const DocKey& doc_key, const HybridTime& hybrid_time);

  // Seek to specified encoded key (it is responsibility of caller to make sure it doesn't have
  // hybrid time).
  CHECKED_STATUS SeekWithoutHt(const KeyBytes& key_bytes);

  // Seek forward to specified encoded key (it is responsibility of caller to make sure it
  // doesn't have hybrid time).
  CHECKED_STATUS SeekForwardWithoutHt(const KeyBytes& key_bytes);

  // Seek forward to specified subdoc key.
  CHECKED_STATUS SeekForward(const SubDocKey& subdoc_key);

  // Seek past specified subdoc key.
  CHECKED_STATUS SeekPastSubKey(const SubDocKey& subdoc_key);

  // Seek out of subdoc key.
  CHECKED_STATUS SeekOutOfSubDoc(const SubDocKey& subdoc_key);

  bool valid();
  Slice key();
  Slice value();

  // If there is a key equal to key_bytes_without_ht + some timestamp, which is later than
  // max_deleted_ts, we update max_deleted_ts and result_value (unless it is nullptr).
  // This should not be used for leaf nodes. - Why? Looks like it is already used for leaf nodes
  // also.
  // Note: it is responsibility of caller to make sure key_bytes_without_ht doesn't have hybrid
  // time.
  // TODO: We could also check that the value is kTombStone or kObject type for sanity checking - ?
  // It could be a simple value as well, not necessarily kTombstone or kObject.
  CHECKED_STATUS FindLastWriteTime(
      const KeyBytes& key_bytes_without_ht,
      HybridTime scan_ht,
      DocHybridTime* max_deleted_ts,
      Value* result_value);

 private:
  // Seek on regular sub-iterator. Regular key-value pairs are final non-intent values written to
  // RocksDB either directly bypassing cross-shard transactions or already resolved from intents
  // during intents cleanup.
  void SeekRegular(const KeyBytes& key_bytes);

  // Seek forward on regular sub-iterator.
  void SeekForwardRegular(const KeyBytes& key_bytes);

  // Strong write intents which are either committed or written by the current
  // transaction (stored in txn_op_context) by considered time are considered as suitable.

  // Seek intent sub-iterator to latest suitable intent starting with intent_key_prefix.
  // If we seek for exact key specified by intent_key_prefix, it will have time no later than
  // high_ht.
  // intent_iter_ will be positioned to first intent for the smallest key greater than
  // resolved_intent_sub_doc_key_encoded_.
  // If is_forward flag is specified and iterator already positioned far enough - does not perform
  // seek.
  CHECKED_STATUS SeekToSuitableIntent(const KeyBytes &intent_key_prefix, HybridTime high_ht,
      bool is_forward = false);

  // Seek intent sub-iterator forward to latest suitable intent for first available key.
  // intent_iter_ will be positioned to first intent for the smallest key greater than
  // resolved_intent_sub_doc_key_encoded_.
  CHECKED_STATUS SeekForwardToSuitableIntent();

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
  CHECKED_STATUS ProcessIntent(const HybridTime& high_ht);

  void UpdateResolvedIntentSubDocKeyEncoded();
  void DebugDump();

  // Whether current entry is regular key-value pair.
  bool IsEntryRegular();

  const HybridTime high_ht_; // Ignoring values with higher HT.
  const TransactionOperationContextOpt txn_op_context_;
  std::unique_ptr<rocksdb::Iterator> iter_;
  std::unique_ptr<rocksdb::Iterator> intent_iter_;

  // Following fields contain information related to resolved suitable intent.
  bool has_resolved_intent_ = false;
  // kIntentPrefix + SubDocKey (no HT).
  KeyBytes resolved_intent_key_prefix_;
  // DocHybridTime of resolved_intent_sub_doc_key_encoded_ is set to commit time or intent time in
  // case of intent is written by current transaction (stored in txn_op_context_).
  DocHybridTime resolved_intent_txn_dht_;
  KeyBytes resolved_intent_sub_doc_key_encoded_;
  KeyBytes resolved_intent_value_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_INTENT_AWARE_ITERATOR_H_

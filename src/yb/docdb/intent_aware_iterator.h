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

#include "yb/common/read_hybrid_time.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/key_bytes.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"

namespace yb {

class DocHybridTime;
class TransactionStatusManager;

namespace docdb {

class Value;

YB_DEFINE_ENUM(ResolvedIntentState, (kNoIntent)(kInvalidPrefix)(kValid));

// Provides a way to iterate over DocDB (sub)keys with respect to committed intents transparently
// for caller. Implementation relies on intents order in RocksDB, which is determined by intent key
// format. If (sub)key A goes before/after (sub)key B, all intents for A should go before/after all
// intents for (sub)key B.
//
// When seeking to some (sub)dockey, it will ignore values for this (sub)dockey
// (except write intents written by the same transaction) with HT higher than high_ht.
//
// For intents from the same transaction, intent with maximal HT would be picked, ignoring high_ht.
// And returned as key with time equals to high_ht.
// Intent data format:
//   kIntentPrefix + SubDocKey (no HybridTime) + IntentType + HybridTime -> TxnId + value.
// TxnId, IntentType, HybridTime are all prefixed with their respective value types.
//
// KeyBytes passed to Seek* methods should not contain hybrid time.
// HybridTime of subdoc_key in Seek* methods would be ignored.
class IntentAwareIterator {
 public:
  IntentAwareIterator(
      rocksdb::DB* rocksdb,
      const rocksdb::ReadOptions& read_opts,
      const ReadHybridTime& read_time,
      const TransactionOperationContextOpt& txn_op_context);

  IntentAwareIterator(const IntentAwareIterator& other) = delete;
  void operator=(const IntentAwareIterator& other) = delete;

  // Seek to the smallest key which is greater or equal than doc_key.
  void Seek(const DocKey& doc_key);

  // Seek to specified encoded key (it is responsibility of caller to make sure it doesn't have
  // hybrid time).
  void SeekWithoutHt(const Slice& key);

  // Seek forward to specified encoded key (it is responsibility of caller to make sure it
  // doesn't have hybrid time).
  void SeekForwardWithoutHt(const Slice& key);

  // Seek forward to specified subdoc key.
  void SeekForwardIgnoreHt(const SubDocKey& subdoc_key);

  // Seek past specified subdoc key.
  void SeekPastSubKey(const SubDocKey& subdoc_key);

  // Seek out of subdoc key.
  void SeekOutOfSubDoc(const SubDocKey& subdoc_key);

  // Seek to last doc key.
  void SeekToLastDocKey();

  // This method positions the iterator at the beginning of the DocKey found before the doc_key
  // provided
  void PrevDocKey(const DocKey& doc_key);

  // Adds new value to prefix stack. The top value of this stack is used to filter
  // returned entries. After seek we check whether currently pointed value has active prefix.
  // If not, than it means that we are out of range of interest and iterator becomes invalid.
  void PushPrefix(const Slice& prefix);

  // Removes top value from prefix stack. This iteration could became valid after poping prefix,
  // if new top prefix is a prefix of currently pointed value.
  void PopPrefix();

  // Fetches currently pointed key and also updates max_seen_ht to ht of this key.
  Result<Slice> FetchKey();

  bool valid();
  Slice value();
  ReadHybridTime read_time() { return read_time_; }
  HybridTime max_seen_ht() { return max_seen_ht_; }

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
      DocHybridTime* max_deleted_ts,
      Value* result_value);

 private:
  // Seek forward on regular sub-iterator.
  void SeekForwardRegular(const Slice& slice, const Slice& prefix = Slice());

  // Skips regular entries with hybrid time after read limit.
  void SkipFutureRecords();

  // Skips intents with hybrid time after read limit.
  void SkipFutureIntents();

  // Strong write intents which are either committed or written by the current
  // transaction (stored in txn_op_context) by considered time are considered as suitable.

  // Seek intent sub-iterator to latest suitable intent starting with intent_key_prefix.
  // intent_iter_ will be positioned to first intent for the smallest key greater than
  // resolved_intent_sub_doc_key_encoded_.
  // If iterator already positioned far enough - does not perform seek.
  void SeekForwardToSuitableIntent(const KeyBytes &intent_key_prefix);

  // Seek intent sub-iterator forward to latest suitable intent for first available key.
  // intent_iter_ will be positioned to first intent for the smallest key greater than
  // resolved_intent_sub_doc_key_encoded_.
  void SeekForwardToSuitableIntent();

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
  void DebugDump();

  // Whether current entry is regular key-value pair.
  bool IsEntryRegular();

  const ReadHybridTime read_time_;
  const TransactionOperationContextOpt txn_op_context_;
  std::unique_ptr<rocksdb::Iterator> intent_iter_;
  std::unique_ptr<rocksdb::Iterator> iter_;
  bool iter_valid_ = false;
  Status status_;
  HybridTime max_seen_ht_ = HybridTime::kMin;

  // Following fields contain information related to resolved suitable intent.
  ResolvedIntentState resolved_intent_state_ = ResolvedIntentState::kNoIntent;
  // kIntentPrefix + SubDocKey (no HT).
  KeyBytes resolved_intent_key_prefix_;
  // DocHybridTime of resolved_intent_sub_doc_key_encoded_ is set to commit time or intent time in
  // case of intent is written by current transaction (stored in txn_op_context_).
  DocHybridTime resolved_intent_txn_dht_;
  DocHybridTime intent_dht_from_same_txn_ = DocHybridTime::kMin;
  KeyBytes resolved_intent_sub_doc_key_encoded_;
  KeyBytes resolved_intent_value_;
  std::vector<Slice> prefix_stack_;
};

// Utility class that controls stack of prefixes in IntentAwareIterator.
class IntentAwareIteratorPrefixScope {
 public:
  IntentAwareIteratorPrefixScope(const Slice& prefix, IntentAwareIterator* iterator)
      : iterator_(iterator) {
    iterator->PushPrefix(prefix);
  }

  ~IntentAwareIteratorPrefixScope() {
    iterator_->PopPrefix();
  }

 private:
  IntentAwareIterator* iterator_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_INTENT_AWARE_ITERATOR_H_

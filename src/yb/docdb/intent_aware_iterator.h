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

// Caches transaction statuses fetched by single IntentAwareIterator.
// Thread safety is not required, because IntentAwareIterator is used in a single thread only.
class TransactionStatusCache {
 public:
  TransactionStatusCache(TransactionStatusManager* txn_status_manager,
                         const ReadHybridTime& read_time,
                         MonoTime deadline)
      : txn_status_manager_(txn_status_manager), read_time_(read_time), deadline_(deadline) {}

  // Returns transaction commit time if already committed by the specified time or HybridTime::kMin
  // otherwise.
  Result<HybridTime> GetCommitTime(const TransactionId& transaction_id);

 private:
  HybridTime GetLocalCommitTime(const TransactionId& transaction_id);
  Result<HybridTime> DoGetCommitTime(const TransactionId& transaction_id);

  TransactionStatusManager* txn_status_manager_;
  ReadHybridTime read_time_;
  MonoTime deadline_;
  std::unordered_map<TransactionId, HybridTime, TransactionIdHash> cache_;
};

// Provides a way to iterate over DocDB (sub)keys with respect to committed intents transparently
// for caller. Implementation relies on intents order in RocksDB, which is determined by intent key
// format. If (sub)key A goes before/after (sub)key B, all intents for A should go before/after all
// intents for (sub)key B.
//
// When seeking to some (sub)dockey, it will ignore values for this (sub)dockey
// (except write intents written by the same transaction) with HT higher than high_ht.
//
// For intents from the same transaction, intent with maximum HT would be picked, ignoring high_ht.
// And returned as key with time equals to high_ht.
// Intent data format:
//   SubDocKey (no HybridTime) + IntentType + HybridTime -> TxnId + value.
// TxnId, IntentType, HybridTime are all prefixed with their respective value types.
//
// KeyBytes passed to Seek* methods should not contain hybrid time.
// HybridTime of subdoc_key in Seek* methods would be ignored.
class IntentAwareIterator {
 public:
  IntentAwareIterator(
      const DocDB& doc_db,
      const rocksdb::ReadOptions& read_opts,
      MonoTime deadline,
      const ReadHybridTime& read_time,
      const TransactionOperationContextOpt& txn_op_context);

  IntentAwareIterator(const IntentAwareIterator& other) = delete;
  void operator=(const IntentAwareIterator& other) = delete;

  // Seek to the smallest key which is greater or equal than doc_key.
  void Seek(const DocKey& doc_key);

  // Seek to specified encoded key (it is responsibility of caller to make sure it doesn't have
  // hybrid time).
  void Seek(const Slice& key);

  // Seek forward to specified encoded key (it is responsibility of caller to make sure it
  // doesn't have hybrid time). For efficiency, the method that takes a non-const KeyBytes pointer
  // avoids memory allocation by using the KeyBytes buffer to prepare the key to seek to, and may
  // append up to kMaxBytesPerEncodedHybridTime + 1 bytes of data to the buffer. The appended data
  // is removed when the method returns.
  void SeekForward(const Slice& key);
  void SeekForward(KeyBytes* key);

  // Seek past specified subdoc key (it is responsibility of caller to make sure it doesn't have
  // hybrid time).
  void SeekPastSubKey(const Slice& key);

  // Seek out of subdoc key (it is responsibility of caller to make sure it doesn't have hybrid
  // time).
  void SeekOutOfSubDoc(const Slice& key);
  // For efficiency, this overload takes a non-const KeyBytes pointer avoids memory allocation by
  // using the KeyBytes buffer to prepare the key to seek to by appending an extra byte. The
  // appended byte is removed when the method returns.
  void SeekOutOfSubDoc(KeyBytes* key_bytes);

  // Seek to last doc key.
  void SeekToLastDocKey();

  // This method positions the iterator at the beginning of the DocKey found before the doc_key
  // provided
  void PrevDocKey(const DocKey& doc_key);

  // Adds new value to prefix stack. The top value of this stack is used to filter returned entries.
  void PushPrefix(const Slice& prefix);

  // Removes top value from prefix stack. This iteration could became valid after poping prefix,
  // if new top prefix is a prefix of currently pointed value.
  void PopPrefix();

  // Fetches currently pointed key and also updates max_seen_ht to ht of this key. The key does not
  // contain the DocHybridTime but is returned separately and optionally.
  Result<Slice> FetchKey(DocHybridTime* doc_ht = nullptr);

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
      const Slice& key_without_ht,
      DocHybridTime* max_deleted_ts,
      Value* result_value = nullptr);

 private:
  // Seek forward on regular sub-iterator.
  void SeekForwardRegular(const Slice& slice);

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

  // Set the exclusive upperbound of the intent iterator to the current SubDocKey of the regular
  // iterator. This is necessary to avoid RocksDB iterator from scanning over the deleted intents
  // beyond the current regular key unnecessarily.
  CHECKED_STATUS SetIntentUpperbound();

  const ReadHybridTime read_time_;
  const string encoded_read_time_local_limit_;
  const string encoded_read_time_global_limit_;
  const TransactionOperationContextOpt txn_op_context_;
  std::unique_ptr<rocksdb::Iterator> intent_iter_;
  std::unique_ptr<rocksdb::Iterator> iter_;
  bool iter_valid_ = false;
  Status status_;
  HybridTime max_seen_ht_ = HybridTime::kMin;

  // Exclusive upperbound of the intent key.
  KeyBytes intent_upperbound_keybytes_;
  Slice intent_upperbound_;

  // Following fields contain information related to resolved suitable intent.
  ResolvedIntentState resolved_intent_state_ = ResolvedIntentState::kNoIntent;
  // SubDocKey (no HT).
  KeyBytes resolved_intent_key_prefix_;

  // DocHybridTime of resolved_intent_sub_doc_key_encoded_ is set to commit time or intent time in
  // case of intent is written by current transaction (stored in txn_op_context_).
  DocHybridTime resolved_intent_txn_dht_;
  DocHybridTime intent_dht_from_same_txn_ = DocHybridTime::kMin;
  KeyBytes resolved_intent_sub_doc_key_encoded_;
  KeyBytes resolved_intent_value_;
  std::vector<Slice> prefix_stack_;
  TransactionStatusCache transaction_status_cache_;

  bool skip_future_records_needed_ = false;
  bool skip_future_intents_needed_ = false;

  // Reusable buffer to prepare seek key to avoid reallocating temporary buffers in critical paths.
  KeyBytes seek_key_buffer_;
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

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

#include <boost/optional/optional.hpp>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/docdb/intent_aware_iterator_interface.h"
#include "yb/docdb/transaction_status_cache.h"

#include "yb/dockv/key_bytes.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"

#include "yb/util/status_fwd.h"
#include "yb/util/stack_trace.h"

namespace yb::docdb {

YB_DEFINE_ENUM(ResolvedIntentState, (kNoIntent)(kInvalidPrefix)(kValid));

// Provides a way to iterate over DocDB (sub)keys with respect to committed intents transparently
// for caller. Implementation relies on intents order in RocksDB, which is determined by intent key
// format. If (sub)key A goes before/after (sub)key B, all intents for A should go before/after all
// intents for (sub)key B.
//
// When seeking to some (sub)dockey, it will ignore values for this (sub)dockey
// (except write intents written by the same transaction) with HT higher than read_time.
//
// For intents from the same transaction, intents with maximum HT would be picked -- ignoring
// read_time for filtering purposes -- and returned as key with time equal to read_time.
// Intent data format:
//   SubDocKey (no HybridTime) + IntentType + HybridTime -> TxnId + value.
// TxnId, IntentType, HybridTime are all prefixed with their respective value types.
//
// KeyBytes/Slice passed to Seek* methods should not contain hybrid time.
// HybridTime of subdoc_key in Seek* methods would be ignored.
class IntentAwareIterator final : public IntentAwareIteratorIf {
 public:
  IntentAwareIterator(
      const DocDB& doc_db,
      const rocksdb::ReadOptions& read_opts,
      const ReadOperationData& read_operation_data,
      const TransactionOperationContext& txn_op_context,
      FastBackwardScan use_fast_backward_scan = FastBackwardScan::kFalse);

  IntentAwareIterator(const IntentAwareIterator& other) = delete;
  void operator=(const IntentAwareIterator& other) = delete;

  void Revalidate();

  // Seek to the smallest key which is greater or equal than doc_key.
  void Seek(const dockv::DocKey& doc_key);

  void Seek(Slice key, Full full = Full::kTrue) override;

  // Seek forward to specified encoded key (it is responsibility of caller to make sure it
  // doesn't have hybrid time).
  void SeekForward(Slice key) override;

  void Next();

  // Seek past specified subdoc key (it is responsibility of caller to make sure it doesn't have
  // hybrid time).
  void SeekPastSubKey(Slice key);

  // For efficiency, this overload takes a non-const KeyBytes pointer avoids memory allocation by
  // using the KeyBytes buffer to prepare the key to seek to by appending an extra byte. The
  // appended byte is removed when the method returns.
  void SeekOutOfSubDoc(dockv::KeyBytes* key_bytes) override;

  // Seek to last doc key.
  void SeekToLastDocKey();

  // Seek to previous SubDocKey as opposed to previous DocKey. Used for Redis only.
  void PrevSubDocKey(const dockv::KeyBytes& key_bytes);

  // Refer to the IntentAwareIteratorIf definition for the methods description.
  void PrevDocKey(const dockv::DocKey& doc_key);
  void PrevDocKey(Slice encoded_doc_key) override;
  void SeekPrevDocKey(Slice encoded_doc_key) override;

  // Positions the iterator to the previous suitable record relative to the current position.
  void Prev();

  // Seek backward to the specified key (it is responsibility of caller to make sure it does not
  // have hybrid time). Stops at the first met record scanned in the reverse order.
  void SeekBackward(dockv::KeyBytes& key_bytes);

  // Fetches currently pointed key and also updates max_seen_ht to ht of this key. The key does not
  // contain the DocHybridTime but is returned separately and optionally.
  Result<const FetchedEntry&> Fetch() override;

  const ReadHybridTime& read_time() const override { return read_time_; }
  Result<HybridTime> RestartReadHt() const override;

  HybridTime TEST_MaxSeenHt() const;

  // Iterate through Next() until a row containing a full record (non merge record) is found, or the
  // key changes.
  //
  // If a new full record with the same key is found, latest_record_ht is set to the write time of
  // that record, result_value is set to the value, and final_key is set to the key.
  //
  // If the key changes, latest_record_ht is set to the write time of the last merge record seen,
  // result_value is set to its value, and final_key is set to the key.
  Result<FetchedEntry> NextFullValue();

  // Finds the latest record for a particular key after the provided max_overwrite_time, returns the
  // write time of the found record, and optionally also the result value. This latest record may
  // not be a full record, but instead a merge record (e.g. a TTL row). Used for Redis only.
  Status FindLatestRecord(
      Slice key_without_ht,
      EncodedDocHybridTime* max_overwrite_time,
      Slice* result_value = nullptr);

  // Finds the oldest record for a particular key that is larger than the
  // specified min_hybrid_time, returns the overwrite time.
  // This record may not be a full record, but instead a merge record (e.g. a
  // TTL row).
  // Returns HybridTime::kInvalid if no such record was found.
  Result<HybridTime> FindOldestRecord(Slice key_without_ht, HybridTime min_hybrid_time);

  size_t NumberOfBytesAppendedDuringSeekForward() const {
    return 1 + encoded_read_time_.global_limit.size();
  }

  void DebugDump();

  std::string DebugPosToString() override;

 private:
  template <bool kLowerBound> friend class IntentAwareIteratorBoundScope;

  // Set the upper bound for the iterator.
  Slice SetUpperbound(Slice upperbound);
  Slice SetLowerbound(Slice lowerbound);

  void DoPrevDocKey(Slice encoded_doc_key);
  void DoSeekPrevDocKey(Slice encoded_doc_key);

  void DoSeekForward(Slice key);

  // Seek forward on regular sub-iterator.
  void SeekForwardRegular(Slice slice);

  // Seek to latest doc key among regular and intent iterator.
  void SeekToLatestDocKeyInternal();

  // Seek to latest subdoc key among regular and intent iterator. Used for Redis only.
  void SeekToLatestSubDocKeyInternal();

  // Choose latest subkey among regular and intent iterators.
  Slice LatestSubDocKey();

  // Skips regular entries with hybrid time after read limit.
  // If `is_forward` is `false` and `iter_` is positioned to the earliest record for the current
  // key, there are two cases:
  // 1 - record has hybrid time <= read limit: does nothing.
  // 2 - record has hybrid time > read limit: will skip all records for this key, since all of
  // them are after earliest record which is after read limit. Then will act the same way on
  // previous key.
  template <Direction direction>
  void SkipFutureRecords(const rocksdb::KeyValueEntry& entry);

  // Skips intents with hybrid time after read limit.
  void SkipFutureIntents();

  // Strong write intents which are either committed or written by the current
  // transaction (stored in txn_op_context) by considered time are considered as suitable.

  // Seek intent sub-iterator to latest suitable intent starting with seek_key_buffer_.
  // intent_iter_ will be positioned to first intent for the smallest key greater than
  // resolved_intent_sub_doc_key_encoded_.
  // If iterator already positioned far enough - does not perform seek.
  // If we already resolved intent after seek_key_prefix_, then it will be used.
  void SeekForwardToSuitableIntent();

  // Returns true if there is a resolved intent and it is correctly ordered towards the given key.
  template <Direction direction>
  bool HasSuitableIntent(Slice key);

  // Seek intent sub-iterator forward (backward) to latest suitable intent for first available
  // key. Updates resolved_intent_XXX fields.
  // intent_iter_ will be positioned to first intent for the smallest (biggest) key
  // greater (smaller) than resolved_intent_sub_doc_key_encoded_.
  template <Direction direction>
  void SeekToSuitableIntent(const rocksdb::KeyValueEntry& entry);

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

  // Seeks to the appropriate intent-prefix and returns the associated
  // DocHybridTime.
  Result<EncodedDocHybridTime> FindMatchingIntentRecordDocHybridTime(Slice key_without_ht);

  // Returns the DocHybridTime associated with the current regular record
  // pointed to, if it matches the key that is passed as the argument.
  // If the current record does not match the passed key, invalid hybrid time
  // is returned.
  Result<EncodedDocHybridTime> GetMatchingRegularRecordDocHybridTime(Slice key_without_ht);

  // Whether current entry is regular key-value pair.
  template <bool kDescending = false>
  inline bool IsEntryRegular() const {
    if (PREDICT_FALSE(!regular_entry_)) {
      return false;
    }
    if (!HasValidIntent()) {
      return true;
    }
    return IsRegularEntryOrderedBeforeResolvedIntent<kDescending>();
  }

  template <bool kDescending>
  bool IsRegularEntryOrderedBeforeResolvedIntent() const;

  // Set the exclusive upperbound of the intent iterator to the current SubDocKey of the regular
  // iterator. This is necessary to avoid RocksDB iterator from scanning over the deleted intents
  // beyond the current regular key unnecessarily.
  bool SetIntentUpperbound();

  // Resets the exclusive upperbound of the intent iterator to its default value.
  // - If we are using an upper bound for the regular RocksDB iterator, we will reuse the same upper
  //   bound for the intent iterator.
  // - If there is no upper bound for regular RocksDB (e.g. because we are scanning the entire
  //   table), we will fall back to scanning the entire intents RocksDB.
  void ResetIntentUpperbound();
  void SyncIntentUpperbound();

  // Does initial steps for prev doc key/sub doc key seek.
  // Returns true if prepare succeed.
  bool PreparePrev(Slice key);

  bool SatisfyBounds(Slice slice);

  const EncodedDocHybridTime& GetIntentDocHybridTime(bool* same_transaction = nullptr);

  inline bool HasValidIntent() const {
    return resolved_intent_state_ == ResolvedIntentState::kValid;
  }

  // Returns true if iterator currently points to some record.
  inline bool HasCurrentEntry() const {
    return regular_entry_ || HasValidIntent();
  }

  size_t IntentPrepareSeek(Slice key, Slice suffix);
  size_t IntentPrepareSeek(Slice key, char suffix);

  // Update seek_intent_iter_needed_, seek_key_prefix_ and seek_key_buffer_ to seek forward
  // for key + suffix.
  // If use_suffix_for_prefix then suffix is used in seek_key_prefix_, otherwise it will match key.
  void IntentSeekForward(size_t prefix_len);

  // Seeks backwards to specified encoded key taking (it is responsibility of caller to make sure it
  // doesn't have hybrid time). Does not perform a seek if the iterator is already positioned
  // before the given key.
  void IntentSeekBackward(Slice key);

  template <class T>
  bool HandleStatus(const Result<T>& result) {
    if (result.ok()) {
      return true;
    }
    status_ = result.status();
    return false;
  }

  // If status is not OK, sets status_ and returns false; otherwise returns true. It is required
  // to be called for any iterator's validity check, refer to #16565 for the details.
  bool HandleStatus(const Status& status);

  template <bool kDescending = false>
  void FillEntry();
  void FillRegularEntry();
  void FillIntentEntry();

  void ValidateResolvedIntentBounds();

  void SeekTriggered() {
#ifndef NDEBUG
    DebugSeekTriggered();
#endif
  }

  const ReadHybridTime read_time_;
  const EncodedReadHybridTime encoded_read_time_;

  const TransactionOperationContext txn_op_context_;
  BoundedRocksDbIterator intent_iter_;
  BoundedRocksDbIterator iter_;

  // regular_value_ contains value for the current entry from regular db.
  // Empty if there is no current value in regular db.
  rocksdb::KeyValueEntry regular_entry_;

  Status status_;
  EncodedDocHybridTime max_seen_ht_{DocHybridTime::kMin};

  // Upperbound for seek. If we see regular or intent record past this bound, it will be ignored.
  Slice upperbound_;
  Slice lowerbound_;

  // Buffer for holding the exclusive upper bound of the intent key.
  KeyBuffer intent_upperbound_buffer_;

  Slice intent_upperbound_;

  // Following fields contain information related to resolved suitable intent.
  ResolvedIntentState resolved_intent_state_ = ResolvedIntentState::kNoIntent;
  // SubDocKey (no HT).
  dockv::KeyBytes resolved_intent_key_prefix_;
  // DocHybridTime of resolved_intent_sub_doc_key_encoded_ is set to commit time or intent time in
  // case of intent is written by current transaction (stored in txn_op_context_).
  EncodedDocHybridTime resolved_intent_txn_dht_;
  EncodedDocHybridTime intent_dht_from_same_txn_{EncodedDocHybridTime::kMin};
  KeyBuffer resolved_intent_sub_doc_key_encoded_;
  dockv::KeyBytes resolved_intent_value_;

  const bool use_fast_backward_scan_ = false;

  TransactionStatusCache transaction_status_cache_;

  // Reusable buffer to prepare seek key to avoid reallocating temporary buffers in critical paths.
  KeyBuffer seek_buffer_;
  FetchedEntry entry_;
  BoundedRocksDbIterator* entry_source_ = nullptr;

#ifndef NDEBUG
  void DebugSeekTriggered();

  bool need_fetch_ = false;
#if YB_INTENT_AWARE_ITERATOR_COLLECT_SEEK_STACK_TRACE
  StackTrace last_seek_stack_trace_;
#endif
#endif
};


template <bool kLowerBound>
class NODISCARD_CLASS IntentAwareIteratorBoundScope {
 public:
  IntentAwareIteratorBoundScope(Slice bound, IntentAwareIterator* iterator)
      : iterator_(DCHECK_NOTNULL(iterator)), prev_bound_(SetBound(bound)) {
  }

  ~IntentAwareIteratorBoundScope() {
    SetBound(prev_bound_);
  }

  IntentAwareIteratorBoundScope(const IntentAwareIteratorBoundScope&) = delete;
  IntentAwareIteratorBoundScope(IntentAwareIteratorBoundScope&&) = delete;
  IntentAwareIteratorBoundScope& operator=(const IntentAwareIteratorBoundScope&) = delete;
  IntentAwareIteratorBoundScope& operator=(IntentAwareIteratorBoundScope&&) = delete;

 private:
  inline Slice SetBound(Slice bound) {
    if constexpr (kLowerBound) {
      return iterator_->SetLowerbound(bound);
    } else {
      return iterator_->SetUpperbound(bound);
    }
  }

  IntentAwareIterator* iterator_;
  Slice prev_bound_;
};

using IntentAwareIteratorLowerboundScope = IntentAwareIteratorBoundScope<true>;
using IntentAwareIteratorUpperboundScope = IntentAwareIteratorBoundScope<false>;

std::string DebugDumpKeyToStr(Slice key);

} // namespace yb::docdb

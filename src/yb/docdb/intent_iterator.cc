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

#include "yb/docdb/intent_iterator.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/iter_util.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/transaction_dump.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/util/debug-util.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

using namespace std::literals;

namespace yb::docdb {

using dockv::KeyBytes;
using dockv::KeyEntryTypeAsChar;
using dockv::SubDocKey;

namespace {

// Given that key is well-formed DocDB encoded key, checks if it is an intent key for the same key
// as intent_prefix. If key is not well-formed DocDB encoded key, result could be true or false.
bool IsIntentForTheSameKey(const Slice& key, const Slice& intent_prefix) {
  return key.starts_with(intent_prefix) && key.size() > intent_prefix.size() &&
         dockv::IntentValueType(key[intent_prefix.size()]);
}

std::string DebugDumpKeyToStr(const Slice& key) {
  return key.ToDebugString() + " (" + SubDocKey::DebugSliceToString(key) + ")";
}

std::string DebugDumpKeyToStr(const KeyBytes& key) {
  return DebugDumpKeyToStr(key.AsSlice());
}

const char kHighestKeyEntryChar = KeyEntryTypeAsChar::kMaxByte;

}  // namespace

IntentIterator::IntentIterator(
    rocksdb::DB* intents_db,
    const KeyBounds* docdb_key_bounds,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    const TransactionOperationContext& txn_op_context,
    const Slice* iterate_upper_bound)
    : read_time_(read_time),
      txn_op_context_(txn_op_context),
      transaction_status_cache_(txn_op_context_, read_time, deadline),
      upperbound_(iterate_upper_bound ? *iterate_upper_bound : Slice(&kHighestKeyEntryChar, 1)) {
  VTRACE(1, __func__);
  VLOG(4) << "IntentIterator, read_time: " << read_time << ", txn_op_context: " << txn_op_context_;

  if (txn_op_context) {
    if (txn_op_context.txn_status_manager->MinRunningHybridTime() != HybridTime::kMax) {
      intent_iter_ = docdb::CreateRocksDBIterator(
          intents_db,
          docdb_key_bounds,
          docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
          boost::none,
          rocksdb::kDefaultQueryId,
          nullptr /* file_filter */,
          &upperbound_);
    } else {
      VLOG(4) << "No transactions running";
    }
  }
  VTRACE(2, "Created intent iterator - initialized? - $0", intent_iter_.Initialized());
}

bool IntentIterator::valid() const {
  return !status_.ok() || !resolved_intent_key_prefix_.empty();
}

// Caller ensures that key doesn't have hybrid time encoded.
void IntentIterator::Seek(const Slice& key) {
  KeyBytes key_bytes;
  key_bytes.AppendRawBytes(key);
  Seek(&key_bytes);
}

// Caller ensures that key doesn't have hybrid time encoded
void IntentIterator::Seek(KeyBytes* key_bytes) {
  VLOG_WITH_FUNC(4) << "key_bytes: " << SubDocKey::DebugSliceToString(*key_bytes)
                    << ", CurrentPosition: "
                    << (intent_iter_.Valid() ? SubDocKey::DebugSliceToString(intent_iter_.key())
                                             : "invalid")
                    << ", ResolvedIntentKeyPrefix: "
                    << (resolved_intent_key_prefix_.empty()
                            ? "not set"
                            : SubDocKey::DebugSliceToString(resolved_intent_key_prefix_));

  AppendStrongWriteAndPerformSeek(key_bytes, false /*seek_forward*/);
  SeekToSuitableIntent<Direction::kForward>();
}

void IntentIterator::SeekForward(KeyBytes* key_bytes) {
  VLOG_WITH_FUNC(4) << "key_bytes: " << SubDocKey::DebugSliceToString(*key_bytes)
                    << ", CurrentPosition: "
                    << (intent_iter_.Valid() ? SubDocKey::DebugSliceToString(intent_iter_.key())
                                             : "invalid")
                    << ", ResolvedIntentKeyPrefix: "
                    << (resolved_intent_key_prefix_.empty()
                            ? "not set"
                            : SubDocKey::DebugSliceToString(resolved_intent_key_prefix_));

  // Ignore call if current key is greater than or equal to provided target.
  if (!resolved_intent_key_prefix_.empty() &&
      resolved_intent_key_prefix_.CompareTo(*key_bytes) >= 0) {
    return;
  }

  // Avoid seeking if intent iterator is already pointing to the target.
  if (intent_iter_.Valid() && intent_iter_.key().compare(*key_bytes) < 0) {
    AppendStrongWriteAndPerformSeek(key_bytes, true /*seek_forward*/);
  } else {
    HandleStatus(intent_iter_.status());
  }

  SeekToSuitableIntent<Direction::kForward>();
}

void IntentIterator::Next() {
  VLOG_WITH_FUNC(4) << "iter_valid_ " << !resolved_intent_key_prefix_.empty();
  if (resolved_intent_key_prefix_.empty()) {
    return;
  }

  // Since iterator had a valid entry before, it means that underlying iterator
  // is already pointing to next entry. So we simply try to find the next valid
  // intent here.
  SeekToSuitableIntent<Direction::kForward>();
}

template <Direction direction>
void IntentIterator::SeekToSuitableIntent() {
  DOCDB_DEBUG_SCOPE_LOG(/* msg */ "", std::bind(&IntentIterator::DebugDump, this));

  // TODO(kpopali): Add support for reverse iteration.
  CHECK(direction == Direction::kForward) << "kBackward direction is not supported";

  VLOG_WITH_FUNC(4) << "intent_iter.Valid() " << intent_iter_.Valid();

  // Clear the previous cached state
  resolved_intent_key_prefix_.Clear();
  resolved_intent_txn_dht_ = DocHybridTime::kMin;
  intent_dht_from_same_txn_ = DocHybridTime::kMin;

  // Find latest suitable intent for the first SubDocKey having suitable intents.
  while (intent_iter_.Valid()) {
    auto intent_key = intent_iter_.key();
    if (intent_key[0] == KeyEntryTypeAsChar::kTransactionId) {
      SkipTxnRegion<direction>();
      continue;
    }
    VLOG(4) << "Intent found: " << DebugIntentKeyToString(intent_key)
            << ", iter_valid_: " << !resolved_intent_key_prefix_.empty();
    // Does key match the specified bounds?
    if (!SatisfyBounds(intent_key)) {
      VLOG(4) << "Intent key outside of the bound, stopping: "
              << DebugIntentKeyToString(intent_key);
      break;
    }

    // Make sure to only look at intents for a single SubDocKey
    if (!resolved_intent_key_prefix_.empty() &&
        // Only scan intents for the first SubDocKey having suitable intents.
        !IsIntentForTheSameKey(intent_key, resolved_intent_key_prefix_)) {
      break;
    }
    ProcessIntent();
    if (!status_.ok()) {
      return;
    }
    switch (direction) {
      case Direction::kForward:
        intent_iter_.Next();
        break;
      case Direction::kBackward:
        intent_iter_.Prev();
        break;
    }
  }
  HandleStatus(intent_iter_.status());

  if (!resolved_intent_key_prefix_.empty()) {
    UpdateResolvedIntentSubDocKeyEncoded();

    // Update the max seen HT
    max_seen_ht_.MakeAtLeast(resolved_intent_txn_dht_.hybrid_time());
  }
}

template <Direction direction>
void IntentIterator::SkipTxnRegion() {
  // If the intent iterator ever enters the transaction metadata and reverse index region, skip
  // past it.
  switch (direction) {
    case Direction::kForward: {
      static const std::array<char, 1> kAfterTransactionId{KeyEntryTypeAsChar::kTransactionId + 1};
      static const Slice kAfterTxnRegion(kAfterTransactionId);
      intent_iter_.Seek(kAfterTxnRegion);
      break;
    }
    case Direction::kBackward:
      // TODO(scan_perf): Add support for reverse iteration.
      CHECK(false);
      break;
  }
}

bool IntentIterator::SatisfyBounds(const Slice& slice) {
  return upperbound_.empty() || slice.compare(upperbound_) <= 0;
}

void IntentIterator::ProcessIntent() {
  auto decode_result =
      DecodeStrongWriteIntent(txn_op_context_, &intent_iter_, &transaction_status_cache_);
  if (!decode_result.ok()) {
    status_ = decode_result.status();
    return;
  }
  VLOG(4) << "Intent decode: " << DebugIntentKeyToString(intent_iter_.key()) << " => "
          << intent_iter_.value().ToDebugHexString() << ", result: " << *decode_result;
  DOCDB_DEBUG_LOG(
      "resolved_intent_txn_dht_: $0 value_time: $1 read_time: $2",
      resolved_intent_txn_dht_.ToString(),
      decode_result->value_time.ToString(),
      read_time_.ToString());
  auto& resolved_intent_time =
      decode_result->same_transaction ? intent_dht_from_same_txn_ : resolved_intent_txn_dht_;
  // If we already resolved intent that is newer that this one, we should ignore current
  // intent because we are interested in the most recent intent only.
  if (decode_result->value_time <= EncodedDocHybridTime(resolved_intent_time)) {
    return;
  }

  // Ignore intent past read limit.
  if (decode_result->value_time >
          decode_result->MaxAllowedValueTime(EncodedReadHybridTime(read_time_))) {
    return;
  }

  // Set the key if iterator was invalid before
  if (resolved_intent_key_prefix_.empty()) {
    resolved_intent_key_prefix_.Reset(decode_result->intent_prefix);
  }
  auto decoded_value_time = decode_result->value_time.Decode();
  if (!decoded_value_time.ok()) {
    status_ = decoded_value_time.status();
    return;
  }
  if (decode_result->same_transaction) {
    intent_dht_from_same_txn_ = *decoded_value_time;
    // We set resolved_intent_txn_dht_ to maximum possible time (time higher than read_time_.read
    // will cause read restart or will be ignored if higher than read_time_.global_limit) in
    // order to ignore intents/values from other transactions. But we save origin intent time into
    // intent_dht_from_same_txn_, so we can compare time of intents for the same key from the same
    // transaction and select the latest one.
    resolved_intent_txn_dht_ = DocHybridTime(read_time_.read, kMaxWriteId);
  } else {
    resolved_intent_txn_dht_ = *decoded_value_time;
  }
  resolved_intent_value_.Reset(decode_result->intent_value);
}

Result<IntentFetchKeyResult> IntentIterator::FetchKey() const {
  RETURN_NOT_OK(status_);

  SCHECK(
      !resolved_intent_key_prefix_.empty(), InternalError,
      "FetchKey() should only be called when iterator is valid");
  IntentFetchKeyResult result;
  result.key = resolved_intent_key_prefix_.AsSlice();
  result.encoded_key = resolved_intent_sub_doc_key_encoded_.AsSlice();
  result.same_transaction = intent_dht_from_same_txn_ != DocHybridTime::kMin;
  result.write_time =
      result.same_transaction ? intent_dht_from_same_txn_ : resolved_intent_txn_dht_;
  max_seen_ht_.MakeAtLeast(resolved_intent_txn_dht_.hybrid_time());

  VLOG(4) << "Fetched key " << SubDocKey::DebugSliceToString(result.key)
          << ", with time: " << result.write_time << ", while read bounds are: " << read_time_;

  YB_TRANSACTION_DUMP(
      Read, txn_op_context_ ? txn_op_context_.txn_status_manager->tablet_id() : TabletId(),
      txn_op_context_ ? txn_op_context_.transaction_id : TransactionId::Nil(), read_time_,
      result.write_time, result.same_transaction, result.key.size(), result.key, value().size(),
      value());

  return result;
}

Slice IntentIterator::value() const {
  DCHECK(!resolved_intent_key_prefix_.empty());
  VLOG(4) << "IntentAwareIterator::value() returning resolved_intent_value_: "
          << resolved_intent_value_.AsSlice().ToDebugHexString();
  return resolved_intent_value_;
}

void IntentIterator::SeekOutOfSubKey(KeyBytes* key_bytes) {
  VLOG(4) << "SeekOutOfSubKey(" << SubDocKey::DebugSliceToString(*key_bytes) << ")";
  if (!status_.ok()) {
    return;
  }

  VLOG_WITH_FUNC(4) << "key_bytes: " << SubDocKey::DebugSliceToString(*key_bytes)
                    << ", CurrentPosition: "
                    << (intent_iter_.Valid() ? SubDocKey::DebugSliceToString(intent_iter_.key())
                                             : "invalid")
                    << ", ResolvedIntentKeyPrefix: "
                    << (resolved_intent_key_prefix_.empty()
                            ? "not set"
                            : SubDocKey::DebugSliceToString(resolved_intent_key_prefix_));

  // Skip the SeekOutOfSubKey call if we have an resolved intent outside of the
  // provided key.
  if (!resolved_intent_key_prefix_.empty() &&
      !resolved_intent_key_prefix_.AsSlice().starts_with(*key_bytes)) {
    return;
  }

  if (intent_iter_.Valid()) {
    docdb::SeekOutOfSubKey(key_bytes, &intent_iter_);
  } else {
    HandleStatus(intent_iter_.status());
  }
  SeekToSuitableIntent<Direction::kForward>();
}

void IntentIterator::UpdateResolvedIntentSubDocKeyEncoded() {
  resolved_intent_sub_doc_key_encoded_.Reset(resolved_intent_key_prefix_.AsSlice());
  resolved_intent_sub_doc_key_encoded_.AppendKeyEntryType(dockv::KeyEntryType::kHybridTime);
  resolved_intent_sub_doc_key_encoded_.AppendHybridTime(resolved_intent_txn_dht_);
  VLOG(4) << "Resolved intent SubDocKey: "
          << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_);
}

void IntentIterator::AppendStrongWriteAndPerformSeek(KeyBytes* key_bytes, bool seek_forward) {
  const size_t key_size = key_bytes->size();
  AppendStrongWrite(key_bytes);

  if (seek_forward) {
    docdb::SeekPossiblyUsingNext(&intent_iter_, *key_bytes);
  } else {
    ROCKSDB_SEEK(&intent_iter_, *key_bytes);
  }
  key_bytes->Truncate(key_size);
}

void IntentIterator::DebugDump() {
  LOG(INFO) << ">> IntentIterator dump";
  LOG(INFO) << "Valid(): " << valid();
  if (intent_iter_.Initialized()) {
    LOG(INFO) << "intent_iter_.Valid(): " << intent_iter_.Valid();
    if (intent_iter_.Valid()) {
      LOG(INFO) << "intent_iter_.key(): " << intent_iter_.key().ToDebugHexString();
    }
  }
  if (!resolved_intent_key_prefix_.empty()) {
    LOG(INFO) << "resolved_intent_sub_doc_key_encoded_: "
              << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_);
  }
  LOG(INFO) << "<< IntentIterator dump";
}

std::string IntentIterator::DebugPosToString() {
  if (!valid()) {
    return "<INVALID>";
  }
  auto key = FetchKey();
  if (!key.ok()) {
    return key.status().ToString();
  }
  return SubDocKey::DebugSliceToString(key->key);
}

void IntentIterator::HandleStatus(const Status& status) {
  if (!status.ok()) {
    status_ = status;
  }
}

std::string DecodeStrongWriteIntentResult::ToString() const {
  return Format(
      "{ intent_prefix: $0 intent_value: $1 intent_time: $2 value_time: $3 "
      "same_transaction: $4 intent_types: $5 }",
      intent_prefix.ToDebugHexString(), intent_value.ToDebugHexString(), intent_time, value_time,
      same_transaction, intent_types);
}

const EncodedDocHybridTime& DecodeStrongWriteIntentResult::MaxAllowedValueTime(
    const EncodedReadHybridTime& read_time) const {
  if (same_transaction) {
    return read_time.in_txn_limit;
  }
  return intent_time > read_time.local_limit ? read_time.read : read_time.global_limit;
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
    TransactionStatusCache* transaction_status_cache) {
  DecodeStrongWriteIntentResult result;
  auto decoded_intent_key = VERIFY_RESULT(dockv::DecodeIntentKey(intent_iter->key()));
  result.intent_prefix = decoded_intent_key.intent_prefix;
  result.intent_types = decoded_intent_key.intent_types;
  if (result.intent_types.Test(dockv::IntentType::kStrongWrite)) {
    auto intent_value = intent_iter->value();
    auto decoded_intent_value = VERIFY_RESULT(dockv::DecodeIntentValue(intent_value));

    const auto& decoded_txn_id = decoded_intent_value.transaction_id;
    auto decoded_subtxn_id = decoded_intent_value.subtransaction_id;

    result.intent_value = decoded_intent_value.body;
    result.intent_time = decoded_intent_key.doc_ht;
    result.same_transaction = decoded_txn_id == txn_op_context.transaction_id;

    // By setting the value time to kMin, we ensure the caller ignores this intent. This is true
    // because the caller is skipping all intents written before or at the same time as
    // intent_dht_from_same_txn_ or resolved_intent_txn_dht_, which of course are greater than or
    // equal to DocHybridTime::kMin.
    if (result.intent_value.starts_with(dockv::ValueEntryTypeAsChar::kRowLock)) {
      result.value_time.Assign(EncodedDocHybridTime::kMin);
    } else if (result.same_transaction) {
      const auto aborted = txn_op_context.subtransaction.aborted.Test(decoded_subtxn_id);
      if (!aborted) {
        result.value_time = decoded_intent_key.doc_ht;
      } else {
        // If this intent is from the same transaction, we can check the aborted set from this
        // txn_op_context to see whether the intent is still live. If not, mask it from the caller.
        result.value_time.Assign(EncodedDocHybridTime::kMin);
      }
      VLOG(4) << "Same transaction: " << decoded_txn_id << ", aborted: " << aborted
              << ", original doc_ht: " << decoded_intent_key.doc_ht.ToString();
    } else {
      auto commit_data =
          VERIFY_RESULT(transaction_status_cache->GetTransactionLocalState(decoded_txn_id));
      auto commit_ht = commit_data.commit_ht;
      const auto& aborted_subtxn_set = commit_data.aborted_subtxn_set;
      auto is_aborted_subtxn = aborted_subtxn_set.Test(decoded_subtxn_id);
      result.value_time.Assign(
          commit_ht == HybridTime::kMin || is_aborted_subtxn
              ? DocHybridTime::kMin
              : DocHybridTime(commit_ht, decoded_intent_value.write_id));
      VLOG(4) << "Transaction id: " << decoded_txn_id
              << ", subtransaction id: " << decoded_subtxn_id
              << ", same transaction: " << result.same_transaction
              << ", value time: " << result.value_time
              << ", commit ht: " << commit_ht
              << ", value: " << result.intent_value.ToDebugHexString()
              << ", aborted subtxn set: " << aborted_subtxn_set.ToString();
    }
  } else {
    result.value_time.Assign(EncodedDocHybridTime::kMin);
  }
  return result;
}

namespace {

const char kStrongWriteTail[] = {
    KeyEntryTypeAsChar::kIntentTypeSet,
    static_cast<char>(dockv::IntentTypeSet({dockv::IntentType::kStrongWrite}).ToUIntPtr()) };

const Slice kStrongWriteTailSlice = Slice(kStrongWriteTail, sizeof(kStrongWriteTail));

char kEmptyKeyStrongWriteTail[] = {
    KeyEntryTypeAsChar::kGroupEnd,
    KeyEntryTypeAsChar::kIntentTypeSet,
    static_cast<char>(dockv::IntentTypeSet({dockv::IntentType::kStrongWrite}).ToUIntPtr()) };

const Slice kEmptyKeyStrongWriteTailSlice =
    Slice(kEmptyKeyStrongWriteTail, sizeof(kEmptyKeyStrongWriteTail));

} // namespace

Slice StrongWriteSuffix(Slice key) {
  return key.empty() ? kEmptyKeyStrongWriteTailSlice : kStrongWriteTailSlice;
}

void AppendStrongWrite(KeyBytes* out) { out->AppendRawBytes(StrongWriteSuffix(*out)); }

}  // namespace yb::docdb

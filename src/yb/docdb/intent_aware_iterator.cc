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

#include "yb/docdb/intent_aware_iterator.h"

#include <thread>
#include <boost/thread/latch.hpp>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/value.h"

using namespace std::literals; // NOLINT

namespace yb {
namespace docdb {

namespace {

KeyBytes GetIntentPrefixForKeyWithoutHt(const Slice& key) {
  KeyBytes intent_key_bytes;
  // Since caller guarantees that key_bytes doesn't have hybrid time, we can simply prepend
  // kIntentPrefix in order to get prefix for all related intents.
  intent_key_bytes.AppendValueType(ValueType::kIntentPrefix);
  intent_key_bytes.AppendRawBytes(key);
  return intent_key_bytes;
}

KeyBytes GetIntentPrefixForKey(const SubDocKey& subdoc_key) {
  return GetIntentPrefixForKeyWithoutHt(subdoc_key.Encode(false /* include_hybrid_time */));
}

// For locally committed transactions returns commit time if committed at specified time or
// HybridTime::kMin otherwise. For other transactions returns HybridTime::kInvalidHybridTime.
HybridTime GetTxnLocalCommitTime(
    TransactionStatusManager* txn_status_manager, const TransactionId& transaction_id,
    const HybridTime& time) {
  const HybridTime local_commit_time = txn_status_manager->LocalCommitTime(transaction_id);
  return local_commit_time.is_valid()
      ? (local_commit_time <= time ? local_commit_time : HybridTime::kMin)
      : local_commit_time;
}

// Returns transaction commit time if already committed at specified time or HybridTime::kMin
// otherwise.
Result<HybridTime> GetTxnCommitTime(
    TransactionStatusManager* txn_status_manager,
    const TransactionId& transaction_id,
    const HybridTime& time) {
  DCHECK_ONLY_NOTNULL(txn_status_manager);

  HybridTime local_commit_time = GetTxnLocalCommitTime(
      txn_status_manager, transaction_id, time);
  if (local_commit_time.is_valid()) {
    return local_commit_time;
  }

  Result<TransactionStatusResult> txn_status_result = STATUS(Uninitialized, "");
  boost::latch latch(1);
  for(;;) {
    auto callback = [&txn_status_result, &latch](Result<TransactionStatusResult> result) {
      txn_status_result = std::move(result);
      latch.count_down();
    };
    txn_status_manager->RequestStatusAt(transaction_id, time, callback);
    latch.wait();
    if (txn_status_result.ok()) {
      break;
    } else {
      LOG(WARNING)
          << "Failed to request transaction " << yb::ToString(transaction_id) << " status: "
          <<  txn_status_result.status();
      if (txn_status_result.status().IsTryAgain()) {
        // TODO(dtxn) In case of TryAgain error status we need to re-request transaction status.
        // Temporary workaround is to sleep for 0.5s and re-request.
        std::this_thread::sleep_for(500ms);
        latch.reset(1);
        continue;
      } else {
        RETURN_NOT_OK(txn_status_result);
      }
    }
  }
  VLOG(4) << "Transaction_id " << transaction_id << " at " << time
          << ": status: " << ToString(txn_status_result->status)
          << ", status_time: " << txn_status_result->status_time;
  if (txn_status_result->status == TransactionStatus::ABORTED) {
    local_commit_time = GetTxnLocalCommitTime(txn_status_manager, transaction_id, time);
    return local_commit_time.is_valid() ? local_commit_time : HybridTime::kMin;
  } else {
    return txn_status_result->status == TransactionStatus::COMMITTED
        ? txn_status_result->status_time
        : HybridTime::kMin;
  }
}

CHECKED_STATUS CreateTryAgain() {
  return STATUS(TryAgain, "Value was written in nearest future, restart read");
}

struct DecodeStrongWriteIntentResult {
  Slice intent_prefix;
  Slice intent_value;
  DocHybridTime value_time;

  // Whether this intent from the same transaction as specified in context.
  bool same_transaction = false;

  std::string ToString() const {
    return Format("{ intent_prefix: $0 intent_value: $1 value_time: $2 same_transaction: $3 }",
                  intent_prefix.ToDebugString(), intent_value.ToDebugString(), value_time,
                  same_transaction);
  }
};

std::ostream& operator<<(std::ostream& out, const DecodeStrongWriteIntentResult& result) {
  return out << result.ToString();
}

// Decodes intent based on intent_iterator and its transaction commit time if intent is a strong
// write intent and transaction is already committed at specified time or it is current transaction.
// Returns HybridTime::kMin as value_time otherwise.
// For current transaction returns intent record hybrid time as value_time.
// Consumes intent from value_slice leaving only value itself.
Result<DecodeStrongWriteIntentResult> DecodeStrongWriteIntent(
    TransactionOperationContext txn_op_context, const ReadHybridTime& read_time,
    rocksdb::Iterator* intent_iter) {
  IntentType intent_type;
  DocHybridTime intent_ht;
  DecodeStrongWriteIntentResult result;
  RETURN_NOT_OK(DecodeIntentKey(
     intent_iter->key(), &result.intent_prefix, &intent_type, &intent_ht));
  if (IsStrongWriteIntent(intent_type)) {
    result.intent_value = intent_iter->value();
    Result<TransactionId> txn_id = DecodeTransactionIdFromIntentValue(&result.intent_value);
    RETURN_NOT_OK(txn_id);
    result.same_transaction = *txn_id == txn_op_context.transaction_id;
    if (result.same_transaction) {
      result.value_time = intent_ht;
    } else {
      Result<HybridTime> commit_ht = GetTxnCommitTime(
          &txn_op_context.txn_status_manager, *txn_id, read_time.limit);
      RETURN_NOT_OK(commit_ht);
      VLOG(4) << "Transaction id: " << *txn_id << " at " << read_time
              << " commit time: " << *commit_ht;
      if (*commit_ht > read_time.read) {
        return CreateTryAgain();
      }
      result.value_time = DocHybridTime(*commit_ht);
    }
  } else {
    result.value_time = DocHybridTime::kMin;
  }
  return result;
}

// Given that key is well-formed DocDB encoded key, checks if it is an intent key for the same key
// as intent_prefix. If key is not well-formed DocDB encoded key, result could be true or false.
bool IsIntentForTheSameKey(const Slice& key, const Slice& intent_prefix) {
  return key.starts_with(intent_prefix)
      && key.size() > intent_prefix.size()
      && key[intent_prefix.size()] == static_cast<char>(ValueType::kIntentType);
}

std::string DebugDumpKeyToStr(const Slice &key) {
  SubDocKey key_decoded;
  DCHECK(key_decoded.FullyDecodeFrom(key).ok());
  return key.ToDebugString() + " (" + key_decoded.ToString() + ")";
}

std::string DebugDumpKeyToStr(const KeyBytes &key) {
  return DebugDumpKeyToStr(key.AsSlice());
}

bool DebugHasHybridTime(const Slice& subdoc_key_encoded) {
  SubDocKey subdoc_key;
  CHECK(subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(subdoc_key_encoded).ok());
  return subdoc_key.has_hybrid_time();
}

} // namespace

IntentAwareIterator::IntentAwareIterator(
    rocksdb::DB* rocksdb,
    const rocksdb::ReadOptions& read_opts,
    const ReadHybridTime& read_time,
    const TransactionOperationContextOpt& txn_op_context)
    : read_time_(read_time),
      txn_op_context_(txn_op_context) {
  if (txn_op_context.is_initialized()) {
    intent_iter_ = docdb::CreateRocksDBIterator(rocksdb,
                                                docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                                boost::none,
                                                rocksdb::kDefaultQueryId);
  }
  iter_.reset(rocksdb->NewIterator(read_opts));
}

Status IntentAwareIterator::Seek(const DocKey &doc_key) {
  return SeekWithoutHt(doc_key.Encode());
}

Status IntentAwareIterator::SeekWithoutHt(const Slice& key) {
  VLOG(4) << "SeekWithoutHt(" << key.ToDebugString() << ")";
  DOCDB_DEBUG_SCOPE_LOG(
      key.ToDebugString(),
      std::bind(&IntentAwareIterator::DebugDump, this));
  ROCKSDB_SEEK(iter_.get(), key);
  SkipFutureRecords();
  if (intent_iter_) {
    ROCKSDB_SEEK(intent_iter_.get(), GetIntentPrefixForKeyWithoutHt(key));
    RETURN_NOT_OK(SeekForwardToSuitableIntent());
  }
  return Status::OK();
}

Status IntentAwareIterator::SeekForwardWithoutHt(const Slice& key) {
  VLOG(4) << "SeekForwardWithoutHt(" << key.ToDebugString() << ")";
  DOCDB_DEBUG_SCOPE_LOG(
      key.ToDebugString(),
      std::bind(&IntentAwareIterator::DebugDump, this));
  RETURN_NOT_OK(SeekForwardRegular(key));
  if (intent_iter_) {
    RETURN_NOT_OK(SeekForwardToSuitableIntent(GetIntentPrefixForKeyWithoutHt(key)));
  }
  return Status::OK();
}

Status IntentAwareIterator::SeekForwardIgnoreHt(const SubDocKey& subdoc_key) {
  VLOG(4) << "SeekForwardIgnoreHt(" << subdoc_key.ToString() << ")";
  auto subdoc_key_encoded = subdoc_key.Encode(false /* include_hybrid_time */);
  AppendDocHybridTime(DocHybridTime(read_time_.limit, kMaxWriteId), &subdoc_key_encoded);
  return SeekForwardWithoutHt(subdoc_key_encoded);
}

Status IntentAwareIterator::SeekPastSubKey(const SubDocKey& subdoc_key) {
  VLOG(4) << "SeekPastSubKey(" << subdoc_key.ToString() << ")";
  docdb::SeekPastSubKey(subdoc_key, iter_.get());
  SkipFutureRecords();
  if (intent_iter_) {
    KeyBytes intent_prefix = GetIntentPrefixForKey(subdoc_key);
    // Skip all intents for subdoc_key.
    intent_prefix.mutable_data()->push_back(static_cast<char>(ValueType::kIntentType) + 1);
    RETURN_NOT_OK(SeekForwardToSuitableIntent(intent_prefix));
  }
  return Status::OK();
}

Status IntentAwareIterator::SeekOutOfSubDoc(const SubDocKey& subdoc_key) {
  VLOG(4) << "SeekOutOfSubDoc(" << subdoc_key.ToString() << ")";
  RETURN_NOT_OK(SeekForwardRegular(subdoc_key.AdvanceOutOfSubDoc()));
  if (intent_iter_) {
    KeyBytes intent_prefix = GetIntentPrefixForKey(subdoc_key);
    // See comment for SubDocKey::AdvanceOutOfSubDoc.
    intent_prefix.AppendValueType(ValueType::kMaxByte);
    RETURN_NOT_OK(SeekForwardToSuitableIntent(intent_prefix));
  }
  return Status::OK();
}

Status IntentAwareIterator::SeekToLastDocKey() {
  if (intent_iter_) {
    // TODO (dtxn): Implement SeekToLast when inten intents are present. Since part of the
    // is made of intents, we may have to avoid that. This is needed when distributed txns are fully
    // supported.
    return Status::OK();
  }
  iter_->SeekToLast();
  if (!iter_->Valid()) {
    return Status::OK();
  }
  // Seek to the first rocksdb kv-pair for this row.
  rocksdb::Slice rocksdb_key(iter_->key());
  DocKey doc_key;
  RETURN_NOT_OK(doc_key.DecodeFrom(&rocksdb_key));
  KeyBytes encoded_doc_key = doc_key.Encode();
  RETURN_NOT_OK(SeekWithoutHt(encoded_doc_key));
  return Status::OK();
}

Status IntentAwareIterator::PrevDocKey(const DocKey& doc_key) {
  RETURN_NOT_OK(Seek(doc_key));
  if (!iter_->Valid()) {
    return SeekToLastDocKey();
  }
  iter_->Prev();
  if (!iter_->Valid()) {
    iter_valid_ = false; // TODO(dtxn) support reverse scan with read restart
    return Status::OK();
  }
  Slice key_slice = iter_->key();
  DocKey prev_key;
  RETURN_NOT_OK(prev_key.DecodeFrom(&key_slice));
  RETURN_NOT_OK(Seek(prev_key));
  return Status::OK();
}

bool IntentAwareIterator::valid() {
  return iter_valid_ || resolved_intent_state_ == ResolvedIntentState::kValid;
}

bool IntentAwareIterator::IsEntryRegular() {
  if (PREDICT_FALSE(!iter_valid_)) {
    return false;
  }
  if (resolved_intent_state_ == ResolvedIntentState::kValid) {
    return iter_->key().compare(resolved_intent_sub_doc_key_encoded_) < 0;
  }
  return true;
}

Slice IntentAwareIterator::key() {
  if (IsEntryRegular()) {
    return iter_->key();
  } else {
    DCHECK_EQ(ResolvedIntentState::kValid, resolved_intent_state_);
    return resolved_intent_sub_doc_key_encoded_;
  }
}

Slice IntentAwareIterator::value() {
  if (IsEntryRegular()) {
    return iter_->value();
  } else {
    DCHECK_EQ(ResolvedIntentState::kValid, resolved_intent_state_);
    return resolved_intent_value_;
  }
}

Status IntentAwareIterator::SeekForwardRegular(const Slice& slice, const Slice& prefix) {
  docdb::SeekForward(slice, iter_.get());
  SkipFutureRecords();
  return Status::OK();
}

Status IntentAwareIterator::ProcessIntent() {
  auto decode_result = DecodeStrongWriteIntent(
      txn_op_context_.get(), read_time_, intent_iter_.get());
  RETURN_NOT_OK(decode_result);
  VLOG(4) << "Intent decode: " << intent_iter_->key().ToDebugString()
          << " => " << intent_iter_->value().ToDebugString() << ", result: " << *decode_result;
  DOCDB_DEBUG_LOG(
      "resolved_intent_txn_dht_: $0 value_time: $1 high_ht: $2",
      resolved_intent_txn_dht_.ToString(),
      decode_result->value_time.ToString(),
      read_time_.read.ToString());
  auto real_time = decode_result->same_transaction ? intent_dht_from_same_txn_
                                                   : resolved_intent_txn_dht_;
  if (decode_result->value_time > real_time &&
      (decode_result->same_transaction ||
           decode_result->value_time.hybrid_time() <= read_time_.limit)) {
    if (resolved_intent_state_ == ResolvedIntentState::kNoIntent) {
      resolved_intent_key_prefix_.Reset(decode_result->intent_prefix);
      auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();
      resolved_intent_state_ =
          decode_result->intent_prefix.starts_with(prefix) ? ResolvedIntentState::kValid
                                                           : ResolvedIntentState::kInvalidPrefix;
    }
    if (decode_result->same_transaction) {
      intent_dht_from_same_txn_ = decode_result->value_time;
      resolved_intent_txn_dht_ = DocHybridTime(read_time_.read, kMaxWriteId);
    } else {
      resolved_intent_txn_dht_ = decode_result->value_time;
    }
    resolved_intent_value_.Reset(decode_result->intent_value);
  }
  return Status::OK();
}

void IntentAwareIterator::UpdateResolvedIntentSubDocKeyEncoded() {
  resolved_intent_sub_doc_key_encoded_.ResetRawBytes(
      resolved_intent_key_prefix_.data().data() + 1, resolved_intent_key_prefix_.size() - 1);
  resolved_intent_sub_doc_key_encoded_.AppendValueType(ValueType::kHybridTime);
  resolved_intent_sub_doc_key_encoded_.AppendHybridTime(resolved_intent_txn_dht_);
  VLOG(4) << "Resolved intent SubDocKey: "
          << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_);
}

Status IntentAwareIterator::SeekForwardToSuitableIntent(const KeyBytes &intent_key_prefix) {
  DOCDB_DEBUG_SCOPE_LOG(intent_key_prefix.ToString(),
                        std::bind(&IntentAwareIterator::DebugDump, this));
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent &&
      resolved_intent_key_prefix_.CompareTo(intent_key_prefix) >= 0) {
    return Status::OK();
  }
  docdb::SeekForward(intent_key_prefix, intent_iter_.get());
  return SeekForwardToSuitableIntent();
}

Status IntentAwareIterator::SeekForwardToSuitableIntent() {
  DOCDB_DEBUG_SCOPE_LOG("", std::bind(&IntentAwareIterator::DebugDump, this));
  resolved_intent_state_ = ResolvedIntentState::kNoIntent;
  resolved_intent_txn_dht_ = DocHybridTime::kMin;
  auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();

  // Find latest suitable intent for the first SubDocKey having suitable intents.
  while (intent_iter_->Valid()) {
    auto intent_key = intent_iter_->key();
    if (GetKeyType(intent_key) != KeyType::kIntentKey) {
      break;
    }
    VLOG(4) << "Intent found: " << intent_key.ToDebugString();
    if (resolved_intent_state_ != ResolvedIntentState::kNoIntent &&
        // Only scan intents for the first SubDocKey having suitable intents.
        !IsIntentForTheSameKey(intent_key, resolved_intent_key_prefix_)) {
      break;
    }
    intent_key.consume_byte();
    if (!intent_key.starts_with(prefix)) {
      break;
    }
    RETURN_NOT_OK(ProcessIntent());
    intent_iter_->Next();
  }
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    UpdateResolvedIntentSubDocKeyEncoded();
  }
  return Status::OK();
}

void IntentAwareIterator::DebugDump() {
  LOG(INFO) << ">> IntentAwareIterator dump";
  LOG(INFO) << "iter_->Valid(): " << iter_->Valid();
  if (iter_->Valid()) {
    LOG(INFO) << "iter_->key(): " << DebugDumpKeyToStr(iter_->key());
  }
  if (intent_iter_) {
    LOG(INFO) << "intent_iter_->Valid(): " << intent_iter_->Valid();
    if (intent_iter_->Valid()) {
      LOG(INFO) << "intent_iter_->key(): " << intent_iter_->key().ToDebugString();
    }
  }
  LOG(INFO) << "resolved_intent_state_: " << yb::ToString(resolved_intent_state_);
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    LOG(INFO) << "resolved_intent_sub_doc_key_encoded_: "
              << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_);
  }
  LOG(INFO) << "valid(): " << valid();
  if (valid()) {
    LOG(INFO) << "key(): " << DebugDumpKeyToStr(key());
  }
  LOG(INFO) << "<< IntentAwareIterator dump";
}

Status IntentAwareIterator::FindLastWriteTime(
    const KeyBytes& key_bytes_without_ht,
    DocHybridTime* max_deleted_ts,
    Value* result_value) {
  DCHECK_ONLY_NOTNULL(max_deleted_ts);
  DOCDB_DEBUG_SCOPE_LOG(
      key_bytes_without_ht.ToString() + ", " + yb::ToString(max_deleted_ts) + ", "
          + yb::ToString(result_value),
      std::bind(&IntentAwareIterator::DebugDump, this));
  DCHECK(!DebugHasHybridTime(key_bytes_without_ht));

  bool found_later_intent_result = false;
  if (intent_iter_) {
    const auto intent_prefix = GetIntentPrefixForKeyWithoutHt(key_bytes_without_ht);
    RETURN_NOT_OK(SeekForwardToSuitableIntent(intent_prefix));
    if (resolved_intent_state_ == ResolvedIntentState::kValid &&
        resolved_intent_txn_dht_ > *max_deleted_ts &&
        resolved_intent_key_prefix_.CompareTo(intent_prefix) == 0) {
      *max_deleted_ts = resolved_intent_txn_dht_;
      found_later_intent_result = true;
    }
  }

  {
    KeyBytes key_with_ts = key_bytes_without_ht;
    key_with_ts.AppendValueType(ValueType::kHybridTime);
    key_with_ts.AppendHybridTimeForSeek(read_time_.limit);
    RETURN_NOT_OK(SeekForwardRegular(key_with_ts, key_bytes_without_ht));
  }

  DocHybridTime hybrid_time;
  bool found_later_regular_result = false;

  if (iter_valid_) {
    bool only_lacks_ht = false;
        RETURN_NOT_OK(key_bytes_without_ht.OnlyLacksHybridTimeFrom(iter_->key(), &only_lacks_ht));
    if (only_lacks_ht) {
      RETURN_NOT_OK(DecodeHybridTimeFromEndOfKey(iter_->key(), &hybrid_time));
      if (hybrid_time > *max_deleted_ts) {
        *max_deleted_ts = hybrid_time;
      }
      found_later_regular_result = true;
      // TODO when we support TTL on non-leaf nodes, we need to take that into account here.
    }
  }

  if (result_value) {
    if (found_later_regular_result) {
      RETURN_NOT_OK(result_value->Decode(iter_->value()));
    } else if (found_later_intent_result) {
      RETURN_NOT_OK(result_value->Decode(resolved_intent_value_));
    }
  }

  return Status::OK();
}

void IntentAwareIterator::PushPrefix(const Slice& prefix) {
  prefix_stack_.push_back(prefix);
  SkipFutureRecords();
  SkipFutureIntents();
}

void IntentAwareIterator::PopPrefix() {
  prefix_stack_.pop_back();
  SkipFutureRecords();
  SkipFutureIntents();
}

void IntentAwareIterator::SkipFutureRecords() {
  auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();
  while (iter_->Valid()) {
    if (!iter_->key().starts_with(prefix)) {
      VLOG(4) << "Unmatched prefix: " << iter_->key().ToDebugString()
              << ", prefix: " << prefix.ToDebugString();
      iter_valid_ = false;
      return;
    }
    DocHybridTime doc_ht;
    auto decode_status = doc_ht.DecodeFromEnd(iter_->key());
    LOG_IF(DFATAL, !decode_status.ok()) << "Decode key failed: " << decode_status
                                        << ", key: " << iter_->key().ToDebugString();
    if (decode_status.ok() && doc_ht.hybrid_time() <= read_time_.limit) {
      iter_valid_ = true;
      return;
    }
    VLOG(4) << "Skipping because of time: " << iter_->key().ToDebugString();
    iter_->Next(); // TODO(dtxn) use seek with the same key, but read limit as doc hybrid time.
  }
  iter_valid_ = false;
}

void IntentAwareIterator::SkipFutureIntents() {
  if (!intent_iter_) {
    return;
  }
  auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    VLOG(4) << "Checking resolved intent: " << resolved_intent_key_prefix_.ToString()
            << ", against new prefix: " << prefix.ToDebugString();
    auto resolved_intent_key_prefix = resolved_intent_key_prefix_.AsSlice();
    resolved_intent_key_prefix.consume_byte();
    auto compare_result = resolved_intent_key_prefix.compare_prefix(prefix);
    if (compare_result == 0) {
      resolved_intent_state_ = ResolvedIntentState::kValid;
      return;
    } else if (compare_result > 0) {
      resolved_intent_state_ = ResolvedIntentState::kInvalidPrefix;
      return;
    }
  }
  CHECK_OK(SeekForwardToSuitableIntent()); // TODO(dtxn) remember&report error
}

}  // namespace docdb
}  // namespace yb

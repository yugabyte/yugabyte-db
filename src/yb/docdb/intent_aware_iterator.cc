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

#include <future>
#include <thread>
#include <boost/optional/optional_io.hpp>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/value.h"

using namespace std::literals;

DEFINE_bool(transaction_allow_rerequest_status_in_tests, true,
            "Allow rerequest transaction status when try again is received.");

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

} // namespace

// For locally committed transactions returns commit time if committed at specified time or
// HybridTime::kMin otherwise. For other transactions returns HybridTime::kInvalid.
HybridTime TransactionStatusCache::GetLocalCommitTime(const TransactionId& transaction_id) {
  const HybridTime local_commit_time = txn_status_manager_->LocalCommitTime(transaction_id);
  return local_commit_time.is_valid()
      ? local_commit_time <= read_time_.global_limit ? local_commit_time : HybridTime::kMin
      : local_commit_time;
}

Result<HybridTime> TransactionStatusCache::GetCommitTime(const TransactionId& transaction_id) {
  auto it = cache_.find(transaction_id);
  if (it != cache_.end()) {
    return it->second;
  }

  auto result = DoGetCommitTime(transaction_id);
  if (result.ok()) {
    cache_.emplace(transaction_id, *result);
  }
  return result;
}

Result<HybridTime> TransactionStatusCache::DoGetCommitTime(const TransactionId& transaction_id) {
  HybridTime local_commit_time = GetLocalCommitTime(transaction_id);
  if (local_commit_time.is_valid()) {
    return local_commit_time;
  }

  TransactionStatusResult txn_status;
  for(;;) {
    std::promise<Result<TransactionStatusResult>> txn_status_promise;
    auto future = txn_status_promise.get_future();
    auto callback = [&txn_status_promise](Result<TransactionStatusResult> result) {
      txn_status_promise.set_value(std::move(result));
    };
    txn_status_manager_->RequestStatusAt(
        {&transaction_id, read_time_.read, read_time_.global_limit, callback});
    future.wait();
    auto txn_status_result = future.get();
    if (txn_status_result.ok()) {
      txn_status = std::move(*txn_status_result);
      break;
    }
    LOG(WARNING)
        << "Failed to request transaction " << yb::ToString(transaction_id) << " status: "
        <<  txn_status_result.status();
    if (!txn_status_result.status().IsTryAgain()) {
      return std::move(txn_status_result.status());
    }
    DCHECK(FLAGS_transaction_allow_rerequest_status_in_tests);
    // TODO(dtxn) In case of TryAgain error status we need to re-request transaction status.
    // Temporary workaround is to sleep for 0.05s and re-request.
    std::this_thread::sleep_for(50ms);
  }
  VLOG(4) << "Transaction_id " << transaction_id << " at " << read_time_
          << ": status: " << TransactionStatus_Name(txn_status.status)
          << ", status_time: " << txn_status.status_time;
  // There could be case when transaction was committed and applied between previous call to
  // GetLocalCommitTime, in this case coordinator does not know transaction and will respond
  // with ABORTED status. So we recheck whether it was committed locally.
  if (txn_status.status == TransactionStatus::ABORTED) {
    local_commit_time = GetLocalCommitTime(transaction_id);
    return local_commit_time.is_valid() ? local_commit_time : HybridTime::kMin;
  } else {
    return txn_status.status == TransactionStatus::COMMITTED ? txn_status.status_time
                                                             : HybridTime::kMin;
  }
}

namespace {

struct DecodeStrongWriteIntentResult {
  Slice intent_prefix;
  Slice intent_value;
  DocHybridTime value_time;
  IntentType intent_type;

  // Whether this intent from the same transaction as specified in context.
  bool same_transaction = false;

  std::string ToString() const {
    return Format("{ intent_prefix: $0 intent_value: $1 value_time: $2 same_transaction: $3 "
                  "intent_type: $4 }",
                  intent_prefix.ToDebugHexString(), intent_value.ToDebugHexString(), value_time,
                  same_transaction, yb::ToString(intent_type));
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
    TransactionOperationContext txn_op_context, rocksdb::Iterator* intent_iter,
    TransactionStatusCache* transaction_status_cache) {
  DocHybridTime intent_ht;
  DecodeStrongWriteIntentResult result;
  RETURN_NOT_OK(DecodeIntentKey(
     intent_iter->key(), &result.intent_prefix, &result.intent_type, &intent_ht));
  if (IsStrongWriteIntent(result.intent_type)) {
    result.intent_value = intent_iter->value();
    Result<TransactionId> txn_id = DecodeTransactionIdFromIntentValue(&result.intent_value);
    RETURN_NOT_OK(txn_id);
    result.same_transaction = *txn_id == txn_op_context.transaction_id;
    if (result.same_transaction) {
      result.value_time = intent_ht;
    } else {
      Result<HybridTime> commit_ht = transaction_status_cache->GetCommitTime(*txn_id);
      RETURN_NOT_OK(commit_ht);
      VLOG(4) << "Transaction id: " << *txn_id << " commit time: " << *commit_ht;
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
  return key.ToDebugHexString() + " (" + key_decoded.ToString() + ")";
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
      txn_op_context_(txn_op_context),
      transaction_status_cache_(
          txn_op_context ? &txn_op_context->txn_status_manager : nullptr, read_time) {
  VLOG(4) << "IntentAwareIterator, read_time: " << read_time
          << ", txp_op_context: " << txn_op_context_;
  if (txn_op_context.is_initialized()) {
    intent_iter_ = docdb::CreateRocksDBIterator(rocksdb,
                                                docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                                boost::none,
                                                rocksdb::kDefaultQueryId);
  }
  iter_.reset(rocksdb->NewIterator(read_opts));
}

void IntentAwareIterator::Seek(const DocKey &doc_key) {
  SeekWithoutHt(doc_key.Encode());
}

void IntentAwareIterator::SeekWithoutHt(const Slice& key) {
  VLOG(4) << "SeekWithoutHt(" << key.ToDebugHexString() << ")";
  DOCDB_DEBUG_SCOPE_LOG(
      key.ToDebugString(),
      std::bind(&IntentAwareIterator::DebugDump, this));
  if (!status_.ok()) {
    return;
  }

  ROCKSDB_SEEK(iter_.get(), key);
  SkipFutureRecords();
  if (intent_iter_) {
    ROCKSDB_SEEK(intent_iter_.get(), GetIntentPrefixForKeyWithoutHt(key));
    SeekForwardToSuitableIntent();
  }
}

void IntentAwareIterator::SeekForwardWithoutHt(const Slice& key) {
  VLOG(4) << "SeekForwardWithoutHt(" << key.ToDebugHexString() << ")";
  DOCDB_DEBUG_SCOPE_LOG(
      key.ToDebugHexString(),
      std::bind(&IntentAwareIterator::DebugDump, this));
  if (!status_.ok()) {
    return;
  }

  SeekForwardRegular(key);
  if (intent_iter_ && status_.ok()) {
    SeekForwardToSuitableIntent(GetIntentPrefixForKeyWithoutHt(key));
  }
}

void IntentAwareIterator::SeekForwardIgnoreHt(const SubDocKey& subdoc_key) {
  VLOG(4) << "SeekForwardIgnoreHt(" << subdoc_key.ToString() << ")";
  auto subdoc_key_encoded = subdoc_key.Encode(false /* include_hybrid_time */);
  AppendDocHybridTime(DocHybridTime(read_time_.global_limit, kMaxWriteId), &subdoc_key_encoded);
  return SeekForwardWithoutHt(subdoc_key_encoded);
}

void IntentAwareIterator::SeekPastSubKey(const SubDocKey& subdoc_key) {
  VLOG(4) << "SeekPastSubKey(" << subdoc_key.ToString() << ")";
  if (!status_.ok()) {
    return;
  }

  docdb::SeekPastSubKey(subdoc_key, iter_.get());
  SkipFutureRecords();
  if (intent_iter_ && status_.ok()) {
    KeyBytes intent_prefix = GetIntentPrefixForKey(subdoc_key);
    // Skip all intents for subdoc_key.
    intent_prefix.mutable_data()->push_back(static_cast<char>(ValueType::kIntentType) + 1);
    SeekForwardToSuitableIntent(intent_prefix);
  }
}

void IntentAwareIterator::SeekOutOfSubDoc(const SubDocKey& subdoc_key) {
  VLOG(4) << "SeekOutOfSubDoc(" << subdoc_key.ToString() << ")";
  if (!status_.ok()) {
    return;
  }

  SeekForwardRegular(subdoc_key.AdvanceOutOfSubDoc());
  if (intent_iter_ && status_.ok()) {
    KeyBytes intent_prefix = GetIntentPrefixForKey(subdoc_key);
    // See comment for SubDocKey::AdvanceOutOfSubDoc.
    intent_prefix.AppendValueType(ValueType::kMaxByte);
    SeekForwardToSuitableIntent(intent_prefix);
  }
}

void IntentAwareIterator::SeekToLastDocKey() {
  if (intent_iter_) {
    // TODO (dtxn): Implement SeekToLast when inten intents are present. Since part of the
    // is made of intents, we may have to avoid that. This is needed when distributed txns are fully
    // supported.
    return;
  }
  iter_->SeekToLast();
  if (!iter_->Valid()) {
    return;
  }
  // Seek to the first rocksdb kv-pair for this row.
  rocksdb::Slice rocksdb_key(iter_->key());
  DocKey doc_key;
  status_ = doc_key.DecodeFrom(&rocksdb_key);
  if (!status_.ok()) {
    return;
  }
  KeyBytes encoded_doc_key = doc_key.Encode();
  SeekWithoutHt(encoded_doc_key);
}

void IntentAwareIterator::PrevDocKey(const DocKey& doc_key) {
  Seek(doc_key);
  if (!status_.ok()) {
    return;
  }
  if (!iter_->Valid()) {
    SeekToLastDocKey();
    return;
  }
  iter_->Prev();
  if (!iter_->Valid()) {
    iter_valid_ = false; // TODO(dtxn) support reverse scan with read restart
    return;
  }
  Slice key_slice = iter_->key();
  DocKey prev_key;
  status_ = prev_key.DecodeFrom(&key_slice);
  if (!status_.ok()) {
    return;
  }
  Seek(prev_key);
}

bool IntentAwareIterator::valid() {
  return !status_.ok() || iter_valid_ || resolved_intent_state_ == ResolvedIntentState::kValid;
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

Result<Slice> IntentAwareIterator::FetchKey() {
  RETURN_NOT_OK(status_);
  Slice result;
  if (IsEntryRegular()) {
    result = iter_->key();
  } else {
    DCHECK_EQ(ResolvedIntentState::kValid, resolved_intent_state_);
    result = resolved_intent_sub_doc_key_encoded_;
  }
  DocHybridTime doc_ht;
  RETURN_NOT_OK(DecodeHybridTimeFromEndOfKey(result, &doc_ht));
  max_seen_ht_.MakeAtLeast(doc_ht.hybrid_time());
  VLOG(4) << "Fetched key " << SubDocKey::DebugSliceToString(result)
          <<  ", with time: " << doc_ht.hybrid_time()
          << ", while read bounds are: " << read_time_;
  return result;
}

Slice IntentAwareIterator::value() {
  if (IsEntryRegular()) {
    return iter_->value();
  } else {
    DCHECK_EQ(ResolvedIntentState::kValid, resolved_intent_state_);
    return resolved_intent_value_;
  }
}

void IntentAwareIterator::SeekForwardRegular(const Slice& slice, const Slice& prefix) {
  docdb::SeekForward(slice, iter_.get());
  SkipFutureRecords();
}

void IntentAwareIterator::ProcessIntent() {
  auto decode_result = DecodeStrongWriteIntent(
      txn_op_context_.get(), intent_iter_.get(), &transaction_status_cache_);
  if (!decode_result.ok()) {
    status_ = decode_result.status();
    return;
  }
  VLOG(4) << "Intent decode: " << DebugIntentKeyToString(intent_iter_->key())
          << " => " << intent_iter_->value().ToDebugHexString() << ", result: " << *decode_result;
  DOCDB_DEBUG_LOG(
      "resolved_intent_txn_dht_: $0 value_time: $1 read_time: $2",
      resolved_intent_txn_dht_.ToString(),
      decode_result->value_time.ToString(),
      read_time_.ToString());
  auto real_time = decode_result->same_transaction ? intent_dht_from_same_txn_
                                                   : resolved_intent_txn_dht_;
  if (decode_result->value_time > real_time &&
      (decode_result->same_transaction ||
           decode_result->value_time.hybrid_time() <= read_time_.global_limit)) {
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
}

void IntentAwareIterator::UpdateResolvedIntentSubDocKeyEncoded() {
  resolved_intent_sub_doc_key_encoded_.ResetRawBytes(
      resolved_intent_key_prefix_.data().data() + 1, resolved_intent_key_prefix_.size() - 1);
  resolved_intent_sub_doc_key_encoded_.AppendValueType(ValueType::kHybridTime);
  resolved_intent_sub_doc_key_encoded_.AppendHybridTime(resolved_intent_txn_dht_);
  VLOG(4) << "Resolved intent SubDocKey: "
          << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_);
}

void IntentAwareIterator::SeekForwardToSuitableIntent(const KeyBytes &intent_key_prefix) {
  DOCDB_DEBUG_SCOPE_LOG(intent_key_prefix.ToString(),
                        std::bind(&IntentAwareIterator::DebugDump, this));
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent &&
      resolved_intent_key_prefix_.CompareTo(intent_key_prefix) >= 0) {
    return;
  }
  docdb::SeekForward(intent_key_prefix, intent_iter_.get());
  SeekForwardToSuitableIntent();
}

void IntentAwareIterator::SeekForwardToSuitableIntent() {
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
    VLOG(4) << "Intent found: " << DebugIntentKeyToString(intent_key)
            << ", resolved state: " << yb::ToString(resolved_intent_state_);
    if (resolved_intent_state_ != ResolvedIntentState::kNoIntent &&
        // Only scan intents for the first SubDocKey having suitable intents.
        !IsIntentForTheSameKey(intent_key, resolved_intent_key_prefix_)) {
      break;
    }
    intent_key.consume_byte();
    if (!intent_key.starts_with(prefix)) {
      break;
    }
    ProcessIntent();
    if (!status_.ok()) {
      return;
    }
    intent_iter_->Next();
  }
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    UpdateResolvedIntentSubDocKeyEncoded();
  }
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
      LOG(INFO) << "intent_iter_->key(): " << intent_iter_->key().ToDebugHexString();
    }
  }
  LOG(INFO) << "resolved_intent_state_: " << yb::ToString(resolved_intent_state_);
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    LOG(INFO) << "resolved_intent_sub_doc_key_encoded_: "
              << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_);
  }
  LOG(INFO) << "valid(): " << valid();
  if (valid()) {
    auto key = FetchKey();
    if (key.ok()) {
      LOG(INFO) << "key(): " << DebugDumpKeyToStr(*key);
    } else {
      LOG(INFO) << "key(): fetch failed: " << key.status();
    }
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

  RETURN_NOT_OK(status_);

  bool found_later_intent_result = false;
  if (intent_iter_) {
    const auto intent_prefix = GetIntentPrefixForKeyWithoutHt(key_bytes_without_ht);
    SeekForwardToSuitableIntent(intent_prefix);
    RETURN_NOT_OK(status_);
    if (resolved_intent_state_ == ResolvedIntentState::kValid &&
        resolved_intent_txn_dht_ > *max_deleted_ts &&
        resolved_intent_key_prefix_.CompareTo(intent_prefix) == 0) {
      *max_deleted_ts = resolved_intent_txn_dht_;
      max_seen_ht_.MakeAtLeast(max_deleted_ts->hybrid_time());
      found_later_intent_result = true;
    }
  }

  {
    KeyBytes key_with_ts = key_bytes_without_ht;
    key_with_ts.AppendValueType(ValueType::kHybridTime);
    key_with_ts.AppendHybridTimeForSeek(read_time_.global_limit);
    SeekForwardRegular(key_with_ts, key_bytes_without_ht);
    RETURN_NOT_OK(status_);
  }

  DocHybridTime doc_ht;
  bool found_later_regular_result = false;

  if (iter_valid_) {
    bool only_lacks_ht = false;
        RETURN_NOT_OK(key_bytes_without_ht.OnlyLacksHybridTimeFrom(iter_->key(), &only_lacks_ht));
    if (only_lacks_ht) {
      RETURN_NOT_OK(DecodeHybridTimeFromEndOfKey(iter_->key(), &doc_ht));
      if (doc_ht > *max_deleted_ts) {
        *max_deleted_ts = doc_ht;
        max_seen_ht_.MakeAtLeast(doc_ht.hybrid_time());
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
  VLOG(4) << "PushPrefix: " << prefix.ToDebugHexString();
  prefix_stack_.push_back(prefix);
  SkipFutureRecords();
  SkipFutureIntents();
}

void IntentAwareIterator::PopPrefix() {
  prefix_stack_.pop_back();
  SkipFutureRecords();
  SkipFutureIntents();
  VLOG(4) << "PopPrefix: "
          << (prefix_stack_.empty() ? Slice() : prefix_stack_.back()).ToDebugHexString();
}

void IntentAwareIterator::SkipFutureRecords() {
  if (!status_.ok()) {
    return;
  }
  auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();
  while (iter_->Valid()) {
    if (!iter_->key().starts_with(prefix)) {
      VLOG(4) << "Unmatched prefix: " << iter_->key().ToDebugHexString()
              << ", prefix: " << prefix.ToDebugHexString();
      iter_valid_ = false;
      return;
    }
    DocHybridTime doc_ht;
    auto decode_status = doc_ht.DecodeFromEnd(iter_->key());
    if (!decode_status.ok()) {
      LOG(ERROR) << "Decode key failed: " << decode_status
                 << ", key: " << iter_->key().ToDebugHexString();
      status_ = std::move(decode_status);
      return;
    }
    auto value = iter_->value();
    auto value_type = static_cast<ValueType>(value[0]);
    if (value_type == ValueType::kHybridTime) {
      // Value came from a transaction, we could try to filter it by original intent time.
      DocHybridTime intent_doc_ht;
      value.consume_byte();
      decode_status = intent_doc_ht.DecodeFrom(&value);
      if (!decode_status.ok()) {
        status_ = std::move(decode_status);
        return;
      }
      if (intent_doc_ht.hybrid_time() <= read_time_.local_limit &&
          doc_ht.hybrid_time() <= read_time_.global_limit) {
        iter_valid_ = true;
        return;
      }
    } else if (doc_ht.hybrid_time() <= read_time_.local_limit) {
      iter_valid_ = true;
      return;
    }
    VLOG(4) << "Skipping because of time: " << iter_->key().ToDebugHexString();
    iter_->Next(); // TODO(dtxn) use seek with the same key, but read limit as doc hybrid time.
  }
  iter_valid_ = false;
}

void IntentAwareIterator::SkipFutureIntents() {
  if (!intent_iter_ || !status_.ok()) {
    return;
  }
  auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    VLOG(4) << "Checking resolved intent: " << resolved_intent_key_prefix_.ToString()
            << ", against new prefix: " << prefix.ToDebugHexString();
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
  SeekForwardToSuitableIntent();
}

}  // namespace docdb
}  // namespace yb

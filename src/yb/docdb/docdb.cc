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

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/hybrid_time.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/transaction.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/shared_lock_manager.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/server/hybrid_clock.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/date_time.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/util/metrics.h"

using std::endl;
using std::list;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::shared_ptr;
using std::stack;
using std::vector;
using std::make_shared;

using yb::HybridTime;
using yb::util::FormatBytesAsStr;
using yb::FormatRocksDBSliceAsStr;
using strings::Substitute;


namespace yb {
namespace docdb {

namespace {

constexpr size_t kMaxWordsPerEncodedHybridTimeWithValueType =
    ((kMaxBytesPerEncodedHybridTime + 1) + sizeof(size_t) - 1) / sizeof(size_t);

// Main intent data::
// Prefix + DocPath + IntentType + DocHybridTime -> TxnId + value of the intent
// Reverse index by txn id:
// Prefix + TxnId + DocHybridTime -> Main intent data key
//
// Expects that last entry of key is DocHybridTime.
void AddIntent(
    const TransactionId& transaction_id,
    const SliceParts& key,
    const SliceParts& value,
    rocksdb::WriteBatch* rocksdb_write_batch) {
  char reverse_key_prefix[1] = { ValueTypeAsChar::kTransactionId };
  size_t doc_ht_buffer[kMaxWordsPerEncodedHybridTimeWithValueType];
  auto doc_ht_slice = key.parts[key.num_parts - 1];
  memcpy(doc_ht_buffer, doc_ht_slice.data(), doc_ht_slice.size());
  for (size_t i = 0; i != kMaxWordsPerEncodedHybridTimeWithValueType; ++i) {
    doc_ht_buffer[i] = ~doc_ht_buffer[i];
  }
  doc_ht_slice = Slice(pointer_cast<char*>(doc_ht_buffer), doc_ht_slice.size());

  std::array<Slice, 3> reverse_key = {{
      Slice(reverse_key_prefix, sizeof(reverse_key_prefix)),
      Slice(transaction_id.data, transaction_id.size()),
      doc_ht_slice,
  }};
  rocksdb_write_batch->Put(key, value);
  rocksdb_write_batch->Put(reverse_key, key);
}

void ApplyIntent(const string& lock_string,
                 const IntentType intent,
                 KeyToIntentTypeMap *keys_locked) {
  auto itr = keys_locked->find(lock_string);
  if (itr == keys_locked->end()) {
    keys_locked->emplace(lock_string, intent);
  } else {
    itr->second = SharedLockManager::CombineIntents(itr->second, intent);
  }
}

}  // namespace

const SliceKeyBound& SliceKeyBound::Invalid() {
  static SliceKeyBound result;
  return result;
}

const IndexBound& IndexBound::Empty() {
  static IndexBound result;
  return result;
}

void PrepareDocWriteOperation(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                              const scoped_refptr<Histogram>& write_lock_latency,
                              IsolationLevel isolation_level,
                              SharedLockManager *lock_manager,
                              LockBatch *keys_locked,
                              bool *need_read_snapshot) {
  KeyToIntentTypeMap key_to_lock_type;
  *need_read_snapshot = false;
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    list<DocPath> doc_paths;
    IsolationLevel level;
    doc_op->GetDocPathsToLock(&doc_paths, &level);
    if (isolation_level != IsolationLevel::NON_TRANSACTIONAL) {
      level = isolation_level;
    }
    const IntentTypePair intent_types = GetWriteIntentsForIsolationLevel(level);

    for (const auto& doc_path : doc_paths) {
      KeyBytes current_prefix = doc_path.encoded_doc_key();
      for (int i = 0; i < doc_path.num_subkeys(); i++) {
        ApplyIntent(current_prefix.AsStringRef(), intent_types.weak, &key_to_lock_type);
        doc_path.subkey(i).AppendToKey(&current_prefix);
      }
      ApplyIntent(current_prefix.AsStringRef(), intent_types.strong, &key_to_lock_type);
    }
    if (doc_op->RequireReadSnapshot()) {
      *need_read_snapshot = true;
    }
  }
  const MonoTime start_time = (write_lock_latency != nullptr) ? MonoTime::Now() : MonoTime();
  *keys_locked = LockBatch(lock_manager, std::move(key_to_lock_type));
  if (write_lock_latency != nullptr) {
    const MonoDelta elapsed_time = MonoTime::Now().GetDeltaSince(start_time);
    write_lock_latency->Increment(elapsed_time.ToMicroseconds());
  }
}

Status ExecuteDocWriteOperation(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                                MonoTime deadline,
                                const ReadHybridTime& read_time,
                                const DocDB& doc_db,
                                KeyValueWriteBatchPB* write_batch,
                                InitMarkerBehavior init_marker_behavior,
                                std::atomic<int64_t>* monotonic_counter,
                                HybridTime* restart_read_ht) {
  DCHECK_ONLY_NOTNULL(restart_read_ht);
  DocWriteBatch doc_write_batch(doc_db, init_marker_behavior, monotonic_counter);
  DocOperationApplyData data = {&doc_write_batch, deadline, read_time, restart_read_ht};
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    Status s = doc_op->Apply(data);
    if (doc_op->OpType() == DocOperation::Type::QL_WRITE_OPERATION && s.IsQLError()) {
      // Ensure we set appropriate error in the response object for QL errors.
      const auto& resp = down_cast<QLWriteOperation*>(doc_op.get())->response();
      resp->set_status(QLResponsePB::YQL_STATUS_SQL_ERROR);
      resp->set_error_message(s.message().ToBuffer());
      continue;
    }
    RETURN_NOT_OK(s);
  }
  doc_write_batch.MoveToWriteBatchPB(write_batch);
  return Status::OK();
}

void PrepareNonTransactionWriteBatch(
    const KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::WriteBatch* rocksdb_write_batch) {
  DocHybridTimeBuffer doc_ht_buffer;
  for (int write_id = 0; write_id < put_batch.kv_pairs_size(); ++write_id) {
    const auto& kv_pair = put_batch.kv_pairs(write_id);
    CHECK(!kv_pair.key().empty());
    CHECK(!kv_pair.value().empty());

#ifndef NDEBUG
    // Debug-only: ensure all keys we get in Raft replication can be decoded.
    {
      SubDocKey subdoc_key;
      Status s = subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(kv_pair.key());
      CHECK(s.ok())
          << "Failed decoding key: " << s.ToString() << "; "
          << "Problematic key: " << BestEffortDocDBKeyToStr(KeyBytes(kv_pair.key())) << "\n"
          << "value: " << util::FormatBytesAsStr(kv_pair.value()) << "\n"
          << "put_batch:\n" << put_batch.DebugString();
    }
#endif

    // We replicate encoded SubDocKeys without a HybridTime at the end, and only append it here.
    // The reason for this is that the HybridTime timestamp is only picked at the time of
    // appending  an entry to the tablet's Raft log. Also this is a good way to save network
    // bandwidth.
    //
    // "Write id" is the final component of our HybridTime encoding (or, to be more precise,
    // DocHybridTime encoding) that helps disambiguate between different updates to the
    // same key (row/column) within a transaction. We set it based on the position of the write
    // operation in its write batch.

    std::array<Slice, 2> key_parts = {{
        Slice(kv_pair.key()),
        doc_ht_buffer.EncodeWithValueType(hybrid_time, write_id),
    }};
    Slice key_value = kv_pair.value();
    rocksdb_write_batch->Put(key_parts, { &key_value, 1 });
  }
}

CHECKED_STATUS EnumerateIntents(
    const google::protobuf::RepeatedPtrField<KeyValuePairPB> &kv_pairs,
    boost::function<Status(IntentKind, Slice, KeyBytes*)> functor) {
  KeyBytes encoded_key;

  for (int index = 0; index < kv_pairs.size(); ++index) {
    const auto &kv_pair = kv_pairs.Get(index);
    CHECK(!kv_pair.key().empty());
    CHECK(!kv_pair.value().empty());
    Slice key = kv_pair.key();
    auto key_size = DocKey::EncodedSize(key, DocKeyPart::WHOLE_DOC_KEY);
    CHECK_OK(key_size);

    encoded_key.Clear();
    encoded_key.AppendRawBytes(key.cdata(), *key_size);
    key.remove_prefix(*key_size);

    for (;;) {
      auto subkey_begin = key.cdata();
      auto decode_result = SubDocKey::DecodeSubkey(&key);
      CHECK_OK(decode_result);
      if (!decode_result.get()) {
        break;
      }
      RETURN_NOT_OK(functor(IntentKind::kWeak, Slice(), &encoded_key));
      encoded_key.AppendRawBytes(subkey_begin, key.cdata() - subkey_begin);
    }

    RETURN_NOT_OK(functor(IntentKind::kStrong, kv_pair.value(), &encoded_key));
  }

  return Status::OK();
}

class PrepareTransactionWriteBatchHelper {
 public:
  PrepareTransactionWriteBatchHelper(const PrepareTransactionWriteBatchHelper&) = delete;
  void operator=(const PrepareTransactionWriteBatchHelper&) = delete;

  // `rocksdb_write_batch` - in-out parameter is filled by this prepare.
  PrepareTransactionWriteBatchHelper(HybridTime hybrid_time,
                                     rocksdb::WriteBatch* rocksdb_write_batch,
                                     const TransactionId& transaction_id,
                                     IsolationLevel isolation_level,
                                     IntraTxnWriteId* intra_txn_write_id)
      : hybrid_time_(hybrid_time),
        rocksdb_write_batch_(rocksdb_write_batch),
        transaction_id_(transaction_id),
        intent_types_(GetWriteIntentsForIsolationLevel(isolation_level)),
        intra_txn_write_id_(intra_txn_write_id) {
  }

  // Using operator() to pass this object conveniently to EnumerateIntents.
  CHECKED_STATUS operator()(IntentKind intent_kind, Slice value_slice, KeyBytes* key) {
    if (intent_kind == IntentKind::kWeak) {
      weak_intents_.insert(key->data());
      return Status::OK();
    }

    const auto transaction_value_type = ValueTypeAsChar::kTransactionId;
    const auto write_id_value_type = ValueTypeAsChar::kWriteId;
    IntraTxnWriteId big_endian_write_id = BigEndian::FromHost32(*intra_txn_write_id_);
    std::array<Slice, 5> value = {{
        Slice(&transaction_value_type, 1),
        Slice(transaction_id_.data, transaction_id_.size()),
        Slice(&write_id_value_type, 1),
        Slice(pointer_cast<char*>(&big_endian_write_id), sizeof(big_endian_write_id)),
        value_slice
    }};

    ++*intra_txn_write_id_;

    char intent_type[2] = { ValueTypeAsChar::kIntentType, static_cast<char>(intent_types_.strong) };

    DocHybridTimeBuffer doc_ht_buffer;

    std::array<Slice, 3> key_parts = {{
        key->AsSlice(),
        Slice(intent_type, 2),
        doc_ht_buffer.EncodeWithValueType(hybrid_time_, write_id_++),
    }};
    AddIntent(transaction_id_, key_parts, value, rocksdb_write_batch_);

    return Status::OK();
  }

  void Finish() {
    char transaction_id_value_type = ValueTypeAsChar::kTransactionId;
    char intent_type[2] = { ValueTypeAsChar::kIntentType, static_cast<char>(intent_types_.weak) };

    DocHybridTimeBuffer doc_ht_buffer;

    std::array<Slice, 2> value = {{
        Slice(&transaction_id_value_type, 1),
        Slice(transaction_id_.data, transaction_id_.size()),
    }};

    for (const auto& intent : weak_intents_) {
      std::array<Slice, 3> key = {{
          Slice(intent),
          Slice(intent_type, 2),
          doc_ht_buffer.EncodeWithValueType(hybrid_time_, write_id_++),
      }};

      AddIntent(transaction_id_, key, value, rocksdb_write_batch_);
    }
  }

 private:
  // TODO(dtxn) weak & strong intent in one batch.
  // TODO(dtxn) extract part of code knowning about intents structure to lower level.
  HybridTime hybrid_time_;
  rocksdb::WriteBatch* rocksdb_write_batch_;
  const TransactionId& transaction_id_;
  IntentTypePair intent_types_;
  std::unordered_set<std::string> weak_intents_;
  IntraTxnWriteId write_id_ = 0;
  IntraTxnWriteId* intra_txn_write_id_;
};

// We have the following distinct types of data in this "intent store":
// Main intent data:
//   Prefix + SubDocKey (no HybridTime) + IntentType + HybridTime -> TxnId + value of the intent
// Transaction metadata
//   TxnId -> status tablet id + isolation level
// Reverse index by txn id
//   TxnId + HybridTime -> Main intent data key
//
// Where prefix is just a single byte prefix. TxnId, IntentType, HybridTime all prefixed with
// appropriate value type.
void PrepareTransactionWriteBatch(
    const KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::WriteBatch* rocksdb_write_batch,
    const TransactionId& transaction_id,
    IsolationLevel isolation_level,
    IntraTxnWriteId* write_id) {
  VLOG(4) << "PrepareTransactionWriteBatch(), write_id = " << *write_id;

  PrepareTransactionWriteBatchHelper helper(
      hybrid_time, rocksdb_write_batch, transaction_id, isolation_level, write_id);

  // We cannot recover from failures here, because it means that we cannot apply replicated
  // operation.
  CHECK_OK(EnumerateIntents(put_batch.kv_pairs(), std::ref(helper)));

  helper.Finish();
}

// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

namespace {

void SeekToLowerBound(const SliceKeyBound& lower_bound, IntentAwareIterator* iter) {
  if (lower_bound.is_exclusive()) {
    iter->SeekPastSubKey(lower_bound.key());
  } else {
    iter->SeekForward(lower_bound.key());
  }
}

// This function does not assume that object init_markers are present. If no init marker is present,
// or if a tombstone is found at some level, it still looks for subkeys inside it if they have
// larger timestamps.
//
// TODO(akashnil): ENG-1152: If object init markers were required, this read path may be optimized.
// We look at all rocksdb keys with prefix = subdocument_key, and construct a subdocument out of
// them, between the timestamp range high_ts and low_ts.
//
// The iterator is expected to be placed at the smallest key that is subdocument_key or later, and
// after the function returns, the iterator should be placed just completely outside the
// subdocument_key prefix. Although if high_subkey is specified, the iterator is only guaranteed
// to be positioned after the high_subkey and not necessarily outside the subdocument_key prefix.
// num_values_observed is used for queries on indices, and keeps track of the number of primitive
// values observed thus far. In a query with lower index bound k, ignore the first k primitive
// values before building the subdocument.
CHECKED_STATUS BuildSubDocument(
    IntentAwareIterator* iter,
    const GetSubDocumentData& data,
    DocHybridTime low_ts,
    int64* num_values_observed) {
  VLOG(3) << "BuildSubDocument data: " << data << " read_time: " << iter->read_time()
          << " low_ts: " << low_ts;
  while (iter->valid()) {
    // Since we modify num_values_observed on recursive calls, we keep a local copy of the value.
    int64 current_values_observed = *num_values_observed;
    DocHybridTime write_time;
    auto key = VERIFY_RESULT(iter->FetchKey(&write_time));
    VLOG(4) << "iter: " << SubDocKey::DebugSliceToString(key)
            << ", key: " << SubDocKey::DebugSliceToString(data.subdocument_key);
    DCHECK(key.starts_with(data.subdocument_key))
        << "iter: " << SubDocKey::DebugSliceToString(key)
        << ", key: " << SubDocKey::DebugSliceToString(data.subdocument_key);

    // Key could be invalidated because we could move iterator, so back it up.
    KeyBytes key_copy(key);
    key = key_copy.AsSlice();
    rocksdb::Slice value = iter->value();
    // Checking that IntentAwareIterator returns an entry with correct time.
    DCHECK_GE(iter->read_time().global_limit, write_time.hybrid_time())
        << "Found key: " << SubDocKey::DebugSliceToString(key);

    if (low_ts > write_time) {
      VLOG(3) << "SeekPastSubKey: " << SubDocKey::DebugSliceToString(key);
      iter->SeekPastSubKey(key);
      continue;
    }
    Value doc_value;
    RETURN_NOT_OK(doc_value.Decode(value));
    ValueType value_type = doc_value.value_type();
    if (key == data.subdocument_key) {
      if (write_time == DocHybridTime::kMin)
        return STATUS(Corruption, "No hybrid timestamp found on entry");

      // We may need to update the TTL in individual columns.
      if (write_time.hybrid_time() >= data.exp.write_ht) {
        // We want to keep the default TTL otherwise.
        if (doc_value.ttl() != Value::kMaxTtl) {
          data.exp.write_ht = write_time.hybrid_time();
          data.exp.ttl = doc_value.ttl();
        } else if (data.exp.ttl.IsNegative()) {
          data.exp.ttl = -data.exp.ttl;
        }
      }

      // If the hybrid time is kMin, then we must be using default TTL.
      if (data.exp.write_ht == HybridTime::kMin) {
        data.exp.write_ht = write_time.hybrid_time();
      }

      bool has_expired;
      CHECK_OK(HasExpiredTTL(data.exp.write_ht, data.exp.ttl,
                             iter->read_time().read, &has_expired));

      // Treat an expired value as a tombstone written at the same time as the original value.
      if (has_expired) {
        doc_value = Value::Tombstone();
        value_type = ValueType::kTombstone;
      }

      const bool is_collection = IsCollectionType(value_type);
      // We have found some key that matches our entire subdocument_key, i.e. we didn't skip ahead
      // to a lower level key (with optional object init markers).
      if (is_collection || value_type == ValueType::kTombstone) {
        if (low_ts < write_time) {
          low_ts = write_time;
        }
        if (is_collection) {
          *data.result = SubDocument(value_type);
        }

        // If the subkey lower bound filters out the key we found, we want to skip to the lower
        // bound. If it does not, we want to seek to the next key. This prevents an infinite loop
        // where the iterator keeps seeking to itself if the key we found matches the low subkey.
        // TODO: why are not we doing this for arrays?
        if (IsObjectType(value_type) && !data.low_subkey->CanInclude(key)) {
          // Try to seek to the low_subkey for efficiency.
          SeekToLowerBound(*data.low_subkey, iter);
        } else {
          VLOG(3) << "SeekPastSubKey: " << SubDocKey::DebugSliceToString(key);
          iter->SeekPastSubKey(key);
        }
        continue;
      } else {
        if (!IsPrimitiveValueType(value_type)) {
          return STATUS_FORMAT(Corruption,
              "Expected primitive value type, got $0", value_type);
        }
        DCHECK_GE(iter->read_time().global_limit, write_time.hybrid_time());
        // TODO: the ttl_seconds in primitive value is currently only in use for CQL. At some
        // point streamline by refactoring CQL to use the mutable Expiration in GetSubDocumentData.
        if (data.exp.ttl == Value::kMaxTtl) {
          doc_value.mutable_primitive_value()->SetTtl(-1);
        } else {
          int64_t time_since_write_seconds = (
              server::HybridClock::GetPhysicalValueMicros(iter->read_time().read) -
              server::HybridClock::GetPhysicalValueMicros(write_time.hybrid_time())) /
              MonoTime::kMicrosecondsPerSecond;
          int64_t ttl_seconds = std::max(static_cast<int64_t>(0),
              data.exp.ttl.ToMilliseconds() /
              MonoTime::kMillisecondsPerSecond - time_since_write_seconds);
          doc_value.mutable_primitive_value()->SetTtl(ttl_seconds);
        }
        // Choose the user supplied timestamp if present.
        const UserTimeMicros user_timestamp = doc_value.user_timestamp();
        doc_value.mutable_primitive_value()->SetWriteTime(
            user_timestamp == Value::kInvalidUserTimestamp
            ? write_time.hybrid_time().GetPhysicalValueMicros()
            : doc_value.user_timestamp());
        if (!data.high_index->CanInclude(current_values_observed)) {
          iter->SeekOutOfSubDoc(&key_copy);
          return Status::OK();
        }
        if (data.low_index->CanInclude(*num_values_observed)) {
          *data.result = SubDocument(doc_value.primitive_value());
        }
        (*num_values_observed)++;
        VLOG(3) << "SeekOutOfSubDoc: " << SubDocKey::DebugSliceToString(key);
        iter->SeekOutOfSubDoc(&key_copy);
        return Status::OK();
      }
    }
    SubDocument descendant{PrimitiveValue(ValueType::kInvalid)};
    // TODO: what if the key we found is the same as before?
    //       We'll get into an infinite recursion then.
    {
      IntentAwareIteratorPrefixScope prefix_scope(key, iter);
      RETURN_NOT_OK(BuildSubDocument(
          iter, data.Adjusted(key, &descendant), low_ts,
          num_values_observed));

    }
    if (descendant.value_type() == ValueType::kInvalid) {
      // The document was not found in this level (maybe a tombstone was encountered).
      continue;
    }

    if (!data.low_subkey->CanInclude(key)) {
      VLOG(3) << "Filtered by low_subkey: " << data.low_subkey->ToString()
              << ", key: " << SubDocKey::DebugSliceToString(key);
      // The value provided is lower than what we are looking for, seek to the lower bound.
      SeekToLowerBound(*data.low_subkey, iter);
      continue;
    }

    // We use num_values_observed as a conservative figure for lower bound and
    // current_values_observed for upper bound so we don't lose any data we should be including.
    if (!data.low_index->CanInclude(*num_values_observed)) {
      continue;
    }

    if (!data.high_subkey->CanInclude(key)) {
      VLOG(3) << "Filtered by high_subkey: " << data.high_subkey->ToString()
              << ", key: " << SubDocKey::DebugSliceToString(key);
      // We have encountered a subkey higher than our constraints, we should stop here.
      return Status::OK();
    }

    if (!data.high_index->CanInclude(current_values_observed)) {
      return Status::OK();
    }

    if (!IsObjectType(data.result->value_type())) {
      *data.result = SubDocument();
    }

    SubDocument* current = data.result;
    size_t num_children;
    RETURN_NOT_OK(current->NumChildren(&num_children));
    if (data.limit != 0 && num_children >= data.limit) {
      // We have processed enough records.
      return Status::OK();
    }

    if (data.count_only) {
      // We need to only count the records that we found.
      data.record_count++;
    } else {
      Slice temp = key;
      temp.remove_prefix(data.subdocument_key.size());
      for (;;) {
        PrimitiveValue child;
        RETURN_NOT_OK(child.DecodeFromKey(&temp));
        if (temp.empty()) {
          current->SetChild(child, std::move(descendant));
          break;
        }
        current = current->GetOrAddChild(child).first;
      }
    }
  }

  return Status::OK();
}

}  // namespace

yb::Status FindLastWriteTime(
    IntentAwareIterator* iter,
    const Slice& key_without_ht,
    DocHybridTime* max_overwrite_time,
    Expiration* exp,
    Value* result_value) {

  Slice value;
  DocHybridTime doc_ht = *max_overwrite_time;
  RETURN_NOT_OK(iter->FindLatestRecord(key_without_ht, &doc_ht, &value));
  if (!iter->valid()) {
    return Status::OK();
  }

  uint64_t merge_flags = 0;
  MonoDelta ttl;
  ValueType value_type;
  RETURN_NOT_OK(Value::DecodePrimitiveValueType(value, &value_type, &merge_flags, &ttl));
  if (value_type == ValueType::kInvalid) {
    return Status::OK();
  }

  // We update the expiration if and only if the write time is later than the write time
  // currently stored in expiration, and the record is not a regular record with default TTL.
  // This is done independently of whether the row is a TTL row.
  // In the case that the always_override flag is true, default TTL will not be preserved.
  Expiration new_exp = *exp;
  if (doc_ht.hybrid_time() >= exp->write_ht) {
    // We want to keep the default TTL otherwise.
    if (ttl != Value::kMaxTtl || merge_flags == Value::kTtlFlag || exp->always_override) {
      new_exp.write_ht = doc_ht.hybrid_time();
      new_exp.ttl = ttl;
    } else if (exp->ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    }
  }

  // If we encounter a TTL row, we assign max_overwrite_time to be the write time of the
  // original value/init marker.
  if (merge_flags == Value::kTtlFlag) {
    DocHybridTime new_ht;
    RETURN_NOT_OK(iter->NextFullValue(&new_ht, &value));

    // There could be a case where the TTL row exists, but the value has been
    // compacted away. Then, it is treated as a Tombstone written at the time
    // of the TTL row.
    if (!iter->valid() && !new_exp.ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    } else {
      ValueType value_type;
      RETURN_NOT_OK(Value::DecodePrimitiveValueType(value, &value_type));
      // Because we still do not know whether we are seeking something expired,
      // we must take the max_overwrite_time as if the value were not expired.
      doc_ht = new_ht;
    }
  }

  if ((value_type == ValueType::kTombstone || value_type == ValueType::kInvalid) &&
      !new_exp.ttl.IsNegative()) {
    new_exp.ttl = -new_exp.ttl;
  }
  *exp = new_exp;

  if (doc_ht > *max_overwrite_time) {
    *max_overwrite_time = doc_ht;
    VLOG(4) << "Max overwritten time for " << key_without_ht.ToDebugHexString() << ": "
            << *max_overwrite_time;
  }

  if (result_value)
    RETURN_NOT_OK(result_value->Decode(value));

  return Status::OK();
}

yb::Status GetSubDocument(
    const DocDB& doc_db,
    const GetSubDocumentData& data,
    const rocksdb::QueryId query_id,
    const TransactionOperationContextOpt& txn_op_context,
    MonoTime deadline,
    const ReadHybridTime& read_time) {
  auto iter = CreateIntentAwareIterator(
      doc_db, BloomFilterMode::USE_BLOOM_FILTER, data.subdocument_key, query_id,
      txn_op_context, deadline, read_time);
  return GetSubDocument(iter.get(), data, nullptr /* projection */, SeekFwdSuffices::kFalse);
}

yb::Status GetSubDocument(
    IntentAwareIterator *db_iter,
    const GetSubDocumentData& data,
    const std::vector<PrimitiveValue>* projection,
    const SeekFwdSuffices seek_fwd_suffices) {
  // TODO(dtxn) scan through all involved first transactions to cache statuses in a batch,
  // so during building subdocument we don't need to request them one by one.
  // TODO(dtxn) we need to restart read with scan_ht = commit_ht if some transaction was committed
  // at time commit_ht within [scan_ht; read_request_time + max_clock_skew). Also we need
  // to wait until time scan_ht = commit_ht passed.
  // TODO(dtxn) for each scanned key (and its subkeys) we need to avoid new values commits at
  // ht <= scan_ht (or just ht < scan_ht?)
  // Question: what will break if we allow later commit at ht <= scan_ht ? Need to write down
  // detailed example.
  *data.doc_found = false;
  DOCDB_DEBUG_LOG("GetSubDocument for key $0 @ $1", data.subdocument_key.ToDebugHexString(),
                  db_iter->read_time().ToString());

  // The latest time at which any prefix of the given key was overwritten.
  DocHybridTime max_overwrite_ht(DocHybridTime::kMin);
  VLOG(4) << "GetSubDocument(" << data << ")";

  SubDocKey found_subdoc_key;
  auto dockey_size =
      VERIFY_RESULT(DocKey::EncodedSize(data.subdocument_key, DocKeyPart::WHOLE_DOC_KEY));

  Slice key_slice(data.subdocument_key.data(), dockey_size);
  IntentAwareIteratorPrefixScope prefix_scope(key_slice, db_iter);
  if (seek_fwd_suffices) {
    db_iter->SeekForward(key_slice);
  } else {
    db_iter->Seek(key_slice);
  }
  Value doc_value;
  // Check ancestors for init markers, tombstones, and expiration, tracking
  // the expiration and corresponding most recent write time in exp, and the
  // the general most recent overwrite time in max_overwrite_ht
  {
    auto temp_key = data.subdocument_key;
    temp_key.remove_prefix(dockey_size);
    for (;;) {
      auto decode_result = VERIFY_RESULT(SubDocKey::DecodeSubkey(&temp_key));
      if (!decode_result) {
        break;
      }
      RETURN_NOT_OK(FindLastWriteTime(db_iter, key_slice, &max_overwrite_ht, &data.exp));
      key_slice = Slice(key_slice.data(), temp_key.data() - key_slice.data());
    }
  }

  // By this point key_bytes is the encoded representation of the DocKey and all the subkeys of
  // subdocument_key. Check for init-marker / tombstones at the top level, update max_overwrite_ht.
  doc_value = Value(PrimitiveValue(ValueType::kInvalid));
  RETURN_NOT_OK(FindLastWriteTime(db_iter, key_slice, &max_overwrite_ht, &data.exp, &doc_value));

  const ValueType value_type = doc_value.value_type();

  if (data.return_type_only) {
    *data.doc_found = value_type != ValueType::kInvalid &&
      !data.exp.ttl.IsNegative();
    // Check for expiration.
    if (*data.doc_found && max_overwrite_ht != DocHybridTime::kMin) {
      bool has_expired;
      CHECK_OK(HasExpiredTTL(data.exp.write_ht, data.exp.ttl,
                             db_iter->read_time().read, &has_expired));
      *data.doc_found = !has_expired;
    }
    if (*data.doc_found) {
      // Observe that this will have the right type but not necessarily the right value.
      *data.result = SubDocument(doc_value.primitive_value());
    }
    return Status::OK();
  }

  if (projection == nullptr) {
    *data.result = SubDocument(ValueType::kInvalid);
    int64 num_values_observed = 0;
    IntentAwareIteratorPrefixScope prefix_scope(key_slice, db_iter);
    RETURN_NOT_OK(BuildSubDocument(db_iter, data, max_overwrite_ht,
                                   &num_values_observed));
    *data.doc_found = data.result->value_type() != ValueType::kInvalid;
    if (*data.doc_found) {
      if (value_type == ValueType::kRedisSet) {
        RETURN_NOT_OK(data.result->ConvertToRedisSet());
      } else if (value_type == ValueType::kRedisTS) {
        RETURN_NOT_OK(data.result->ConvertToRedisTS());
      } else if (value_type == ValueType::kRedisSortedSet) {
        RETURN_NOT_OK(data.result->ConvertToRedisSortedSet());
      } else if (value_type == ValueType::kRedisList) {
        RETURN_NOT_OK(data.result->ConvertToRedisList());
      }
    }
    return Status::OK();
  }
  // Seed key_bytes with the subdocument key. For each subkey in the projection, build subdocument
  // and reuse key_bytes while appending the subkey.
  *data.result = SubDocument();
  KeyBytes key_bytes(data.subdocument_key);
  const size_t subdocument_key_size = key_bytes.size();
  for (const PrimitiveValue& subkey : *projection) {
    // Append subkey to subdocument key. Reserve extra kMaxBytesPerEncodedHybridTime + 1 bytes in
    // key_bytes to avoid the internal buffer from getting reallocated and moved by SeekForward()
    // appending the hybrid time, thereby invalidating the buffer pointer saved by prefix_scope.
    subkey.AppendToKey(&key_bytes);
    key_bytes.Reserve(key_bytes.size() + kMaxBytesPerEncodedHybridTime + 1);
    // This seek is to initialize the iterator for BuildSubDocument call.
    IntentAwareIteratorPrefixScope prefix_scope(key_bytes, db_iter);
    db_iter->SeekForward(&key_bytes);
    SubDocument descendant(ValueType::kInvalid);
    int64 num_values_observed = 0;
    RETURN_NOT_OK(BuildSubDocument(
        db_iter, data.Adjusted(key_bytes, &descendant), max_overwrite_ht,
        &num_values_observed));
    *data.doc_found = descendant.value_type() != ValueType::kInvalid;
    data.result->SetChild(subkey, std::move(descendant));

    // Restore subdocument key by truncating the appended subkey.
    key_bytes.Truncate(subdocument_key_size);
  }
  // Make sure the iterator is placed outside the whole document in the end.
  key_bytes.Truncate(dockey_size);
  key_bytes.AppendValueType(ValueType::kMaxByte);
  db_iter->SeekForward(&key_bytes);
  return Status::OK();
}

// Note: Do not use if also retrieving other value, as some work will be repeated.
// Assumes every value has a TTL, and the TTL is stored in the row with this key.
// Also observe that tombstone checking only works because we assume the key has
// no ancestors.
yb::Status GetTtl(const Slice& encoded_subdoc_key,
                  IntentAwareIterator* iter,
                  bool* doc_found,
                  Expiration* exp) {
  auto dockey_size =
    VERIFY_RESULT(DocKey::EncodedSize(encoded_subdoc_key, DocKeyPart::WHOLE_DOC_KEY));
  Slice key_slice(encoded_subdoc_key.data(), dockey_size);
  iter->Seek(key_slice);
  if (!iter->valid())
    return Status::OK();
  DocHybridTime doc_ht;
  auto key = VERIFY_RESULT(iter->FetchKey(&doc_ht));
  if ((*doc_found = (!key.compare(key_slice)))) {
    Value doc_value = Value(PrimitiveValue(ValueType::kInvalid));
    RETURN_NOT_OK(doc_value.Decode(iter->value()));
    if (doc_value.value_type() == ValueType::kTombstone) {
      *doc_found = false;
    } else {
      exp->ttl = doc_value.ttl();
      exp->write_ht = doc_ht.hybrid_time();
    }
  }
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Debug output
// ------------------------------------------------------------------------------------------------

namespace {

Result<std::string> DocDBKeyToDebugStr(Slice key_slice, StorageDbType db_type, KeyType* key_type) {
  *key_type = GetKeyType(key_slice, db_type);
  SubDocKey subdoc_key;
  switch (*key_type) {
    case KeyType::kIntentKey:
    {
      Slice intent_prefix;
      IntentType intent_type;
      DocHybridTime doc_ht;
      RETURN_NOT_OK_PREPEND(
          DecodeIntentKey(key_slice, &intent_prefix, &intent_type, &doc_ht),
          "Error: failed decoding RocksDB intent key " + FormatRocksDBSliceAsStr(key_slice));
      RETURN_NOT_OK(subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(intent_prefix));
      return subdoc_key.ToString() + " " + ToString(intent_type) + " " + doc_ht.ToString();
    }
    case KeyType::kReverseTxnKey:
    {
      RETURN_NOT_OK(key_slice.consume_byte(ValueTypeAsChar::kTransactionId));
      auto transaction_id = VERIFY_RESULT(DecodeTransactionId(&key_slice));
      if (key_slice.empty() || key_slice.size() > kMaxBytesPerEncodedHybridTime + 1) {
        return STATUS_FORMAT(
            Corruption,
            "Invalid doc hybrid time in reverse intent record, transaction id: $0, suffix: $1",
            transaction_id, key_slice.ToDebugHexString());
      }
      size_t doc_ht_buffer[kMaxWordsPerEncodedHybridTimeWithValueType];
      memcpy(doc_ht_buffer, key_slice.data(), key_slice.size());
      for (size_t i = 0; i != kMaxWordsPerEncodedHybridTimeWithValueType; ++i) {
        doc_ht_buffer[i] = ~doc_ht_buffer[i];
      }
      key_slice = Slice(pointer_cast<char*>(doc_ht_buffer), key_slice.size());

      if (static_cast<ValueType>(key_slice[0]) != ValueType::kHybridTime) {
        return STATUS_FORMAT(
            Corruption,
            "Invalid prefix of doc hybrid time in reverse intent record, transaction id: $0, "
                "decoded suffix: $1",
            transaction_id, key_slice.ToDebugHexString());
      }
      key_slice.consume_byte();
      DocHybridTime doc_ht;
      RETURN_NOT_OK(doc_ht.DecodeFrom(&key_slice));
      return Format("TXN REV $0 $1", transaction_id, doc_ht);
    }
    case KeyType::kTransactionMetadata:
    {
      RETURN_NOT_OK(key_slice.consume_byte(ValueTypeAsChar::kTransactionId));
      auto transaction_id = DecodeTransactionId(&key_slice);
      RETURN_NOT_OK(transaction_id);
      return Format("TXN META $0", *transaction_id);
    }
    case KeyType::kEmpty: FALLTHROUGH_INTENDED;
    case KeyType::kValueKey:
      RETURN_NOT_OK_PREPEND(
          subdoc_key.FullyDecodeFrom(key_slice),
          "Error: failed decoding RocksDB intent key " + FormatRocksDBSliceAsStr(key_slice));
      return subdoc_key.ToString();
  }
  return STATUS_SUBSTITUTE(Corruption, "Corrupted KeyType: $0", yb::ToString(*key_type));
}

Result<std::string> DocDBValueToDebugStr(Slice value_slice, const KeyType& key_type) {
  std::string prefix;
  if (key_type == KeyType::kIntentKey) {
    auto txn_id_res = VERIFY_RESULT(DecodeTransactionIdFromIntentValue(&value_slice));
    prefix = Format("TransactionId($0) ", txn_id_res);
    if (!value_slice.empty()) {
          RETURN_NOT_OK(value_slice.consume_byte(ValueTypeAsChar::kWriteId));
      if (value_slice.size() < sizeof(IntraTxnWriteId)) {
        return STATUS_FORMAT(Corruption, "Not enought bytes for write id: $0", value_slice.size());
      }
      auto write_id = BigEndian::Load32(value_slice.data());
      value_slice.remove_prefix(sizeof(write_id));
      prefix += Format("WriteId($0) ", write_id);
    }
  }

  // Empty values are allowed for weak intents.
  if (!value_slice.empty() || key_type != KeyType::kIntentKey) {
    Value v;
    RETURN_NOT_OK_PREPEND(
        v.Decode(value_slice),
        Substitute("Error: failed to decode value $0", prefix));
    return prefix + v.ToString();
  } else {
    return prefix + "none";
  }
}

Result<std::string> DocDBValueToDebugStr(
    KeyType key_type, const std::string& key_str, Slice value) {
  switch (key_type) {
    case KeyType::kTransactionMetadata: {
      TransactionMetadataPB metadata_pb;
      if (!metadata_pb.ParseFromArray(value.cdata(), value.size())) {
        return STATUS_FORMAT(Corruption, "Bad metadata: $0", value.ToDebugHexString());
      }
      auto metadata = TransactionMetadata::FromPB(metadata_pb);
      RETURN_NOT_OK(metadata);
      return ToString(*metadata);
    }
    case KeyType::kReverseTxnKey: {
      KeyType ignore_key_type;
      return DocDBKeyToDebugStr(value, StorageDbType::kIntents, &ignore_key_type);
    }
    case KeyType::kEmpty: FALLTHROUGH_INTENDED;
    case KeyType::kIntentKey: FALLTHROUGH_INTENDED;
    case KeyType::kValueKey:
      return DocDBValueToDebugStr(value, key_type);
  }
  FATAL_INVALID_ENUM_VALUE(KeyType, key_type);
}

void ProcessDumpEntry(Slice key, Slice value, IncludeBinary include_binary, StorageDbType db_type,
                      std::ostream* out) {
  KeyType key_type;
  Result<std::string> key_str = DocDBKeyToDebugStr(key, db_type, &key_type);
  if (!key_str.ok()) {
    *out << key_str.status() << endl;
    return;
  }
  Result<std::string> value_str = DocDBValueToDebugStr(key_type, *key_str, value);
  if (!value_str.ok()) {
    *out << value_str.status().CloneAndAppend(Substitute(". Key: $0", *key_str)) << endl;
    return;
  }
  *out << *key_str << " -> " << *value_str << endl;
  if (include_binary) {
    *out << FormatRocksDBSliceAsStr(key) << " -> "
         << FormatRocksDBSliceAsStr(value) << endl << endl;
  }
}

std::string EntryToString(const rocksdb::Iterator& iterator, StorageDbType db_type) {
  std::ostringstream out;
  ProcessDumpEntry(iterator.key(), iterator.value(), IncludeBinary::kFalse, db_type, &out);
  return out.str();
}

}  // namespace

void DocDBDebugDump(rocksdb::DB* rocksdb, ostream& out, StorageDbType db_type,
                    IncludeBinary include_binary) {
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  iter->SeekToFirst();

  while (iter->Valid()) {
    ProcessDumpEntry(iter->key(), iter->value(), include_binary, db_type, &out);
    iter->Next();
  }
}

std::string DocDBDebugDumpToStr(DocDB docdb, IncludeBinary include_binary) {
  stringstream ss;
  DocDBDebugDump(docdb.regular, ss, StorageDbType::kRegular, include_binary);
  if (docdb.intents) {
    DocDBDebugDump(docdb.intents, ss, StorageDbType::kIntents, include_binary);
  }
  return ss.str();
}

std::string DocDBDebugDumpToStr(rocksdb::DB* rocksdb, StorageDbType db_type,
                                IncludeBinary include_binary) {
  stringstream ss;
  DocDBDebugDump(rocksdb, ss, db_type, include_binary);
  return ss.str();
}

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, KeyBytes* out) {
  out->AppendValueType(ValueType::kTransactionId);
  out->AppendRawBytes(Slice(transaction_id.data, transaction_id.size()));
}

DocHybridTimeBuffer::DocHybridTimeBuffer() {
  buffer_[0] = ValueTypeAsChar::kHybridTime;
}

Slice DocHybridTimeBuffer::EncodeWithValueType(const DocHybridTime& doc_ht) {
  auto end = doc_ht.EncodedInDocDbFormat(buffer_.data() + 1);
  return Slice(buffer_.data(), end);
}

CHECKED_STATUS IntentToWriteRequest(
    const Slice& transaction_id_slice,
    HybridTime commit_ht,
    rocksdb::Iterator* reverse_index_iter,
    rocksdb::Iterator* intent_iter,
    rocksdb::WriteBatch* regular_batch,
    IntraTxnWriteId* write_id) {
  DocHybridTimeBuffer doc_ht_buffer;
  intent_iter->Seek(reverse_index_iter->value());
  if (!intent_iter->Valid() || intent_iter->key() != reverse_index_iter->value()) {
    LOG(DFATAL) << "Unable to find intent: " << reverse_index_iter->value().ToDebugString()
                << " for " << reverse_index_iter->key().ToDebugString();
    return Status::OK();
  }
  auto intent = VERIFY_RESULT(ParseIntentKey(intent_iter->key(), transaction_id_slice));

  if (IsStrongIntent(intent.type)) {
    IntraTxnWriteId stored_write_id;
    Slice intent_value;
    RETURN_NOT_OK(DecodeIntentValue(
        intent_iter->value(), transaction_id_slice, &stored_write_id, &intent_value));

    // Write id should match to one that were calculated during append of intents.
    // Doing it just for sanity check.
    DCHECK_EQ(stored_write_id, *write_id)
      << "Value: " << intent_iter->value().ToDebugHexString();

    // After strip of prefix and suffix intent_key contains just SubDocKey w/o a hybrid time.
    // Time will be added when writing batch to RocksDB.
    std::array<Slice, 2> key_parts = {{
        intent.doc_path,
        doc_ht_buffer.EncodeWithValueType(commit_ht, *write_id),
    }};
    std::array<Slice, 2> value_parts = {{
        intent.doc_ht,
        intent_value,
    }};
    regular_batch->Put(key_parts, value_parts);
    ++*write_id;
  }

  return Status::OK();
}

Status PrepareApplyIntentsBatch(
    const TransactionId &transaction_id, HybridTime commit_ht,
    rocksdb::WriteBatch *regular_batch,
    rocksdb::DB *intents_db, rocksdb::WriteBatch *intents_batch) {
  Slice reverse_index_upperbound;
  auto reverse_index_iter = CreateRocksDBIterator(
      intents_db, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none, rocksdb::kDefaultQueryId,
      nullptr /* read_filter */, &reverse_index_upperbound);

  std::unique_ptr<rocksdb::Iterator> intent_iter;
  // If we don't have regular_batch, it means that we just removing intents.
  // We don't need intent iterator, since reverse index iterator is enough in this case.
  if (regular_batch) {
    intent_iter = CreateRocksDBIterator(
        intents_db, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none, rocksdb::kDefaultQueryId);
  }

  KeyBytes txn_reverse_index_prefix;
  Slice transaction_id_slice(transaction_id.data, TransactionId::static_size());
  AppendTransactionKeyPrefix(transaction_id, &txn_reverse_index_prefix);

  KeyBytes txn_reverse_index_upperbound = txn_reverse_index_prefix;
  txn_reverse_index_upperbound.AppendValueType(ValueType::kMaxByte);
  reverse_index_upperbound = txn_reverse_index_upperbound.AsSlice();

  reverse_index_iter->Seek(txn_reverse_index_prefix.data());

  DocHybridTimeBuffer doc_ht_buffer;

  IntraTxnWriteId write_id = 0;
  while (reverse_index_iter->Valid()) {
    rocksdb::Slice key_slice(reverse_index_iter->key());

    if (!key_slice.starts_with(txn_reverse_index_prefix.data())) {
      break;
    }

    VLOG(4) << "Apply reverse index record: "
            << EntryToString(*reverse_index_iter, StorageDbType::kIntents);

    // If the key ends at the transaction id then it is transaction metadata (status tablet,
    // isolation level etc.).
    if (key_slice.size() > txn_reverse_index_prefix.size()) {
      // Value of reverse index is a key of original intent record, so seek it and check match.
      if (regular_batch) {
        RETURN_NOT_OK(IntentToWriteRequest(
            transaction_id_slice, commit_ht, reverse_index_iter.get(), intent_iter.get(),
            regular_batch, &write_id));
      }

      intents_batch->Delete(reverse_index_iter->value());
    }

    intents_batch->Delete(reverse_index_iter->key());

    reverse_index_iter->Next();
  }

  return Status::OK();
}

}  // namespace docdb
}  // namespace yb

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

#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/internal_doc_iterator.h"
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

void AddIntent(
    const TransactionId& transaction_id,
    Slice key,
    Slice value,
    rocksdb::WriteBatch* rocksdb_write_batch) {
  // TODO(dtxn) Sergei: Would be nice to reuse buffer allocated by reverse_key for other keys from
  // the same batch. I propose to implement PrepareTransactionWriteBatch using class.
  // And make this method of that class, also that would help to decrease closure size passed to
  // EnumerateIntents.
  KeyBytes reverse_key;
  AppendTransactionKeyPrefix(transaction_id, &reverse_key);
  int size = 0;
  CHECK_OK(DocHybridTime::CheckAndGetEncodedSize(key, &size));
  reverse_key.AppendRawBytes(key.cend() - size, size);

  rocksdb_write_batch->Put(key, value);
  rocksdb_write_batch->Put(reverse_key.data(), key);
}

void AppendTransactionId(const TransactionId& id, std::string* value) {
  value->push_back(static_cast<char>(ValueType::kTransactionId));
  value->append(pointer_cast<const char*>(id.data), id.size());
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

const SubDocKeyBound& SubDocKeyBound::Empty() {
  static SubDocKeyBound result;
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
                                const ReadHybridTime& read_time,
                                rocksdb::DB *rocksdb,
                                KeyValueWriteBatchPB* write_batch,
                                InitMarkerBehavior init_marker_behavior,
                                std::atomic<int64_t>* monotonic_counter,
                                HybridTime* restart_read_ht) {
  DCHECK_ONLY_NOTNULL(restart_read_ht);
  DocWriteBatch doc_write_batch(rocksdb, init_marker_behavior, monotonic_counter);
  DocOperationApplyData data = {&doc_write_batch, read_time, restart_read_ht};
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    RETURN_NOT_OK(doc_op->Apply(data));
  }
  doc_write_batch.MoveToWriteBatchPB(write_batch);
  return Status::OK();
}

void PrepareNonTransactionWriteBatch(
    const KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::WriteBatch* rocksdb_write_batch) {
  std::string patched_key;
  for (int write_id = 0; write_id < put_batch.kv_pairs_size(); ++write_id) {
    const auto& kv_pair = put_batch.kv_pairs(write_id);
    CHECK(kv_pair.has_key());
    CHECK(kv_pair.has_value());

#ifndef NDEBUG
    // Debug-only: ensure all keys we get in Raft replication can be decoded.
    {
      docdb::SubDocKey subdoc_key;
      Status s = subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(kv_pair.key());
      CHECK(s.ok())
          << "Failed decoding key: " << s.ToString() << "; "
          << "Problematic key: " << docdb::BestEffortDocDBKeyToStr(KeyBytes(kv_pair.key())) << "\n"
          << "value: " << util::FormatBytesAsStr(kv_pair.value()) << "\n"
          << "put_batch:\n" << put_batch.DebugString();
    }
#endif

    patched_key.reserve(kv_pair.key().size() + kMaxBytesPerEncodedHybridTime);
    patched_key = kv_pair.key();

    // We replicate encoded SubDocKeys without a HybridTime at the end, and only append it here.
    // The reason for this is that the HybridTime timestamp is only picked at the time of
    // appending  an entry to the tablet's Raft log. Also this is a good way to save network
    // bandwidth.
    //
    // "Write id" is the final component of our HybridTime encoding (or, to be more precise,
    // DocHybridTime encoding) that helps disambiguate between different updates to the
    // same key (row/column) within a transaction. We set it based on the position of the write
    // operation in its write batch.
    patched_key.push_back(static_cast<char>(ValueType::kHybridTime));  // Don't forget ValueType!
    DocHybridTime(hybrid_time, write_id).AppendEncodedInDocDbFormat(&patched_key);

    rocksdb_write_batch->Put(patched_key, kv_pair.value());
  }
}

CHECKED_STATUS EnumerateIntents(
    const google::protobuf::RepeatedPtrField<yb::docdb::KeyValuePairPB> &kv_pairs,
    boost::function<Status(IntentKind, Slice, KeyBytes*)> functor) {
  KeyBytes encoded_key;

  for (int index = 0; index < kv_pairs.size(); ++index) {
    const auto &kv_pair = kv_pairs.Get(index);
    CHECK(kv_pair.has_key());
    CHECK(kv_pair.has_value());
    Slice key = kv_pair.key();
    auto key_size = DocKey::EncodedSize(key, docdb::DocKeyPart::WHOLE_DOC_KEY);
    CHECK_OK(key_size);

    encoded_key.Clear();
    encoded_key.AppendValueType(ValueType::kIntentPrefix);
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
    IsolationLevel isolation_level) {
  auto intent_types = GetWriteIntentsForIsolationLevel(isolation_level);

  // TODO(dtxn) weak & strong intent in one batch.
  // TODO(dtxn) extract part of code knowning about intents structure to lower level.
  std::unordered_set<std::string> weak_intents;

  std::string value;

  IntraTxnWriteId write_id = 0;

  // TODO(dtxn) This lambda is too long. This function should be implemented using utility class.
  CHECK_OK(EnumerateIntents(put_batch.kv_pairs(),
      [&](IntentKind intent_kind, Slice value_slice, KeyBytes* key) {
        if (intent_kind == IntentKind::kWeak) {
          weak_intents.insert(key->data());
          return Status::OK();
        }

        AppendIntentKeySuffix(
            intent_types.strong,
            DocHybridTime(hybrid_time, write_id++),
            key);

        value.clear();
        value.reserve(1 + transaction_id.size() + value_slice.size());
        AppendTransactionId(transaction_id, &value);
        value.append(value_slice.cdata(), value_slice.size());
        AddIntent(transaction_id, key->data(), value, rocksdb_write_batch);

        return Status::OK();
      }));

  KeyBytes encoded_key;
  for (const auto& intent : weak_intents) {
    encoded_key.Clear();
    encoded_key.AppendRawBytes(intent);
    AppendIntentKeySuffix(
        intent_types.weak,
        DocHybridTime(hybrid_time, write_id++),
        &encoded_key);
    value.clear();
    value.reserve(1 + transaction_id.size());
    AppendTransactionId(transaction_id, &value);
    AddIntent(transaction_id, encoded_key.data(), value, rocksdb_write_batch);
  }
}

// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

namespace {

void SeekToLowerBound(const SubDocKeyBound& lower_bound, IntentAwareIterator* iter) {
  if (lower_bound.is_exclusive()) {
    iter->SeekPastSubKey(lower_bound);
  } else {
    iter->SeekForwardIgnoreHt(lower_bound);
  }
}

// This works similar to the ScanSubDocument function, but doesn't assume that object init_markers
// are present. If no init marker is present, or if a tombstone is found at some level,
// it still looks for subkeys inside it if they have larger timestamps.
// TODO(akashnil): ENG-1152: If object init markers were required, this read path may be optimized.
// We look at all rocksdb keys with prefix = subdocument_key, and construct a subdocument out of
// them, between the timestamp range high_ts and low_ts.
//
// The iterator is expected to be placed at the smallest key that is subdocument_key or later, and
// after the function returns, the iterator should be placed just completely outside the
// subdocument_key prefix. Although if high_subkey is specified, the iterator is only guaranteed
// to be positioned after the high_subkey and not necessarily outside the subdocument_key prefix.
CHECKED_STATUS BuildSubDocument(
    IntentAwareIterator* iter,
    const GetSubDocumentData& data,
    DocHybridTime low_ts) {
  DCHECK(!data.subdocument_key->has_hybrid_time());
  DOCDB_DEBUG_LOG("subdocument_key=$0, high_ts=$1, low_ts=$2, table_ttl=$3",
                  data.subdocument_key->ToString(),
                  iter->read_time().ToString(),
                  low_ts.ToString(),
                  data.table_ttl.ToString());
  const KeyBytes encoded_key = data.subdocument_key->Encode();
  while (iter->valid()) {
    auto iter_key = iter->FetchKey();
    RETURN_NOT_OK(iter_key);
    VLOG(4) << "iter: " << iter_key->ToDebugString()
            << ", key: " << encoded_key.ToString();
    DCHECK(iter_key->starts_with(encoded_key))
        << "iter: " << iter_key->ToDebugString()
        << ", key: " << encoded_key.ToString();

    SubDocKey found_key;
    RETURN_NOT_OK(found_key.FullyDecodeFrom(*iter_key));

    rocksdb::Slice value = iter->value();

    DCHECK_GE(iter->read_time().local_limit, found_key.hybrid_time())
        << "Found key: " << found_key.ToString();

    if (low_ts > found_key.doc_hybrid_time()) {
      DOCDB_DEBUG_LOG("SeekPastSubKey: $0", found_key.ToString());
      iter->SeekPastSubKey(found_key);
      continue;
    }

    Value doc_value;
    RETURN_NOT_OK(doc_value.Decode(value));

    bool only_lacks_ht = false;
    RETURN_NOT_OK(encoded_key.OnlyLacksHybridTimeFrom(*iter_key, &only_lacks_ht));
    if (only_lacks_ht) {
      const MonoDelta ttl = ComputeTTL(doc_value.ttl(), data.table_ttl);

      DocHybridTime write_time = found_key.doc_hybrid_time();

      bool has_expired = false;
      if (!ttl.Equals(Value::kMaxTtl)) {
        const HybridTime expiry =
            server::HybridClock::AddPhysicalTimeToHybridTime(found_key.hybrid_time(), ttl);
        if (iter->read_time().read.CompareTo(expiry) > 0) {
          has_expired = true;
          if (low_ts.hybrid_time() > expiry) {
            // We should have expiry > hybrid time from key > low_ts.
            return STATUS_SUBSTITUTE(Corruption,
                "Unexpected expiry time $0 found, should be higher than $1",
                expiry.ToDebugString(), low_ts.ToString());
          }

          // Treat the value as a tombstone written at expiry time. Note that this doesn't apply
          // to init markers for collections since even if the init marker for the collection has
          // expired, individual elements in the collection might still be valid.
          if (!IsCollectionType(doc_value.value_type())) {
            doc_value = Value(PrimitiveValue::kTombstone);
            // Use a write id that could never be used by a real operation within a single-shard
            // txn, so that we don't split that operation into multiple parts.
            write_time = DocHybridTime(expiry, kMaxWriteId);
          }
        }
      }

      // We have found some key that matches our entire subdocument_key, i.e. we didn't skip ahead
      // to a lower level key (with optional object init markers).
      if (IsCollectionType(doc_value.value_type()) ||
          doc_value.value_type() == ValueType::kTombstone) {
        if (low_ts < write_time) {
          low_ts = write_time;
        }
        if (IsCollectionType(doc_value.value_type()) && !has_expired) {
          *data.result = SubDocument(doc_value.value_type());
        }

        // If the low subkey cannot include the found key, we want to skip to the low subkey,
        // but if it can, we want to seek to the next key. This prevents an infinite loop
        // where the iterator keeps seeking to itself if the found key matches the low subkey.
        if (IsObjectType(doc_value.value_type()) &&
        data.low_subkey->IsValid() &&
        !data.low_subkey->CanInclude(found_key)) {
          // Try to seek to the low_subkey for efficiency.
          SeekToLowerBound(*data.low_subkey, iter);
        } else {
          DOCDB_DEBUG_LOG("SeekPastSubKey: $0", found_key.ToString());
          iter->SeekPastSubKey(found_key);
        }
        continue;
      } else {
        if (!IsPrimitiveValueType(doc_value.value_type())) {
          return STATUS_FORMAT(Corruption,
              "Expected primitive value type, got $0", doc_value.value_type());
        }

        DCHECK_GE(iter->read_time().local_limit, write_time.hybrid_time());
        if (ttl.Equals(Value::kMaxTtl)) {
          doc_value.mutable_primitive_value()->SetTtl(-1);
        } else {
          int64_t time_since_write_seconds = (
              server::HybridClock::GetPhysicalValueMicros(iter->read_time().read) -
              server::HybridClock::GetPhysicalValueMicros(write_time.hybrid_time())) /
              MonoTime::kMicrosecondsPerSecond;
          int64_t ttl_seconds = std::max(static_cast<int64_t>(0),
              ttl.ToMilliseconds() / MonoTime::kMillisecondsPerSecond - time_since_write_seconds);
          doc_value.mutable_primitive_value()->SetTtl(ttl_seconds);
        }

        // Choose the user supplied timestamp if present.
        const UserTimeMicros user_timestamp = doc_value.user_timestamp();
        doc_value.mutable_primitive_value()->SetWritetime(
            user_timestamp == Value::kInvalidUserTimestamp
                ? write_time.hybrid_time().GetPhysicalValueMicros()
                : doc_value.user_timestamp());
        *data.result = SubDocument(doc_value.primitive_value());
        DOCDB_DEBUG_LOG("SeekForward: $0.AdvanceOutOfSubDoc() = $1", found_key.ToString(),
            found_key.AdvanceOutOfSubDoc().ToString());
        iter->SeekOutOfSubDoc(found_key);
        return Status::OK();
      }
    }

    SubDocument descendant = SubDocument(PrimitiveValue(ValueType::kInvalidValueType));
    // TODO: what if found_key is the same as before? We'll get into an infinite recursion then.
    found_key.remove_hybrid_time();

    {
      auto encoded_found_key = found_key.Encode();
      IntentAwareIteratorPrefixScope prefix_scope(encoded_found_key, iter);
      RETURN_NOT_OK(BuildSubDocument(iter, data.Adjusted(&found_key, &descendant), low_ts));
    }
    if (descendant.value_type() == ValueType::kInvalidValueType) {
      // The document was not found in this level (maybe a tombstone was encountered).
      continue;
    }

    // For the purposes of comparison, we strip the found key until it matches the length of both
    // the low and high subkeys for their respective calculations.
    SubDocKey found_key_prefix_low = found_key;
    SubDocKey found_key_prefix_high = found_key;
    found_key_prefix_low.KeepPrefix(data.low_subkey->num_subkeys());
    found_key_prefix_high.KeepPrefix(data.high_subkey->num_subkeys());

    if (data.low_subkey->IsValid() && !data.low_subkey->CanInclude(found_key_prefix_low)) {
      // The value provided is lower than what we are looking for, seek to the lower bound.
      SeekToLowerBound(*data.low_subkey, iter);
      continue;
    }

    if (data.high_subkey->IsValid() && !data.high_subkey->CanInclude(found_key_prefix_high)) {
      // We have encountered a subkey higher than our constraints, we should stop here.
      return Status::OK();
    }

    if (!IsObjectType(data.result->value_type())) {
      *data.result = SubDocument();
    }

    SubDocument* current = data.result;

    for (int i = data.subdocument_key->num_subkeys(); i < found_key.num_subkeys() - 1; i++) {
      current = current->GetOrAddChild(found_key.subkeys()[i]).first;
    }
    current->SetChild(found_key.subkeys().back(), SubDocument(descendant));
  }

  return Status::OK();
}

}  // namespace

yb::Status GetSubDocument(
    rocksdb::DB *db,
    const GetSubDocumentData& data,
    const rocksdb::QueryId query_id,
    const TransactionOperationContextOpt& txn_op_context,
    const ReadHybridTime& read_time) {
  const auto doc_key_encoded = data.subdocument_key->doc_key().Encode();
  auto iter = CreateIntentAwareIterator(
      db, BloomFilterMode::USE_BLOOM_FILTER, doc_key_encoded.AsSlice(), query_id, txn_op_context,
      read_time);
  return GetSubDocument(iter.get(), data, nullptr /* projection */, false /* is_iter_valid */);
}

yb::Status GetSubDocument(
    IntentAwareIterator *db_iter,
    const GetSubDocumentData& data,
    const vector<PrimitiveValue>* projection,
    const bool is_iter_valid) {
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
  DOCDB_DEBUG_LOG("GetSubDocument for key $0 @ $1", data.subdocument_key->ToString(),
                  db_iter->read_time().ToString());
  DocHybridTime max_deleted_ts(DocHybridTime::kMin);

  VLOG(4) << "GetSubDocument(" << data.subdocument_key->ToString() << ")";

  SubDocKey found_subdoc_key;

  DCHECK(!data.subdocument_key->has_hybrid_time());
  KeyBytes key_bytes = data.subdocument_key->doc_key().Encode();

  auto doc_key_bytes = key_bytes;
  IntentAwareIteratorPrefixScope prefix_scope(doc_key_bytes, db_iter);

  if (is_iter_valid) {
    db_iter->SeekForwardWithoutHt(key_bytes);
  } else {
    db_iter->SeekWithoutHt(key_bytes);
  }

  // Check ancestors for init markers and tombstones, update max_deleted_ts with them.
  for (const PrimitiveValue& subkey : data.subdocument_key->subkeys()) {
    RETURN_NOT_OK(db_iter->FindLastWriteTime(key_bytes, &max_deleted_ts, nullptr));
    subkey.AppendToKey(&key_bytes);
  }

  // By this point key_bytes is the encoded representation of the DocKey and all the subkeys of
  // subdocument_key.

  // Check for init-marker / tombstones at the top level, update max_deleted_ts.
  Value doc_value = Value(PrimitiveValue(ValueType::kInvalidValueType));
  RETURN_NOT_OK(db_iter->FindLastWriteTime(key_bytes, &max_deleted_ts, &doc_value));

  if (data.return_type_only) {
    *data.doc_found = doc_value.value_type() != ValueType::kInvalidValueType;
    *data.result = SubDocument(doc_value.primitive_value());
    return Status::OK();
  }

  if (projection == nullptr) {
    *data.result = SubDocument(ValueType::kInvalidValueType);
    IntentAwareIteratorPrefixScope prefix_scope(key_bytes, db_iter);
    RETURN_NOT_OK(BuildSubDocument(db_iter, data, max_deleted_ts));
    *data.doc_found = data.result->value_type() != ValueType::kInvalidValueType;
    if (*data.doc_found && doc_value.value_type() == ValueType::kRedisSet) {
      RETURN_NOT_OK(data.result->ConvertToRedisSet());
    } else if (*data.doc_found && doc_value.value_type() == ValueType::kRedisTS) {
      RETURN_NOT_OK(data.result->ConvertToRedisTS());
    } else if (*data.doc_found && doc_value.value_type() == ValueType::kRedisSortedSet) {
      RETURN_NOT_OK(data.result->ConvertToRedisSortedSet());
    }
    // TODO: Also could handle lists here.

    return Status::OK();
  }
  // For each subkey in the projection, build subdocument.
  *data.result = SubDocument();
  for (const PrimitiveValue& subkey : *projection) {
    SubDocKey projection_subdockey = *data.subdocument_key;
    projection_subdockey.AppendSubKeysAndMaybeHybridTime(subkey);
    // This seek is to initialize the iterator for BuildSubDocument call.
    auto encoded_projection_subdockey =
        projection_subdockey.Encode(/* include_hybrid_time */ false);
    IntentAwareIteratorPrefixScope prefix_scope(encoded_projection_subdockey, db_iter);
    db_iter->SeekForwardWithoutHt(encoded_projection_subdockey);

    SubDocument descendant(ValueType::kInvalidValueType);
    RETURN_NOT_OK(BuildSubDocument(
        db_iter, data.Adjusted(&projection_subdockey, &descendant), max_deleted_ts));
    if (descendant.value_type() != ValueType::kInvalidValueType) {
      *data.doc_found = true;
    }
    data.result->SetChild(subkey, std::move(descendant));
  }
  // Make sure the iterator is placed outside the whole document in the end.
  db_iter->SeekForwardWithoutHt(data.subdocument_key->AdvanceOutOfSubDoc());
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Debug output
// ------------------------------------------------------------------------------------------------

namespace {

Result<std::string> DocDBKeyToDebugStr(Slice key_slice, KeyType* key_type) {
  *key_type = GetKeyType(key_slice);
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
      intent_prefix.consume_byte();
      RETURN_NOT_OK(subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(intent_prefix));
      return subdoc_key.ToString() + " " + yb::docdb::ToString(intent_type) + " " +
          doc_ht.ToString();
    }
    case KeyType::kReverseTxnKey:
    {
      key_slice.remove_prefix(2); // kIntentPrefix + kTransactionId
      auto transaction_id = DecodeTransactionId(&key_slice);
      RETURN_NOT_OK(transaction_id);
      return Format("TXN REV $0", *transaction_id);
    }
    case KeyType::kTransactionMetadata:
    {
      key_slice.remove_prefix(2); // kIntentPrefix + kTransactionId
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
    Result<TransactionId> txn_id_res = DecodeTransactionIdFromIntentValue(&value_slice);
    RETURN_NOT_OK_PREPEND(txn_id_res, "Error: failed to decode transaction ID for intent");
    prefix = Substitute("TransactionId($0) ", yb::ToString(*txn_id_res));
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
      return DocDBKeyToDebugStr(value, &ignore_key_type);
    }
    case KeyType::kEmpty: FALLTHROUGH_INTENDED;
    case KeyType::kIntentKey: FALLTHROUGH_INTENDED;
    case KeyType::kValueKey:
      return DocDBValueToDebugStr(value, key_type);
  }
  FATAL_INVALID_ENUM_VALUE(KeyType, key_type);
}

void ProcessDumpEntry(Slice key, Slice value, IncludeBinary include_binary, std::ostream* out) {
  KeyType key_type;
  Result<std::string> key_str = DocDBKeyToDebugStr(key, &key_type);
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

}  // namespace

void DocDBDebugDump(rocksdb::DB* rocksdb, ostream& out, IncludeBinary include_binary) {
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  iter->SeekToFirst();

  while (iter->Valid()) {
    ProcessDumpEntry(iter->key(), iter->value(), include_binary, &out);
    iter->Next();
  }
}

std::string DocDBDebugDumpToStr(rocksdb::DB* rocksdb, IncludeBinary include_binary) {
  stringstream ss;
  DocDBDebugDump(rocksdb, ss, include_binary);
  return ss.str();
}

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, docdb::KeyBytes* out) {
  out->AppendValueType(docdb::ValueType::kIntentPrefix);
  out->AppendValueType(docdb::ValueType::kTransactionId);
  out->AppendRawBytes(Slice(transaction_id.data, transaction_id.size()));
}

}  // namespace docdb
}  // namespace yb

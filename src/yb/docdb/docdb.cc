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
// This a zero-terminated string for safety, even though we only intend to use one byte.
const char kObjectValueType[] = { static_cast<char>(ValueType::kObject), 0 };

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

void ApplyIntent(LockBatch *keys_locked, const string& lock_string, const IntentType intent) {
  auto itr = keys_locked->find(lock_string);
  if (itr == keys_locked->end()) {
    keys_locked->emplace(lock_string, intent);
  } else {
    itr->second = SharedLockManager::CombineIntents(itr->second, intent);
  }
}

}  // namespace

IntentTypePair WriteIntentsForIsolationLevel(const IsolationLevel level) {
  switch (level) {
    case IsolationLevel::SNAPSHOT_ISOLATION:
      return { IntentType::kStrongSnapshotWrite, IntentType::kWeakSnapshotWrite };
    case IsolationLevel::SERIALIZABLE_ISOLATION:
      return { IntentType::kStrongSerializableWrite, IntentType::kWeakSerializableWrite };
    case IsolationLevel::NON_TRANSACTIONAL:
      FATAL_INVALID_ENUM_VALUE(IsolationLevel, level);
  }
  FATAL_INVALID_ENUM_VALUE(IsolationLevel, level);
}

void PrepareDocWriteOperation(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                              SharedLockManager *lock_manager,
                              LockBatch *keys_locked,
                              bool *need_read_snapshot,
                              const scoped_refptr<Histogram>& write_lock_latency) {
  *need_read_snapshot = false;
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    list<DocPath> doc_paths;
    IsolationLevel level;
    doc_op->GetDocPathsToLock(&doc_paths, &level);
    const IntentTypePair intent_types = WriteIntentsForIsolationLevel(level);

    for (const auto& doc_path : doc_paths) {
      KeyBytes current_prefix = doc_path.encoded_doc_key();
      for (int i = 0; i < doc_path.num_subkeys(); i++) {
        ApplyIntent(keys_locked, current_prefix.AsStringRef(), intent_types.weak);
        doc_path.subkey(i).AppendToKey(&current_prefix);
      }
      ApplyIntent(keys_locked, current_prefix.AsStringRef(), intent_types.strong);
    }
    if (doc_op->RequireReadSnapshot()) {
      *need_read_snapshot = true;
    }
  }
  const MonoTime start_time = (write_lock_latency != nullptr) ? MonoTime::FineNow() : MonoTime();
  lock_manager->Lock(*keys_locked);
  if (write_lock_latency != nullptr) {
    const MonoDelta elapsed_time = MonoTime::Now(MonoTime::FINE).GetDeltaSince(start_time);
    write_lock_latency->Increment(elapsed_time.ToMicroseconds());
  }
}

Status ApplyDocWriteOperation(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                              const HybridTime& hybrid_time,
                              rocksdb::DB *rocksdb,
                              KeyValueWriteBatchPB* write_batch,
                              std::atomic<int64_t>* monotonic_counter) {
  DocWriteBatch doc_write_batch(rocksdb, monotonic_counter);
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    RETURN_NOT_OK(doc_op->Apply(&doc_write_batch, rocksdb, hybrid_time));
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
      Status s = subdoc_key.FullyDecodeFromKeyWithoutHybridTime(kv_pair.key());
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
// DocWriteBatch
// ------------------------------------------------------------------------------------------------

DocWriteBatch::DocWriteBatch(rocksdb::DB* rocksdb, std::atomic<int64_t>* monotonic_counter)
    : rocksdb_(rocksdb),
      monotonic_counter_(monotonic_counter),
      num_rocksdb_seeks_(0) {
}

CHECKED_STATUS DocWriteBatch::SetPrimitiveInternal(
    const DocPath& doc_path,
    const Value& value,
    InternalDocIterator *doc_iter,
    const bool is_deletion,
    const int num_subkeys,
    InitMarkerBehavior use_init_marker) {

  // The write_id is always incremented by one for each new element of the write batch.
  if (put_batch_.size() > numeric_limits<IntraTxnWriteId>::max()) {
    return STATUS_SUBSTITUTE(
        NotSupported,
        "Trying to add more than $0 key/value pairs in the same single-shard txn.",
        numeric_limits<IntraTxnWriteId>::max());
  }

  const auto write_id = static_cast<IntraTxnWriteId>(put_batch_.size());
  const DocHybridTime hybrid_time =
      DocHybridTime(HybridTime::kMax, write_id);

  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    // We don't need to check if intermediate documents already exist if init markers are optional,
    // or if we already know they exist (either from previous reads or our own writes in the same
    // single-shard txn.)
    if (use_init_marker == InitMarkerBehavior::OPTIONAL || doc_iter->subdoc_exists()) {
      if (use_init_marker == InitMarkerBehavior::REQUIRED &&
          !IsObjectType(doc_iter->subdoc_type())) {
        // We raise this error only if init markers are mandatory.
        return STATUS_FORMAT(IllegalState, "Cannot set values inside a subdocument of type $0",
            doc_iter->subdoc_type());
      }
      if ((subkey_index == num_subkeys - 1 && !is_deletion) ||
          use_init_marker == InitMarkerBehavior::OPTIONAL) {
        // We don't need to perform a RocksDB read at the last level for upserts, we just overwrite
        // the value within the last subdocument with what we're trying to write. We still perform
        // the read for deletions, because we try to avoid writing a new tombstone if the data is
        // not there anyway. Apart from the above case, if init markers are optional, there is no
        // point in seeking to intermediate document levels to verify their existence.
        if (use_init_marker == InitMarkerBehavior::OPTIONAL) {
          // In the case where init markers are optional, we don't need to check existence of
          // the current subdocument.
          doc_iter->AppendToPrefix(subkey);
        } else {
          // TODO: convert CHECKs inside the function below to a returned Status.
          doc_iter->AppendSubkeyInExistingSubDoc(subkey);
        }
      } else {
        // We need to check if the subdocument at this subkey exists.
        RETURN_NOT_OK(doc_iter->SeekToSubDocument(subkey));
        if (is_deletion && !doc_iter->subdoc_exists()) {
          // A parent subdocument of the value we're trying to delete, or that value itself,
          // does not exist, nothing to do.
          //
          // TODO: in Redis's HDEL command we need to count the number of fields deleted, so we need
          // to count the deletes that are actually happening.
          // See http://redis.io/commands/hdel
          DOCDB_DEBUG_LOG("Subdocument does not exist at subkey level $0 (subkey: $1)",
                          subkey_index, subkey.ToString());
          return Status::OK();
        }
      }
    } else {
      if (is_deletion) {
        // A parent subdocument of the subdocument we're trying to delete does not exist, nothing
        // to do.
        return Status::OK();
      }

      // The document/subdocument that this subkey is supposed to live in does not exist, create it.
      KeyBytes parent_key(doc_iter->key_prefix());

      // Add the parent key to key/value batch before appending the encoded HybridTime to it.
      // (We replicate key/value pairs without the HybridTime and only add it before writing to
      // RocksDB.)
      put_batch_.emplace_back(std::move(*parent_key.mutable_data()), kObjectValueType);

      // Update our local cache to record the fact that we're adding this subdocument, so that
      // future operations in this DocWriteBatch don't have to add it or look for it in RocksDB.
      cache_.Put(KeyBytes(doc_iter->key_prefix().AsSlice()), hybrid_time, ValueType::kObject);

      doc_iter->AppendToPrefix(subkey);
    }
  }

  // The key we use in the DocWriteBatchCache does not have a final hybrid_time, because that's the
  // key we expect to look up.
  cache_.Put(doc_iter->key_prefix(), hybrid_time, value.primitive_value().value_type());

  // The key in the key/value batch does not have an encoded HybridTime.
  put_batch_.emplace_back(doc_iter->key_prefix().AsStringRef(), value.Encode());

  return Status::OK();
}

Status DocWriteBatch::SetPrimitive(const DocPath& doc_path,
                                   const Value& value,
                                   InitMarkerBehavior use_init_marker) {
  DOCDB_DEBUG_LOG("Called with doc_path=$0, value=$1",
                  doc_path.ToString(), value.ToString());
  const KeyBytes& encoded_doc_key = doc_path.encoded_doc_key();
  const int num_subkeys = doc_path.num_subkeys();
  const bool is_deletion = value.primitive_value().value_type() == ValueType::kTombstone;

  InternalDocIterator doc_iter(rocksdb_, &cache_, BloomFilterMode::USE_BLOOM_FILTER,
      encoded_doc_key, rocksdb::kDefaultQueryId, &num_rocksdb_seeks_);

  if (num_subkeys > 0 || is_deletion) {
    doc_iter.SetDocumentKey(encoded_doc_key);
    if (use_init_marker == InitMarkerBehavior::REQUIRED) {
      // Navigate to the root of the document. We don't yet know whether the document exists or when
      // it was last updated.
      RETURN_NOT_OK(doc_iter.SeekToKeyPrefix());
      DOCDB_DEBUG_LOG("Top-level document exists: $0", doc_iter.subdoc_exists());
      if (!doc_iter.subdoc_exists() && is_deletion) {
        DOCDB_DEBUG_LOG("We're performing a deletion, and the document is not present. "
                            "Nothing to do.");
        return Status::OK();
      }
    }
  } else {
    // If we are overwriting an entire document with a primitive value (not deleting it), we don't
    // need to perform any reads from RocksDB at all.
    //
    // Even if we are deleting a document, but we don't need to get any feedback on whether the
    // deletion was performed or the document was not there to begin with, we could also skip the
    // read as an optimization.
    doc_iter.SetDocumentKey(encoded_doc_key);
  }

  return SetPrimitiveInternal(doc_path, value, &doc_iter, is_deletion, num_subkeys,
                              use_init_marker);
}

Status DocWriteBatch::ExtendSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    InitMarkerBehavior use_init_marker,
    MonoDelta ttl) {
  if (IsObjectType(value.value_type())) {
    const auto& map = value.object_container();
    for (const auto& ent : map) {
      DocPath child_doc_path = doc_path;
      child_doc_path.AddSubKey(ent.first);
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, ent.second, use_init_marker, ttl));
    }
  } else if (value.value_type() == ValueType::kArray) {
      RETURN_NOT_OK(ExtendList(doc_path, value, ListExtendOrder::APPEND, use_init_marker, ttl));
  } else {
    if (!value.IsPrimitive() && value.value_type() != ValueType::kTombstone) {
      return STATUS_FORMAT(InvalidArgument,
          "Found unexpected value type $0. Expecting a PrimitiveType or a Tombstone",
          value.value_type());
    }
    RETURN_NOT_OK(SetPrimitive(doc_path, Value(value, ttl), use_init_marker));
  }
  return Status::OK();
}

Status DocWriteBatch::InsertSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    InitMarkerBehavior use_init_marker,
    MonoDelta ttl) {
  if (!value.IsPrimitive() && value.value_type() != ValueType::kTombstone) {
    RETURN_NOT_OK(SetPrimitive(
        doc_path, Value(PrimitiveValue(value.value_type()), ttl), use_init_marker));
  }
  return ExtendSubDocument(doc_path, value, use_init_marker, ttl);
}

Status DocWriteBatch::DeleteSubDoc(
    const DocPath& doc_path,
    InitMarkerBehavior use_init_marker) {
  return SetPrimitive(doc_path, PrimitiveValue(ValueType::kTombstone), use_init_marker);
}

Status DocWriteBatch::ExtendList(
    const DocPath& doc_path,
    const SubDocument& value,
    ListExtendOrder extend_order,
    InitMarkerBehavior use_init_marker,
    MonoDelta ttl) {
  if (monotonic_counter_ == nullptr) {
    return STATUS(IllegalState, "List cannot be extended if monotonic_counter_ is uninitialized");
  }
  if (value.value_type() != ValueType::kArray) {
    return STATUS_FORMAT(
        InvalidArgument,
        "Expecting Subdocument of type kArray, found $0",
        value.value_type());
  }
  const std::vector<SubDocument>& list = value.array_container();
  // It is assumed that there is an exclusive lock on the list key.
  // The lock ensures that there isn't another thread picking ArrayIndexes for the same list.
  // No additional lock is required.
  int64_t index =
      std::atomic_fetch_add(monotonic_counter_, static_cast<int64_t>(list.size()));
  if (extend_order == ListExtendOrder::APPEND) {
    for (size_t i = 0; i < list.size(); i++) {
      DocPath child_doc_path = doc_path;
      index++;
      child_doc_path.AddSubKey(PrimitiveValue::ArrayIndex(index));
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, list[i], use_init_marker, ttl));
    }
  } else { // PREPEND - adding in reverse order with negated index
    for (size_t i = list.size(); i > 0; i--) {
      DocPath child_doc_path = doc_path;
      index++;
      child_doc_path.AddSubKey(PrimitiveValue::ArrayIndex(-index));
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, list[i - 1], use_init_marker, ttl));
    }
  }
  return Status::OK();
}

Status DocWriteBatch::ReplaceInList(
    const DocPath &doc_path,
    const vector<int>& indexes,
    const vector<SubDocument>& values,
    const HybridTime& current_time,
    const rocksdb::QueryId query_id,
    MonoDelta table_ttl,
    MonoDelta write_ttl,
    InitMarkerBehavior use_init_marker) {
  SubDocKey sub_doc_key;
  RETURN_NOT_OK(sub_doc_key.FromDocPath(doc_path));
  KeyBytes key_bytes = sub_doc_key.Encode( /*include_hybrid_time =*/ false);
  // Ensure we seek directly to indexes and skip init marker if it exists
  key_bytes.AppendValueType(ValueType::kArrayIndex);
  rocksdb::Slice seek_key = key_bytes.AsSlice();
  auto iter = CreateRocksDBIterator(rocksdb_, BloomFilterMode::USE_BLOOM_FILTER, seek_key,
      query_id);
  SubDocKey found_key;
  Value found_value;
  int current_index = 0;
  int replace_index = 0;
  ROCKSDB_SEEK(iter.get(), seek_key);
  while (true) {
    SubDocKey found_key;
    Value doc_value;

    if (indexes[replace_index] <= 0 || !iter->Valid() || !iter->key().starts_with(seek_key)) {
      return STATUS_SUBSTITUTE(
          QLError,
          "Unable to replace items into list, expecting index $0, reached end of list with size $1",
          indexes[replace_index],
          current_index);
    }

    RETURN_NOT_OK(found_key.FullyDecodeFrom(iter->key()));
    MonoDelta entry_ttl;
    rocksdb::Slice rocksdb_value = iter->value();
    RETURN_NOT_OK(Value::DecodeTTL(&rocksdb_value, &entry_ttl));
    entry_ttl = ComputeTTL(entry_ttl, table_ttl);

    if (!entry_ttl.Equals(Value::kMaxTtl)) {
      const HybridTime expiry =
          server::HybridClock::AddPhysicalTimeToHybridTime(found_key.hybrid_time(), entry_ttl);
      if (current_time > expiry) {
        found_key.KeepPrefix(sub_doc_key.num_subkeys()+1);
        SeekPastSubKey(found_key, iter.get());
        continue;
      }
    }
    current_index++;
    // Should we verify that the subkeys are indeed numbers as list indexes should be?
    // Or just go in order for the index'th largest key in any subdocument?
    if (current_index == indexes[replace_index]) {
      DocPath child_doc_path = doc_path;
      child_doc_path.AddSubKey(found_key.subkeys()[sub_doc_key.num_subkeys()]);
      RETURN_NOT_OK(InsertSubDocument(child_doc_path, values[replace_index], use_init_marker,
          write_ttl));
      replace_index++;
      if (replace_index == indexes.size()) {
        return Status::OK();
      }
    }
    SeekPastSubKey(found_key, iter.get());
  }
}

void DocWriteBatch::Clear() {
  put_batch_.clear();
  cache_.Clear();
}

void DocWriteBatch::MoveToWriteBatchPB(KeyValueWriteBatchPB *kv_pb) {
  kv_pb->mutable_kv_pairs()->Reserve(put_batch_.size());
  for (auto& entry : put_batch_) {
    KeyValuePairPB* kv_pair = kv_pb->add_kv_pairs();
    kv_pair->mutable_key()->swap(entry.first);
    kv_pair->mutable_value()->swap(entry.second);
  }
}

void DocWriteBatch::TEST_CopyToWriteBatchPB(KeyValueWriteBatchPB *kv_pb) const {
  kv_pb->mutable_kv_pairs()->Reserve(put_batch_.size());
  for (auto& entry : put_batch_) {
    KeyValuePairPB* kv_pair = kv_pb->add_kv_pairs();
    kv_pair->mutable_key()->assign(entry.first);
    kv_pair->mutable_value()->assign(entry.second);
  }
}

int DocWriteBatch::GetAndResetNumRocksDBSeeks() {
  const int ret_val = num_rocksdb_seeks_;
  num_rocksdb_seeks_ = 0;
  return ret_val;
}

// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

namespace {

// This works similar to the ScanSubDocument function, but doesn't assume that object init_markers
// are present. If no init marker is present, or if a tombstone is found at some level,
// it still looks for subkeys inside it if they have larger timestamps.
// TODO(akashnil): ENG-1152: If object init markers were required, this read path may be optimized.
// We look at all rocksdb keys with prefix = subdocument_key, and construct a subdocument out of
// them, between the timestamp range high_ts and low_ts.
//
// The iterator is expected to be placed at the smallest key that is subdocument_key or later, and
// after the function returns, the iterator should be placed just completely outside the
// subdocument_key prefix.
yb::Status BuildSubDocument(
    IntentAwareIterator* iter,
    const SubDocKey &subdocument_key,
    SubDocument* subdocument,
    const HybridTime high_ts,
    DocHybridTime low_ts,
    MonoDelta table_ttl) {
  DCHECK(!subdocument_key.has_hybrid_time());
  DOCDB_DEBUG_LOG("subdocument_key=$0, high_ts=$1, low_ts=$2, table_ttl=$3",
                  subdocument_key.ToString(),
                  high_ts.ToDebugString(),
                  low_ts.ToString(),
                  table_ttl.ToString());
  const KeyBytes encoded_key = subdocument_key.Encode();

  while (true) {

    SubDocKey found_key;
    Value doc_value;

    if (!iter->valid()) {
      return Status::OK();
    }

    if (!iter->key().starts_with(encoded_key.AsSlice())) {
      return Status::OK();
    }

    RETURN_NOT_OK(found_key.FullyDecodeFrom(iter->key()));

    rocksdb::Slice value = iter->value();

    if (high_ts < found_key.hybrid_time()) {
      found_key.SetHybridTimeForReadPath(high_ts);
      DOCDB_DEBUG_LOG("SeekForward: $0", found_key.ToString());
      RETURN_NOT_OK(iter->SeekForward(found_key));
      continue;
    }

    if (low_ts > found_key.doc_hybrid_time()) {
      DOCDB_DEBUG_LOG("SeekPastSubKey: $0", found_key.ToString());
      RETURN_NOT_OK(iter->SeekPastSubKey(found_key));
      continue;
    }

    RETURN_NOT_OK(doc_value.Decode(value));

    bool only_lacks_ht = false;
    RETURN_NOT_OK(encoded_key.OnlyLacksHybridTimeFrom(iter->key(), &only_lacks_ht));
    if (only_lacks_ht) {
      MonoDelta ttl;
      RETURN_NOT_OK(Value::DecodeTTL(&value, &ttl));

      ttl = ComputeTTL(ttl, table_ttl);

      DocHybridTime write_time = found_key.doc_hybrid_time();

      if (!ttl.Equals(Value::kMaxTtl)) {
        const HybridTime expiry =
            server::HybridClock::AddPhysicalTimeToHybridTime(found_key.hybrid_time(), ttl);
        if (high_ts.CompareTo(expiry) > 0) {
          // Treat the value as a tombstone written at expiry time.
          if (low_ts.hybrid_time() > expiry) {
            // We should have expiry > hybrid time from key > low_ts.
            return STATUS_SUBSTITUTE(Corruption,
                "Unexpected expiry time $0 found, should be higher than $1",
                expiry.ToDebugString(), low_ts.ToString());
          }
          doc_value = Value(PrimitiveValue(ValueType::kTombstone));
          // Use a write id that could never be used by a real operation within a single-shard txn,
          // so that we don't split that operation into multiple parts.
          write_time = DocHybridTime(expiry, kMaxWriteId);
        }
      }

      // We have found some key that matches our entire subdocument_key, i.e. we didn't skip ahead
      // to a lower level key (with optional object init markers).
      if (IsObjectType(doc_value.value_type()) ||
          doc_value.value_type() == ValueType::kArray ||
          doc_value.value_type() == ValueType::kTombstone) {
        if (low_ts < write_time) {
          low_ts = write_time;
        }
        if (IsObjectType(doc_value.value_type()) ||
            doc_value.value_type() == ValueType::kArray) {
          *subdocument = SubDocument(doc_value.value_type());
        }
        DOCDB_DEBUG_LOG("SeekPastSubKey: $0", found_key.ToString());
        RETURN_NOT_OK(iter->SeekPastSubKey(found_key));
        continue;
      } else {
        if (!IsPrimitiveValueType(doc_value.value_type())) {
          return STATUS_FORMAT(Corruption,
              "Expected primitive value type, got $0", doc_value.value_type());
        }

        DCHECK_GE(high_ts, write_time.hybrid_time());
        if (ttl.Equals(Value::kMaxTtl)) {
          doc_value.mutable_primitive_value()->SetTtl(-1);
        } else {
          int64_t time_since_write_seconds = (
              server::HybridClock::GetPhysicalValueMicros(high_ts) -
              server::HybridClock::GetPhysicalValueMicros(write_time.hybrid_time())) /
              MonoTime::kMicrosecondsPerSecond;
          int64_t ttl_seconds = std::max(static_cast<int64_t>(0),
              ttl.ToMilliseconds() / MonoTime::kMillisecondsPerSecond - time_since_write_seconds);
          doc_value.mutable_primitive_value()->SetTtl(ttl_seconds);
        }
        doc_value.mutable_primitive_value()->SetWritetime(write_time.hybrid_time().ToUint64());
        *subdocument = SubDocument(doc_value.primitive_value());
        DOCDB_DEBUG_LOG("SeekForward: $0.AdvanceOutOfSubDoc() = $1", found_key.ToString(),
            found_key.AdvanceOutOfSubDoc().ToString());
        RETURN_NOT_OK(iter->SeekOutOfSubDoc(found_key));
        return Status::OK();
      }
    }

    SubDocument descendant = SubDocument(PrimitiveValue(ValueType::kInvalidValueType));
    // TODO: what if found_key is the same as before? We'll get into an infinite recursion then.
    found_key.remove_hybrid_time();

    RETURN_NOT_OK(BuildSubDocument(iter, found_key, &descendant, high_ts, low_ts, table_ttl));
    if (descendant.value_type() == ValueType::kInvalidValueType) {
      // The document was not found in this level (maybe a tombstone was encountered).
      continue;
    }
    if (!IsObjectType(subdocument->value_type())) {
      *subdocument = SubDocument();
    }

    SubDocument* current = subdocument;

    for (int i = subdocument_key.num_subkeys(); i < found_key.num_subkeys() - 1; i++) {
      current = current->GetOrAddChild(found_key.subkeys()[i]).first;
    }
    current->SetChild(found_key.subkeys().back(), SubDocument(descendant));
  }
}

}  // namespace

yb::Status GetSubDocument(rocksdb::DB *db,
    const SubDocKey& subdocument_key,
    const rocksdb::QueryId query_id,
    const TransactionOperationContextOpt& txn_op_context,
    SubDocument* result,
    bool* doc_found,
    HybridTime scan_ht,
    MonoDelta table_ttl,
    bool return_type_only) {
  const auto doc_key_encoded = subdocument_key.doc_key().Encode();
  auto iter = CreateIntentAwareIterator(
      db, BloomFilterMode::USE_BLOOM_FILTER, doc_key_encoded.AsSlice(), query_id, txn_op_context,
      scan_ht);
  return GetSubDocument(
      iter.get(), subdocument_key, result, doc_found, scan_ht, table_ttl,
      nullptr /* projection */, return_type_only, false /* is_iter_valid */);
}

yb::Status GetSubDocument(
    IntentAwareIterator *db_iter,
    const SubDocKey& subdocument_key,
    SubDocument *result,
    bool *doc_found,
    const HybridTime scan_ht,
    MonoDelta table_ttl,
    const vector<PrimitiveValue>* projection,
    bool return_type_only,
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
  *doc_found = false;
  DOCDB_DEBUG_LOG("GetSubDocument for key $0 @ $1", subdocument_key.ToString(),
      scan_ht.ToDebugString());
  DocHybridTime max_deleted_ts(DocHybridTime::kMin);

  SubDocKey found_subdoc_key;

  DCHECK(!subdocument_key.has_hybrid_time());
  KeyBytes key_bytes = subdocument_key.doc_key().Encode();

  if (is_iter_valid) {
    RETURN_NOT_OK(db_iter->SeekForwardWithoutHt(key_bytes));
  } else {
    RETURN_NOT_OK(db_iter->SeekWithoutHt(key_bytes));
  }

  // Check ancestors for init markers and tombstones, update max_deleted_ts with them.
  for (const PrimitiveValue& subkey : subdocument_key.subkeys()) {
    RETURN_NOT_OK(db_iter->FindLastWriteTime(key_bytes, scan_ht, &max_deleted_ts, nullptr));
    subkey.AppendToKey(&key_bytes);
  }

  // By this point key_bytes is the encoded representation of the DocKey and all the subkeys of
  // subdocument_key.

  // Check for init-marker / tombstones at the top level, update max_deleted_ts.
  Value doc_value = Value(PrimitiveValue(ValueType::kInvalidValueType));
  RETURN_NOT_OK(db_iter->FindLastWriteTime(key_bytes, scan_ht, &max_deleted_ts, &doc_value));

  if (return_type_only) {
    *doc_found = doc_value.value_type() != ValueType::kInvalidValueType;
    *result = SubDocument(doc_value.primitive_value());
    return Status::OK();
  }

  if (projection == nullptr) {
    *result = SubDocument(ValueType::kInvalidValueType);
    RETURN_NOT_OK(BuildSubDocument(db_iter, subdocument_key, result, scan_ht, max_deleted_ts,
        table_ttl));
    *doc_found = result->value_type() != ValueType::kInvalidValueType;
    if (*doc_found && doc_value.value_type() == ValueType::kRedisSet) {
      RETURN_NOT_OK(result->ConvertToRedisSet());
    } else if (*doc_found && doc_value.value_type() == ValueType::kRedisTS) {
      RETURN_NOT_OK(result->ConvertToRedisTS());
    }
    // TODO: Also could handle lists here.

    return Status::OK();
  }
  // For each subkey in the projection, build subdocument.
  *result = SubDocument();
  for (const PrimitiveValue& subkey : *projection) {
    SubDocument descendant(ValueType::kInvalidValueType);
    SubDocKey projection_subdockey = subdocument_key;
    projection_subdockey.AppendSubKeysAndMaybeHybridTime(subkey);
    // This seek is to initialize the iterator for BuildSubDocument call.
    RETURN_NOT_OK(db_iter->SeekForwardWithoutHt(
        projection_subdockey.Encode(/* include_hybrid_time */ false)));
    RETURN_NOT_OK(BuildSubDocument(db_iter, projection_subdockey, &descendant, scan_ht,
        max_deleted_ts, table_ttl));
    if (descendant.value_type() != ValueType::kInvalidValueType) {
      *doc_found = true;
    }
    result->SetChild(subkey, std::move(descendant));
  }
  // Make sure the iterator is placed outside the whole document in the end.
  return db_iter->SeekForwardWithoutHt(subdocument_key.AdvanceOutOfSubDoc());
}

// ------------------------------------------------------------------------------------------------
// Debug output
// ------------------------------------------------------------------------------------------------

Result<std::string> DocDBKeyToDebugStr(const Slice& key_slice, KeyType* key_type) {
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
      RETURN_NOT_OK(subdoc_key.FullyDecodeFrom(intent_prefix, false));
      return subdoc_key.ToString() + " " + yb::docdb::ToString(intent_type) + " " +
          doc_ht.ToString();
    }
    case KeyType::kReverseTxnKey:
      // TODO(dtxn) - support dumping reverse txn key-values.
      return std::string();
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
  return prefix;
}

Status DocDBDebugDump(rocksdb::DB* rocksdb, ostream& out, const bool include_binary) {
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  iter->SeekToFirst();
  auto result_status = Status::OK();

  auto handle_error = [&result_status, &iter, &out](const Status& s) -> void {
    if (!result_status.ok()) {
      result_status = s;
    }
    out << s.ToString();
    iter->Next();
  };

  while (iter->Valid()) {
    KeyType key_type;
    Result<std::string> key_str = DocDBKeyToDebugStr(iter->key(), &key_type);
    if (!key_str.ok()) {
      handle_error(key_str.status());
      continue;
    }
    if (key_type == KeyType::kReverseTxnKey) {
      // TODO(dtxn) - support dumping reverse txn key-values, for now we just skip them.
      iter->Next();
      continue;
    }

    Result<std::string> value_str = DocDBValueToDebugStr(iter->value(), key_type);
    if (!value_str.ok()) {
      handle_error(value_str.status().CloneAndAppend(Substitute(". Key: $0", *key_str)));
      continue;
    }
    out << *key_str << " -> " << *value_str << endl;
    if (include_binary) {
      out << FormatRocksDBSliceAsStr(iter->key()) << " -> "
          << FormatRocksDBSliceAsStr(iter->value()) << endl << endl;
    }

    iter->Next();
  }
  return Status::OK();
}

std::string DocDBDebugDumpToStr(rocksdb::DB* rocksdb, const bool include_binary) {
  stringstream ss;
  CHECK_OK(DocDBDebugDump(rocksdb, ss, include_binary));
  return ss.str();
}

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, docdb::KeyBytes* out) {
  out->AppendValueType(docdb::ValueType::kIntentPrefix);
  out->AppendValueType(docdb::ValueType::kTransactionId);
  out->AppendRawBytes(Slice(transaction_id.data, transaction_id.size()));
}

}  // namespace docdb
}  // namespace yb

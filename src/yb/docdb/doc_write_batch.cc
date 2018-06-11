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

#include "yb/docdb/doc_write_batch.h"

#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/internal_doc_iterator.h"
#include "yb/docdb/value_type.h"
#include "yb/rocksdb/db.h"
#include "yb/server/hybrid_clock.h"

using yb::server::HybridClock;

namespace yb {
namespace docdb {

DocWriteBatch::DocWriteBatch(const DocDB& doc_db,
                             InitMarkerBehavior init_marker_behavior,
                             std::atomic<int64_t>* monotonic_counter)
    : doc_db_(doc_db),
      init_marker_behavior_(init_marker_behavior),
      monotonic_counter_(monotonic_counter),
      num_rocksdb_seeks_(0) {
}

Result<bool> DocWriteBatch::SetPrimitiveInternalHandleUserTimestamp(
    const Value &value,
    InternalDocIterator* doc_iter) {
  bool should_apply = true;
  if (value.user_timestamp() != Value::kInvalidUserTimestamp) {
    // Seek for the older version of the key that we're about to write to. This is essentially a
    // NOOP if we've already performed the seek due to the cache used in our iterator.
    RETURN_NOT_OK(doc_iter->SeekToKeyPrefix());
    // We'd like to include tombstones in our timestamp comparisons as well.
    if ((doc_iter->subdoc_exists() || doc_iter->subdoc_type_unchecked() == ValueType::kTombstone) &&
        doc_iter->found_exact_key_prefix_unchecked()) {
      UserTimeMicros user_timestamp = doc_iter->subdoc_user_timestamp_unchecked();

      if (user_timestamp != Value::kInvalidUserTimestamp) {
        should_apply = value.user_timestamp() >= user_timestamp;
      } else {
        // Look at the hybrid time instead.
        const DocHybridTime& doc_hybrid_time = doc_iter->subdoc_ht_unchecked();
        if (doc_hybrid_time.hybrid_time().is_valid()) {
          should_apply = value.user_timestamp() >=
              doc_hybrid_time.hybrid_time().GetPhysicalValueMicros();
        }
      }
    }
  }
  return should_apply;
}

CHECKED_STATUS DocWriteBatch::SetPrimitiveInternal(
    const DocPath& doc_path,
    const Value& value,
    InternalDocIterator *doc_iter,
    const bool is_deletion,
    const int num_subkeys) {

  // The write_id is always incremented by one for each new element of the write batch.
  if (put_batch_.size() > numeric_limits<IntraTxnWriteId>::max()) {
    return STATUS_SUBSTITUTE(
        NotSupported,
        "Trying to add more than $0 key/value pairs in the same single-shard txn.",
        numeric_limits<IntraTxnWriteId>::max());
  }

  if (value.has_user_timestamp() && !optional_init_markers()) {
    return STATUS(IllegalState,
                  "User Timestamp is only supported for Optional Init Markers");
  }

  // We need the write_id component of DocHybridTime to disambiguate between writes in the same
  // WriteBatch, as they will have the same HybridTime when committed. E.g. if we insert, delete,
  // and re-insert the same column in one WriteBatch, we need to know the order of these operations.
  const auto write_id = static_cast<IntraTxnWriteId>(put_batch_.size());
  const DocHybridTime hybrid_time = DocHybridTime(HybridTime::kMax, write_id);

  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    // We don't need to check if intermediate documents already exist if init markers are optional,
    // or if we already know they exist (either from previous reads or our own writes in the same
    // single-shard operation.)
    if (optional_init_markers() || doc_iter->subdoc_exists()) {
      if (required_init_markers() && !IsObjectType(doc_iter->subdoc_type())) {
        // REDIS
        // ~~~~~
        // We raise this error only if init markers are mandatory.
        return STATUS_FORMAT(IllegalState,
                             "Cannot set values inside a subdocument of type $0",
                             doc_iter->subdoc_type());
      }
      if (optional_init_markers()) {
        // CASSANDRA
        // ~~~~~~~~~
        // In the case where init markers are optional, we don't need to check existence of
        // the current subdocument. Although if we have a user timestamp specified, we need to
        // check whether the provided user timestamp is higher than what is already present. If
        // an intermediate subdocument is found with a higher timestamp, we consider it as an
        // overwrite and skip the entire write.
        auto should_apply = SetPrimitiveInternalHandleUserTimestamp(value, doc_iter);
        RETURN_NOT_OK(should_apply);

        if (!should_apply.get()) {
          return Status::OK();
        }
        doc_iter->AppendToPrefix(subkey);
      } else if (subkey_index == num_subkeys - 1 && !is_deletion) {
        // REDIS
        // ~~~~~
        // We don't need to perform a RocksDB read at the last level for upserts, we just overwrite
        // the value within the last subdocument with what we're trying to write. We still perform
        // the read for deletions, because we try to avoid writing a new tombstone if the data is
        // not there anyway.
        RETURN_NOT_OK(doc_iter->AppendSubkeyInExistingSubDoc(subkey));
      } else {
        // REDIS
        // ~~~~~
        // We need to check if the subdocument at this subkey exists.
        RETURN_NOT_OK(doc_iter->SeekToSubDocument(subkey));
        if (is_deletion && !doc_iter->subdoc_exists()) {
          // A parent subdocument of the value we're trying to delete, or that value itself, does
          // not exist, nothing to do.
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
      // REDIS
      // ~~~~~
      // The subdocument at the current level does not exist.
      if (is_deletion) {
        // A parent subdocument of the subdocument we're trying to delete does not exist, nothing
        // to do.
        return Status::OK();
      }

      DCHECK(!value.has_user_timestamp());

      // The document/subdocument that this subkey is supposed to live in does not exist, create it.
      KeyBytes parent_key(doc_iter->key_prefix());

      // Add the parent key to key/value batch before appending the encoded HybridTime to it.
      // (We replicate key/value pairs without the HybridTime and only add it before writing to
      // RocksDB.)
      put_batch_.emplace_back(std::move(*parent_key.mutable_data()),
                              string(1, ValueTypeAsChar::kObject));

      // Update our local cache to record the fact that we're adding this subdocument, so that
      // future operations in this DocWriteBatch don't have to add it or look for it in RocksDB.
      cache_.Put(KeyBytes(doc_iter->key_prefix().AsSlice()), hybrid_time, ValueType::kObject);

      doc_iter->AppendToPrefix(subkey);
    }
  }

  // We need to handle the user timestamp if present.
  auto should_apply = SetPrimitiveInternalHandleUserTimestamp(value, doc_iter);
  RETURN_NOT_OK(should_apply);

  if (should_apply.get()) {
    // The key in the key/value batch does not have an encoded HybridTime.
    put_batch_.emplace_back(doc_iter->key_prefix().AsStringRef(), value.Encode());

    // The key we use in the DocWriteBatchCache does not have a final hybrid_time, because that's
    // the key we expect to look up.
    cache_.Put(doc_iter->key_prefix(), hybrid_time, value.primitive_value().value_type(),
               value.user_timestamp());
  }

  return Status::OK();
}

Status DocWriteBatch::SetPrimitive(const DocPath& doc_path,
                                   const Value& value,
                                   rocksdb::QueryId query_id) {
  DOCDB_DEBUG_LOG("Called with doc_path=$0, value=$1",
                  doc_path.ToString(), value.ToString());
  const KeyBytes& encoded_doc_key = doc_path.encoded_doc_key();
  const int num_subkeys = doc_path.num_subkeys();
  const bool is_deletion = value.primitive_value().value_type() == ValueType::kTombstone;
  InternalDocIterator doc_iter(
      doc_db_.regular, &cache_, BloomFilterMode::USE_BLOOM_FILTER, encoded_doc_key,
      query_id, &num_rocksdb_seeks_);

  if (num_subkeys > 0 || is_deletion) {
    doc_iter.SetDocumentKey(encoded_doc_key);
    if (required_init_markers()) {
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

  return SetPrimitiveInternal(doc_path, value, &doc_iter, is_deletion, num_subkeys);
}

Status DocWriteBatch::ExtendSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    rocksdb::QueryId query_id,
    MonoDelta ttl,
    UserTimeMicros user_timestamp) {
  if (IsObjectType(value.value_type())) {
    const auto& map = value.object_container();
    for (const auto& ent : map) {
      DocPath child_doc_path = doc_path;
      child_doc_path.AddSubKey(ent.first);
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, ent.second, query_id, ttl, user_timestamp));
    }
  } else if (value.value_type() == ValueType::kArray) {
      RETURN_NOT_OK(ExtendList(
          doc_path, value, ListExtendOrder::APPEND, query_id, ttl, user_timestamp));
  } else {
    if (!value.IsTombstoneOrPrimitive()) {
      return STATUS_FORMAT(
          InvalidArgument,
          "Found unexpected value type $0. Expecting a PrimitiveType or a Tombstone",
          value.value_type());
    }
    RETURN_NOT_OK(SetPrimitive(doc_path, Value(value, ttl, user_timestamp), query_id));
  }
  return Status::OK();
}

Status DocWriteBatch::InsertSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    rocksdb::QueryId query_id,
    MonoDelta ttl,
    UserTimeMicros user_timestamp) {
  if (!value.IsTombstoneOrPrimitive()) {
    RETURN_NOT_OK(SetPrimitive(
        doc_path, Value(PrimitiveValue(value.value_type()), ttl, user_timestamp), query_id));
  }
  return ExtendSubDocument(doc_path, value, query_id, ttl, user_timestamp);
}

Status DocWriteBatch::DeleteSubDoc(
    const DocPath& doc_path,
    rocksdb::QueryId query_id,
    UserTimeMicros user_timestamp) {
  return SetPrimitive(doc_path, PrimitiveValue::kTombstone, query_id, user_timestamp);
}

Status DocWriteBatch::ExtendList(
    const DocPath& doc_path,
    const SubDocument& value,
    ListExtendOrder extend_order,
    rocksdb::QueryId query_id,
    MonoDelta ttl,
    UserTimeMicros user_timestamp) {
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
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, list[i], query_id, ttl, user_timestamp));
    }
  } else { // PREPEND - adding in reverse order with negated index
    for (size_t i = list.size(); i > 0; i--) {
      DocPath child_doc_path = doc_path;
      index++;
      child_doc_path.AddSubKey(PrimitiveValue::ArrayIndex(-index));
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, list[i - 1], query_id, ttl, user_timestamp));
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
    MonoDelta write_ttl) {
  SubDocKey sub_doc_key;
  RETURN_NOT_OK(sub_doc_key.FromDocPath(doc_path));
  KeyBytes key_bytes = sub_doc_key.Encode();
  // Ensure we seek directly to indexes and skip init marker if it exists
  key_bytes.AppendValueType(ValueType::kArrayIndex);
  rocksdb::Slice seek_key = key_bytes.AsSlice();
  auto iter = CreateRocksDBIterator(doc_db_.regular, BloomFilterMode::USE_BLOOM_FILTER, seek_key,
                                    query_id);
  SubDocKey found_key;
  Value found_value;
  int current_index = 0;
  int replace_index = 0;
  ROCKSDB_SEEK(iter.get(), seek_key);
  while (true) {
    if (indexes[replace_index] <= 0 || !iter->Valid() || !iter->key().starts_with(seek_key)) {
      return STATUS_SUBSTITUTE(
          QLError,
          "Unable to replace items into list, expecting index $0, reached end of list with size $1",
          indexes[replace_index] - 1, // YQL layer list index starts from 0, not 1 as in DocDB.
          current_index);
    }

    SubDocKey found_key;
    RETURN_NOT_OK(found_key.FullyDecodeFrom(iter->key()));
    MonoDelta entry_ttl;
    rocksdb::Slice rocksdb_value = iter->value();
    RETURN_NOT_OK(Value::DecodeTTL(&rocksdb_value, &entry_ttl));
    entry_ttl = ComputeTTL(entry_ttl, table_ttl);

    if (!entry_ttl.Equals(Value::kMaxTtl)) {
      const HybridTime expiry = HybridClock::AddPhysicalTimeToHybridTime(found_key.hybrid_time(),
                                                                         entry_ttl);
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
      RETURN_NOT_OK(InsertSubDocument(child_doc_path, values[replace_index], query_id, write_ttl));
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

}  // namespace docdb
}  // namespace yb

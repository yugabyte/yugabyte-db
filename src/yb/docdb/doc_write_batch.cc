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

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/ql_value.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/kv_debug.h"
#include "yb/docdb/read_operation_data.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_path.h"
#include "yb/dockv/doc_ttl_util.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/subdocument.h"
#include "yb/dockv/value_type.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/write_batch.h"

#include "yb/rocksutil/write_batch_formatter.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/checked_narrow_cast.h"
#include "yb/util/enums.h"
#include "yb/util/fast_varint.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

using std::numeric_limits;
using std::string;

namespace yb {
namespace docdb {

using dockv::DocPath;
using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryValue;
using dockv::SubDocKey;
using dockv::ValueControlFields;
using dockv::ValueEntryType;

// Lazily creates iterator on demand.
struct DocWriteBatch::LazyIterator {
 public:
  std::unique_ptr<IntentAwareIterator> iterator;
  const DocDB* doc_db;
  const DocPath* doc_path;
  const ReadOperationData* read_operation_data;
  rocksdb::QueryId query_id;

  IntentAwareIterator& Iterator() {
    if (!iterator) {
      iterator = CreateIntentAwareIterator(
          *doc_db,
          BloomFilterMode::USE_BLOOM_FILTER,
          doc_path->encoded_doc_key().AsSlice(),
          query_id,
          TransactionOperationContext(),
          *read_operation_data);
    }
    return *iterator;
  }
};

DocWriteBatch::DocWriteBatch(const DocDB& doc_db,
                             InitMarkerBehavior init_marker_behavior,
                             std::reference_wrapper<const ScopedRWOperation> pending_op,
                             std::atomic<int64_t>* monotonic_counter)
    : doc_db_(doc_db),
      init_marker_behavior_(init_marker_behavior),
      pending_op_(pending_op),
      monotonic_counter_(monotonic_counter) {}

Status DocWriteBatch::SeekToKeyPrefix(LazyIterator* iter, HasAncestor has_ancestor) {
  subdoc_exists_ = false;
  current_entry_.value_type = ValueEntryType::kInvalid;

  // Check the cache first.
  boost::optional<DocWriteBatchCache::Entry> cached_entry = cache_.Get(key_prefix_);
  if (cached_entry) {
    current_entry_ = *cached_entry;
    subdoc_exists_ = current_entry_.value_type != ValueEntryType::kTombstone;
    return Status::OK();
  }
  return SeekToKeyPrefix(&iter->Iterator(), has_ancestor);
}

Status DocWriteBatch::SeekToKeyPrefix(IntentAwareIterator* doc_iter, HasAncestor has_ancestor) {
  const auto prev_subdoc_ht = current_entry_.doc_hybrid_time;
  const auto prev_key_prefix_exact = current_entry_.found_exact_key_prefix;

  // Seek the value.
  doc_iter->Seek(key_prefix_.AsSlice());
  VLOG_WITH_FUNC(4) << SubDocKey::DebugSliceToString(key_prefix_.AsSlice())
                    << ", prev_subdoc_ht: " << prev_subdoc_ht
                    << ", prev_key_prefix_exact: " << prev_key_prefix_exact
                    << ", has_ancestor: " << has_ancestor;

  auto key_data = VERIFY_RESULT_REF(doc_iter->Fetch());

  bool use_packed_row = false;
  Slice recent_value;
  if (!packed_row_key_.empty() && key_prefix_.AsSlice().starts_with(packed_row_key_.AsSlice())) {
    auto subkeys = key_prefix_.AsSlice().WithoutPrefix(packed_row_key_.size());
    if (!subkeys.empty() && IsColumnId(static_cast<KeyEntryType>(subkeys[0]))) {
      if (key_data.key.empty() || key_data.write_time < packed_row_write_time_) {
        KeyEntryType entry_type = static_cast<KeyEntryType>(subkeys.consume_byte());
        int64_t column_id_as_int64 = VERIFY_RESULT(FastDecodeSignedVarIntUnsafe(&subkeys));
        ColumnId column_id_ref;
        RETURN_NOT_OK(ColumnId::FromInt64(column_id_as_int64, &column_id_ref));
        if (subkeys.empty()) {
          key_data.write_time = packed_row_write_time_;
          key_data.key = key_prefix_.AsSlice();
          auto value_opt = packed_row_packing_->GetValue(
              column_id_ref, packed_row_value_.AsSlice());
          if (value_opt) {
            recent_value = *value_opt;
            use_packed_row = true;
          }
          VLOG_WITH_FUNC(4)
              << "Has packed row for: " << AsString(entry_type) << ", " << column_id_ref << ": "
              << recent_value.ToDebugHexString();
        }
      }
    }
  }

  if (key_data.key.empty() || !key_prefix_.IsPrefixOf(key_data.key)) {
    return Status::OK();
  }

  // Checking for expiration.
  if (!use_packed_row) {
    recent_value = key_data.value;
  }
  ValueControlFields control_fields;
  {
    auto value_copy = recent_value;
    control_fields = VERIFY_RESULT(ValueControlFields::Decode(&value_copy));
    current_entry_.user_timestamp = control_fields.timestamp;
    current_entry_.value_type = dockv::DecodeValueEntryType(value_copy);
    if (doc_read_context_ && IsPackedRow(current_entry_.value_type)) {
      RSTATUS_DCHECK_NE(current_entry_.value_type, ValueEntryType::kPackedRowV2,
                        Corruption, "Packed row V2 should be used for YCQL");
      value_copy.consume_byte();
      packed_row_key_.Assign(key_data.key);
      packed_row_packing_ = &VERIFY_RESULT_REF(
          doc_read_context_->schema_packing_storage.GetPacking(&value_copy));
      packed_row_value_.Assign(value_copy);
      packed_row_write_time_ = key_data.write_time;

      VLOG_WITH_FUNC(4)
          << "Init packed row: " << SubDocKey::DebugSliceToString(packed_row_key_.AsSlice())
          << ", value: " << packed_row_key_.AsSlice().ToDebugHexString() << ", write time: "
          << packed_row_write_time_.ToString();
    }
  }

  bool expired = VERIFY_RESULT(dockv::HasExpiredTTL(
      key_data.write_time, control_fields.ttl, doc_iter->read_time().read));

  VLOG_WITH_FUNC(4)
      << "Value: " << recent_value.ToDebugHexString() << ", control_fields: "
      << control_fields.ToString() << ", expired: " << expired << ", use_packed_row: "
      << use_packed_row << ", write time: " << key_data.write_time.ToString();

  if (expired) {
    current_entry_.value_type = ValueEntryType::kTombstone;
    current_entry_.doc_hybrid_time = key_data.write_time;
    current_entry_.found_exact_key_prefix = key_prefix_ == key_data.key;
    cache_.Put(key_prefix_, current_entry_);
    return Status::OK();
  }

  if (use_packed_row) {
    key_data = VERIFY_RESULT(doc_iter->NextFullValue());

    if (!key_data) {
      return Status::OK();
    }
  } else {
    key_data.value = recent_value;
  }

  // If the first key >= key_prefix_ in RocksDB starts with key_prefix_, then a
  // document/subdocument pointed to by key_prefix_ exists, or has been recently deleted.
  if (key_prefix_.IsPrefixOf(key_data.key)) {
    // No need to decode again if no merge records were encountered.
    if (key_data.value != recent_value) {
      auto value_copy = key_data.value;
      current_entry_.user_timestamp = VERIFY_RESULT(
          ValueControlFields::Decode(&value_copy)).timestamp;
      current_entry_.value_type = dockv::DecodeValueEntryType(value_copy);
    }
    current_entry_.found_exact_key_prefix = key_prefix_ == key_data.key;
    current_entry_.doc_hybrid_time = key_data.write_time;
    VLOG_WITH_FUNC(4)
        << "Current found_exact_key_prefix: " << current_entry_.found_exact_key_prefix
        << ", doc_hybrid_time: " << current_entry_.doc_hybrid_time << ", value_type: "
        << AsString(current_entry_.value_type);

    // TODO: with optional init markers we can find something that is more than one level
    //       deep relative to the current prefix.
    // Note: this comment was originally placed right before the line decoding the HybridTime,
    // which has since been refactored away. Not sure what this means, so keeping it for now.

    // Cache the results of reading from RocksDB so that we don't have to read again in a later
    // operation in the same DocWriteBatch.
    DOCDB_DEBUG_LOG("Writing to DocWriteBatchCache: $0",
                    dockv::BestEffortDocDBKeyToStr(key_prefix_));

    if (has_ancestor && prev_subdoc_ht > current_entry_.doc_hybrid_time &&
        prev_key_prefix_exact) {
      // We already saw an object init marker or a tombstone one level higher with a higher
      // hybrid_time, so just ignore this key/value pair. This had to be added when we switched
      // from a format with intermediate hybrid_times to our current format without them.
      //
      // Example (from a real test case):
      //
      // SubDocKey(DocKey([], ["a"]), [HT(38)]) -> {}
      // SubDocKey(DocKey([], ["a"]), [HT(37)]) -> DEL
      // SubDocKey(DocKey([], ["a"]), [HT(36)]) -> false
      // SubDocKey(DocKey([], ["a"]), [HT(1)]) -> {}
      // SubDocKey(DocKey([], ["a"]), ["y", HT(35)]) -> "lD\x97\xaf^m\x0a1\xa0\xfc\xc8YM"
      //
      // Caveat (04/17/2017): the HybridTime encoding in the above example is outdated.
      //
      // In the above layout, if we try to set "a.y.x" to a new value, we first seek to the
      // document key "a" and find that it exists, but then we seek to "a.y" and find that it
      // also exists as a primitive value (assuming we don't check the hybrid_time), and
      // therefore we can't create "a.y.x", which would be incorrect.
      subdoc_exists_ = false;
    } else {
      cache_.Put(key_prefix_, current_entry_);
      subdoc_exists_ = current_entry_.value_type != ValueEntryType::kTombstone;
    }
  }
  return Status::OK();
}

Result<bool> DocWriteBatch::SetPrimitiveInternalHandleUserTimestamp(
    const ValueControlFields& control_fields,
    LazyIterator* iter) {
  if (!control_fields.has_timestamp()) {
    return true;
  }
  // Seek for the older version of the key that we're about to write to. This is essentially a
  // NOOP if we've already performed the seek due to the cache.
  RETURN_NOT_OK(SeekToKeyPrefix(iter, HasAncestor::kFalse));
  // We'd like to include tombstones in our timestamp comparisons as well.

  VLOG_WITH_FUNC(4)
      << "found_exact_key_prefix: " << current_entry_.found_exact_key_prefix
      << ", subdoc_exists: " << subdoc_exists_ << ", value_type: "
      << AsString(current_entry_.value_type) << ", user_timestamp: "
      << current_entry_.user_timestamp << ", control fields: " << control_fields.ToString()
      << ", doc_hybrid_time: "
      << current_entry_.doc_hybrid_time;

  if (!current_entry_.found_exact_key_prefix) {
    return true;
  }
  if (!subdoc_exists_ && current_entry_.value_type != ValueEntryType::kTombstone) {
    return true;
  }

  if (current_entry_.user_timestamp != ValueControlFields::kInvalidTimestamp) {
    return control_fields.timestamp >= current_entry_.user_timestamp;
  }

  // Look at the hybrid time instead.
  const auto& doc_hybrid_time = current_entry_.doc_hybrid_time;
  if (doc_hybrid_time.empty()) {
    return true;
  }

  return control_fields.timestamp >= 0 &&
         implicit_cast<size_t>(control_fields.timestamp) >=
             VERIFY_RESULT(doc_hybrid_time.Decode()).hybrid_time().GetPhysicalValueMicros();
}

namespace {

Status AppendToKeySafely(
    const KeyEntryValue& subkey, const DocPath& doc_path, KeyBytes* key_bytes) {
  subkey.AppendToKey(key_bytes);
  return Status::OK();
}

}  // namespace

Status DocWriteBatch::SetPrimitiveInternal(
    const DocPath& doc_path,
    const ValueControlFields& control_fields,
    const ValueRef& value,
    LazyIterator* iter,
    const bool is_deletion,
    std::optional<IntraTxnWriteId> write_id) {
  UpdateMaxValueTtl(control_fields.ttl);

  // The write_id is always incremented by one for each new element of the write batch.
  if (put_batch_.size() > numeric_limits<IntraTxnWriteId>::max()) {
    return STATUS_SUBSTITUTE(
        NotSupported,
        "Trying to add more than $0 key/value pairs in the same single-shard txn.",
        numeric_limits<IntraTxnWriteId>::max());
  }

  if (control_fields.has_timestamp() && !optional_init_markers()) {
    return STATUS(IllegalState,
                  "User Timestamp is only supported for Optional Init Markers");
  }

  // We need the write_id component of DocHybridTime to disambiguate between writes in the same
  // WriteBatch, as they will have the same HybridTime when committed. E.g. if we insert, delete,
  // and re-insert the same column in one WriteBatch, we need to know the order of these operations.
  IntraTxnWriteId ht_write_id = write_id
      ? *write_id
      : VERIFY_RESULT(checked_narrow_cast<IntraTxnWriteId>(put_batch_.size()));
  EncodedDocHybridTime hybrid_time(DocHybridTime(HybridTime::kMax, ht_write_id));

  auto num_subkeys = doc_path.num_subkeys();
  for (size_t subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    const auto& subkey = doc_path.subkey(subkey_index);

    // We don't need to check if intermediate documents already exist if init markers are optional,
    // or if we already know they exist (either from previous reads or our own writes in the same
    // single-shard operation.)

    if (optional_init_markers() || subdoc_exists_) {
      if (required_init_markers() && !IsObjectType(current_entry_.value_type)) {
        // REDIS
        // ~~~~~
        // We raise this error only if init markers are mandatory.
        return STATUS_FORMAT(IllegalState,
                             "Cannot set values inside a subdocument of type $0",
                             current_entry_.value_type);
      }
      if (optional_init_markers()) {
        // CASSANDRA
        // ~~~~~~~~~
        // In the case where init markers are optional, we don't need to check existence of
        // the current subdocument. Although if we have a user timestamp specified, we need to
        // check whether the provided user timestamp is higher than what is already present. If
        // an intermediate subdocument is found with a higher timestamp, we consider it as an
        // overwrite and skip the entire write.
        auto should_apply = SetPrimitiveInternalHandleUserTimestamp(control_fields, iter);
        RETURN_NOT_OK(should_apply);
        if (!should_apply.get()) {
          return Status::OK();
        }

        RETURN_NOT_OK(AppendToKeySafely(subkey, doc_path, &key_prefix_));
      } else if (subkey_index == num_subkeys - 1 && !is_deletion) {
        // REDIS
        // ~~~~~
        // We don't need to perform a RocksDB read at the last level for upserts, we just overwrite
        // the value within the last subdocument with what we're trying to write. We still perform
        // the read for deletions, because we try to avoid writing a new tombstone if the data is
        // not there anyway.
        if (!subdoc_exists_) {
          return STATUS(IllegalState, "Subdocument is supposed to exist.");
        }
        if (!IsObjectType(current_entry_.value_type)) {
          return STATUS(IllegalState, "Expected object subdocument type.");
        }
        RETURN_NOT_OK(AppendToKeySafely(subkey, doc_path, &key_prefix_));
      } else {
        // REDIS
        // ~~~~~
        // We need to check if the subdocument at this subkey exists.
        if (!subdoc_exists_) {
          return STATUS(IllegalState, "Subdocument is supposed to exist. $0");
        }
        if (!IsObjectType(current_entry_.value_type)) {
          return STATUS(IllegalState, "Expected object subdocument type. $0");
        }
        RETURN_NOT_OK(AppendToKeySafely(subkey, doc_path, &key_prefix_));
        RETURN_NOT_OK(SeekToKeyPrefix(iter, HasAncestor::kTrue));
        if (is_deletion && !subdoc_exists_) {
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

      DCHECK(!control_fields.has_timestamp());

      // Add the parent key to key/value batch before appending the encoded HybridTime to it.
      // (We replicate key/value pairs without the HybridTime and only add it before writing to
      // RocksDB.)
      put_batch_.push_back({
        .key = key_prefix_.ToStringBuffer(),
        .value = std::string(1, dockv::ValueEntryTypeAsChar::kObject),
      });

      // Update our local cache to record the fact that we're adding this subdocument, so that
      // future operations in this DocWriteBatch don't have to add it or look for it in RocksDB.
      cache_.Put(key_prefix_, hybrid_time, ValueEntryType::kObject);
      RETURN_NOT_OK(AppendToKeySafely(subkey, doc_path, &key_prefix_));
    }
  }

  // We need to handle the user timestamp if present.
  if (VERIFY_RESULT(SetPrimitiveInternalHandleUserTimestamp(control_fields, iter)) || write_id) {
    // The key in the key/value batch does not have an encoded HybridTime.
    DocWriteBatchEntry* kv_pair_ptr;
    if (write_id) {
      put_batch_[*write_id].key = key_prefix_.ToStringBuffer();
      kv_pair_ptr = &put_batch_[*write_id];
    } else {
      put_batch_.push_back({
        .key = key_prefix_.ToStringBuffer(),
        .value = std::string(),
      });
      kv_pair_ptr = &put_batch_.back();
    }
    auto& encoded_value = kv_pair_ptr->value;
    control_fields.AppendEncoded(&encoded_value);
    size_t prefix_len = encoded_value.size();

    if (value.encoded_value()) {
      encoded_value.assign(value.encoded_value()->cdata(), value.encoded_value()->size());
    } else {
      dockv::AppendEncodedValue(value.value_pb(), &encoded_value);
      if (value.custom_value_type() != ValueEntryType::kInvalid) {
        encoded_value[prefix_len] = static_cast<char>(value.custom_value_type());
      }
    }

    // The key we use in the DocWriteBatchCache does not have a final hybrid_time, because that's
    // the key we expect to look up.
    cache_.Put(key_prefix_, hybrid_time, static_cast<ValueEntryType>(encoded_value[prefix_len]),
               control_fields.timestamp);
  }

  return Status::OK();
}

Status DocWriteBatch::SetPrimitive(
    const DocPath& doc_path,
    const ValueControlFields& control_fields,
    const ValueRef& value,
    std::unique_ptr<IntentAwareIterator> intent_iter) {
  LazyIterator iter = {
    .iterator = std::move(intent_iter),
    .doc_db = nullptr,
    .doc_path = nullptr,
    .read_operation_data = nullptr,
    .query_id = {},
  };
  return DoSetPrimitive(doc_path, control_fields, value, &iter, /* write_id= */ {});
}

Status DocWriteBatch::DoSetPrimitive(
    const DocPath& doc_path,
    const ValueControlFields& control_fields,
    const ValueRef& value,
    LazyIterator* iter,
    std::optional<IntraTxnWriteId> write_id) {
  DOCDB_DEBUG_LOG("Called SetPrimitive with doc_path=$0, value=$1",
                  doc_path.ToString(), value.ToString());
  current_entry_.doc_hybrid_time.Assign(EncodedDocHybridTime::kMin);
  const bool is_deletion = value.custom_value_type() == ValueEntryType::kTombstone;

  key_prefix_ = doc_path.encoded_doc_key();

  // If we are overwriting an entire document with a primitive value (not deleting it), we don't
  // need to perform any reads from RocksDB at all.
  //
  // Even if we are deleting a document, but we don't need to get any feedback on whether the
  // deletion was performed or the document was not there to begin with, we could also skip the
  // read as an optimization.
  if (doc_path.num_subkeys() > 0 || is_deletion) {
    if (required_init_markers()) {
      // Navigate to the root of the document. We don't yet know whether the document exists or when
      // it was last updated.
      RETURN_NOT_OK(SeekToKeyPrefix(iter, HasAncestor::kFalse));
      DOCDB_DEBUG_LOG("Top-level document exists: $0", subdoc_exists_);
      if (!subdoc_exists_ && is_deletion) {
        DOCDB_DEBUG_LOG("We're performing a deletion, and the document is not present. "
                        "Nothing to do.");
        return Status::OK();
      }
    }
  }
  return SetPrimitiveInternal(doc_path, control_fields, value, iter, is_deletion, write_id);
}

Status DocWriteBatch::SetPrimitive(const DocPath& doc_path,
                                   const ValueControlFields& control_fields,
                                   const ValueRef& value,
                                   const ReadOperationData& read_operation_data,
                                   rocksdb::QueryId query_id,
                                   std::optional<IntraTxnWriteId> write_id) {
  DOCDB_DEBUG_LOG("Called with doc_path=$0, value=$1", doc_path.ToString(), value.ToString());

  LazyIterator iter = {
    .iterator = nullptr,
    .doc_db = &doc_db_,
    .doc_path = &doc_path,
    .read_operation_data = &read_operation_data,
    .query_id = query_id,
  };
  return DoSetPrimitive(doc_path, control_fields, value, &iter, write_id);
}

Status DocWriteBatch::ExtendSubDocument(
    const DocPath& doc_path,
    const ValueRef& value,
    const ReadOperationData& read_operation_data,
    rocksdb::QueryId query_id,
    MonoDelta ttl,
    UserTimeMicros user_timestamp) {
  if (value.is_array()) {
    return ExtendList(doc_path, value, read_operation_data, query_id, ttl, user_timestamp);
  }
  if (value.is_set()) {
    ValueRef value_ref(
        value.write_instruction() == bfql::TSOpcode::kSetRemove ||
        value.write_instruction() == bfql::TSOpcode::kMapRemove
        ? ValueEntryType::kTombstone : ValueEntryType::kNullLow);
    for (const auto& key : value.value_pb().set_value().elems()) {
      DocPath child_doc_path = doc_path;
      child_doc_path.AddSubKey(KeyEntryValue::FromQLValuePB(key, value.sorting_type()));
      RETURN_NOT_OK(ExtendSubDocument(
          child_doc_path, value_ref, read_operation_data, query_id, ttl, user_timestamp));
    }
    return Status::OK();
  }
  if (value.is_map()) {
    const auto& map_value = value.value_pb().map_value();
    int size = map_value.keys().size();
    for (int i = 0; i != size; ++i) {
      DocPath child_doc_path = doc_path;
      const auto& key = map_value.keys(i);
      if (key.value_case() != QLValuePB::kVirtualValue ||
          key.virtual_value() != QLVirtualValuePB::ARRAY) {
        auto sorting_type =
            value.list_extend_order() == dockv::ListExtendOrder::APPEND
            ? value.sorting_type() : SortingType::kDescending;
        if (value.write_instruction() == bfql::TSOpcode::kListAppend &&
            key.value_case() == QLValuePB::kInt64Value) {
          child_doc_path.AddSubKey(KeyEntryValue::ArrayIndex(key.int64_value()));
        } else {
          child_doc_path.AddSubKey(KeyEntryValue::FromQLValuePB(key, sorting_type));
        }
      }
      RETURN_NOT_OK(ExtendSubDocument(
          child_doc_path,
          ValueRef(map_value.values(i), value),
          read_operation_data, query_id, ttl, user_timestamp));
    }
    return Status::OK();
  }
  auto control_fields = ValueControlFields{
    .ttl = ttl,
    .timestamp = user_timestamp,
  };
  return SetPrimitive(doc_path, control_fields, value, read_operation_data, query_id);
}

Status DocWriteBatch::InsertSubDocument(
    const DocPath& doc_path,
    const ValueRef& value,
    const ReadOperationData& read_operation_data,
    rocksdb::QueryId query_id,
    MonoDelta ttl,
    UserTimeMicros user_timestamp,
    bool init_marker_ttl) {
  if (!value.IsTombstoneOrPrimitive()) {
    auto key_ttl = init_marker_ttl ? ttl : ValueControlFields::kMaxTtl;
    auto control_fields = ValueControlFields {
      .ttl = key_ttl,
      .timestamp = user_timestamp,
    };
    RETURN_NOT_OK(SetPrimitive(
        doc_path, control_fields, ValueRef(value.ContainerValueType()), read_operation_data,
        query_id));
  }
  return ExtendSubDocument(doc_path, value, read_operation_data, query_id, ttl, user_timestamp);
}

Status DocWriteBatch::ExtendList(
    const DocPath& doc_path,
    const ValueRef& value,
    const ReadOperationData& read_operation_data,
    rocksdb::QueryId query_id,
    MonoDelta ttl,
    UserTimeMicros user_timestamp) {
  if (monotonic_counter_ == nullptr) {
    return STATUS(IllegalState, "List cannot be extended if monotonic_counter_ is uninitialized");
  }
  SCHECK(value.is_array(), InvalidArgument, Format("Expecting array value ref, found $0", value));

  const auto& array = value.value_pb().list_value().elems();
  // It is assumed that there is an exclusive lock on the list key.
  // The lock ensures that there isn't another thread picking ArrayIndexes for the same list.
  // No additional lock is required.
  int64_t index = std::atomic_fetch_add(monotonic_counter_, static_cast<int64_t>(array.size()));
  // PREPEND - adding in reverse order with negated index
  if (value.list_extend_order() == dockv::ListExtendOrder::PREPEND_BLOCK) {
    for (auto i = array.size(); i-- > 0;) {
      DocPath child_doc_path = doc_path;
      index++;
      child_doc_path.AddSubKey(KeyEntryValue::ArrayIndex(-index));
      RETURN_NOT_OK(ExtendSubDocument(
          child_doc_path, ValueRef(array.Get(i), value), read_operation_data, query_id,
          ttl, user_timestamp));
    }
  } else {
    for (const auto& elem : array) {
      DocPath child_doc_path = doc_path;
      index++;
      child_doc_path.AddSubKey(KeyEntryValue::ArrayIndex(
          value.list_extend_order() == dockv::ListExtendOrder::APPEND ? index : -index));
      RETURN_NOT_OK(ExtendSubDocument(
          child_doc_path, ValueRef(elem, value), read_operation_data, query_id, ttl,
          user_timestamp));
    }
  }
  return Status::OK();
}

Status DocWriteBatch::ReplaceRedisInList(
    const DocPath &doc_path,
    int64_t index,
    const ValueRef& value,
    const ReadOperationData& read_operation_data,
    const rocksdb::QueryId query_id,
    const Direction dir,
    const int64_t start_index,
    std::vector<string>* results,
    MonoDelta default_ttl,
    MonoDelta write_ttl) {
  SubDocKey sub_doc_key;
  RETURN_NOT_OK(sub_doc_key.FromDocPath(doc_path));
  key_prefix_ = sub_doc_key.Encode();

  auto iter = yb::docdb::CreateIntentAwareIterator(
      doc_db_,
      BloomFilterMode::USE_BLOOM_FILTER,
      key_prefix_.AsSlice(),
      query_id,
      TransactionOperationContext(),
      read_operation_data);

  if (dir == Direction::kForward) {
    // Ensure we seek directly to indices and skip init marker if it exists.
    key_prefix_.AppendKeyEntryType(KeyEntryType::kArrayIndex);
    RETURN_NOT_OK(SeekToKeyPrefix(iter.get(), HasAncestor::kFalse));
  } else {
    // We would like to seek past the entire list and go backwards.
    key_prefix_.AppendKeyEntryType(KeyEntryType::kMaxByte);
    iter->PrevSubDocKey(key_prefix_);
    key_prefix_.RemoveKeyEntryTypeSuffix(KeyEntryType::kMaxByte);
    key_prefix_.AppendKeyEntryType(KeyEntryType::kArrayIndex);
  }

  SubDocKey found_key;
  FetchedEntry key_data;
  for (auto current_index = start_index;;) {
    bool valid = index > 0;
    if (valid) {
      key_data = VERIFY_RESULT(iter->Fetch());
      valid = key_data && key_data.key.starts_with(key_prefix_);
    }
    if (!valid) {
      return STATUS_SUBSTITUTE(Corruption,
          "Index Error: $0, reached beginning of list with size $1",
          index - 1, // YQL layer list index starts from 0, not 1 as in DocDB.
          current_index);
    }
    RETURN_NOT_OK(found_key.FullyDecodeFrom(key_data.key, dockv::HybridTimeRequired::kFalse));

    if (VERIFY_RESULT(dockv::Value::IsTombstoned(key_data.value))) {
      found_key.KeepPrefix(sub_doc_key.num_subkeys() + 1);
      if (dir == Direction::kForward) {
        iter->SeekPastSubKey(key_data.key);
      } else {
        iter->PrevSubDocKey(KeyBytes(key_data.key));
      }
      continue;
    }

    // TODO (rahul): it may be cleaner to put this in the read path.
    // The code below is meant specifically for POP functionality in Redis lists.
    if (results) {
      dockv::Value v;
      RETURN_NOT_OK(v.Decode(key_data.value));
      results->push_back(v.primitive_value().GetString());
    }

    if (dir == Direction::kForward) {
      current_index++;
    } else {
      current_index--;
    }

    // Should we verify that the subkeys are indeed numbers as list indices should be?
    // Or just go in order for the index'th largest key in any subdocument?
    if (current_index == index) {
      // When inserting, key_prefix_ is modified.
      KeyBytes array_index_prefix(key_prefix_);
      DocPath child_doc_path = doc_path;
      child_doc_path.AddSubKey(found_key.subkeys()[sub_doc_key.num_subkeys()]);
      return InsertSubDocument(child_doc_path, value, read_operation_data, query_id, write_ttl);
    }

    if (dir == Direction::kForward) {
      iter->SeekPastSubKey(key_data.key);
    } else {
      iter->PrevSubDocKey(KeyBytes(key_data.key));
    }
  }
}

void DocWriteBatch::UpdateMaxValueTtl(const MonoDelta& ttl) {
  // Don't update the max value TTL if the value is uninitialized or if it is set to
  // kMaxTtl (i.e. use table TTL).
  if (!ttl.Initialized() || ttl.Equals(ValueControlFields::kMaxTtl)) {
    return;
  }
  if (!ttl_.Initialized() || ttl > ttl_) {
    ttl_ = ttl;
  }
}

Status DocWriteBatch::ReplaceCqlInList(
    const DocPath& doc_path,
    const int target_cql_index,
    const ValueRef& value,
    const ReadOperationData& read_operation_data,
    const rocksdb::QueryId query_id,
    MonoDelta default_ttl,
    MonoDelta write_ttl) {
  SubDocKey sub_doc_key;
  RETURN_NOT_OK(sub_doc_key.FromDocPath(doc_path));
  key_prefix_ = sub_doc_key.Encode();

  auto iter = yb::docdb::CreateIntentAwareIterator(
      doc_db_,
      BloomFilterMode::USE_BLOOM_FILTER,
      key_prefix_.AsSlice(),
      query_id,
      TransactionOperationContext(),
      read_operation_data);

  RETURN_NOT_OK(SeekToKeyPrefix(iter.get(), HasAncestor::kFalse));

  const auto& current_key = VERIFY_RESULT_REF(iter->Fetch());
  if (!current_key) {
    return STATUS(QLError, "Unable to replace items in empty list.");
  }

  // Note that the only case we should have a collection without an init marker is if the collection
  // was created with upsert semantics. e.g.:
  // UPDATE foo SET v = v + [1, 2] WHERE k = 1
  // If the value v at row k = 1 did not exist before, then it will be written without an init
  // marker. In this case, using DocHybridTime::kMin is valid, as it has the effect of treating each
  // collection item found in DocDB as if there were no higher-level overwrite or invalidation of
  // it.
  auto current_key_is_init_marker = current_key.key.compare(key_prefix_) == 0;
  auto collection_write_time = current_key_is_init_marker
      ? current_key.write_time : DocHybridTime::EncodedMin();

  Slice value_slice;
  SubDocKey found_key;
  int current_cql_index = 0;

  // Seek past init marker if it exists.
  key_prefix_.AppendKeyEntryType(KeyEntryType::kArrayIndex);
  RETURN_NOT_OK(SeekToKeyPrefix(iter.get(), HasAncestor::kFalse));

  FetchedEntry key_data;
  while (true) {
    bool valid = target_cql_index >= 0;
    if (valid) {
      key_data = VERIFY_RESULT(iter->Fetch());
      valid = key_data && key_data.key.starts_with(key_prefix_);
    }
    if (!valid) {
      return STATUS_SUBSTITUTE(
          QLError,
          "Unable to replace items into list, expecting index $0, reached end of list with size $1",
          target_cql_index,
          current_cql_index);
    }

    RETURN_NOT_OK(found_key.FullyDecodeFrom(key_data.key, dockv::HybridTimeRequired::kFalse));

    value_slice = key_data.value;
    auto entry_ttl = VERIFY_RESULT(ValueControlFields::Decode(&value_slice)).ttl;
    auto value_type = dockv::DecodeValueEntryType(value_slice);

    bool has_expired = false;
    if (value_type == ValueEntryType::kTombstone || key_data.write_time < collection_write_time) {
      has_expired = true;
    } else {
      entry_ttl = dockv::ComputeTTL(entry_ttl, default_ttl);
      has_expired = VERIFY_RESULT(dockv::HasExpiredTTL(
          key_data.write_time, entry_ttl, read_operation_data.read_time.read));
    }

    if (has_expired) {
      found_key.KeepPrefix(sub_doc_key.num_subkeys() + 1);
      iter->SeekPastSubKey(key_data.key);
      continue;
    }

    // Should we verify that the subkeys are indeed numbers as list indices should be?
    // Or just go in order for the index'th largest key in any subdocument?
    if (current_cql_index == target_cql_index) {
      // When inserting, key_prefix_ is modified.
      KeyBytes array_index_prefix(key_prefix_);
      DocPath child_doc_path = doc_path;
      child_doc_path.AddSubKey(found_key.subkeys()[sub_doc_key.num_subkeys()]);
      return InsertSubDocument(child_doc_path, value, read_operation_data, query_id, write_ttl);
    }

    current_cql_index++;
    iter->SeekPastSubKey(key_data.key);
  }
}

Status DocWriteBatch::DeleteSubDoc(
    const DocPath& doc_path,
    const ReadOperationData& read_operation_data,
    rocksdb::QueryId query_id,
    UserTimeMicros user_timestamp) {
  return SetPrimitive(
      doc_path, ValueRef(ValueEntryType::kTombstone), read_operation_data, query_id,
      user_timestamp);
}

void DocWriteBatch::Clear() {
  put_batch_.clear();
  cache_.Clear();
}

// TODO(lw_uc) allocate entries on the same arena, then just reference them.
void DocWriteBatch::MoveToWriteBatchPB(LWKeyValueWriteBatchPB *kv_pb) {
  for (auto& entry : put_batch_) {
    auto* kv_pair = kv_pb->add_write_pairs();
    kv_pair->dup_key(entry.key);
    kv_pair->dup_value(entry.value);
  }
  if (has_ttl()) {
    kv_pb->set_ttl(ttl_ns());
  }
}

void DocWriteBatch::TEST_CopyToWriteBatchPB(LWKeyValueWriteBatchPB *kv_pb) const {
  for (auto& entry : put_batch_) {
    auto* kv_pair = kv_pb->add_write_pairs();
    kv_pair->dup_key(entry.key);
    kv_pair->dup_value(entry.value);
  }
  if (has_ttl()) {
    kv_pb->set_ttl(ttl_ns());
  }
}

// ------------------------------------------------------------------------------------------------
// Converting a RocksDB write batch to a string.
// ------------------------------------------------------------------------------------------------

DocWriteBatchFormatter::DocWriteBatchFormatter(
    StorageDbType storage_db_type,
    BinaryOutputFormat binary_output_format,
    WriteBatchOutputFormat batch_output_format,
    std::string line_prefix,
    SchemaPackingProvider* schema_packing_provider)
    : WriteBatchFormatter(binary_output_format, batch_output_format, std::move(line_prefix)),
      storage_db_type_(storage_db_type),
      schema_packing_provider_(schema_packing_provider) {}

std::string DocWriteBatchFormatter::FormatKey(const Slice& key) {
  const auto key_result = DocDBKeyToDebugStr(key, storage_db_type_);
  if (key_result.ok()) {
    return *key_result;
  }
  return Format(
      "$0 (error: $1)",
      WriteBatchFormatter::FormatKey(key),
      key_result.status());
}

std::string DocWriteBatchFormatter::FormatValue(const Slice& key, const Slice& value) {
  auto key_type = GetKeyType(key, storage_db_type_);
  const auto value_result = DocDBValueToDebugStr(key_type, key, value, schema_packing_provider_);
  if (value_result.ok()) {
    return *value_result;
  }
  return Format(
      "$0 (error: $1)",
      WriteBatchFormatter::FormatValue(key, value),
      value_result.status());
}

Result<std::string> WriteBatchToString(
    const rocksdb::WriteBatch& write_batch,
    StorageDbType storage_db_type,
    BinaryOutputFormat binary_output_format,
    WriteBatchOutputFormat batch_output_format,
    std::string line_prefix,
    SchemaPackingProvider* schema_packing_provider) {
  DocWriteBatchFormatter formatter(
      storage_db_type, binary_output_format, batch_output_format, line_prefix,
      schema_packing_provider);
  RETURN_NOT_OK(write_batch.Iterate(&formatter));
  return formatter.str();
}

namespace {

const QLValuePB kNullValuePB;

}

ValueRef::ValueRef(ValueEntryType value_type) : value_pb_(&kNullValuePB), value_type_(value_type) {
}

std::string ValueRef::ToString() const {
  return YB_CLASS_TO_STRING(value_pb, value_type);
}

bool ValueRef::IsTombstoneOrPrimitive() const {
  return !is_array() && !is_map() && !is_set();
}

ValueEntryType ValueRef::ContainerValueType() const {
  if (value_type_ != ValueEntryType::kInvalid) {
    return value_type_;
  }
  if (is_array()) {
    return ValueEntryType::kArray;
  }
  if (is_map() || is_set()) {
    return ValueEntryType::kObject;
  }
  FATAL_INVALID_ENUM_VALUE(QLValuePB::ValueCase, value_pb_->value_case());
  return ValueEntryType::kInvalid;
}

bool ValueRef::is_array() const {
  return value_pb_->value_case() == QLValuePB::kListValue;
}

bool ValueRef::is_set() const {
  return value_pb_->value_case() == QLValuePB::kSetValue;
}

bool ValueRef::is_map() const {
  return value_pb_->value_case() == QLValuePB::kMapValue;
}

}  // namespace docdb
}  // namespace yb

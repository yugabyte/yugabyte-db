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

#include "yb/bfql/tserver_opcodes.h"

#include "yb/common/constants.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/docdb/doc_write_batch_cache.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/read_operation_data.h"
#include "yb/dockv/value.h"

#include "yb/rocksdb/cache.h"

#include "yb/rocksutil/write_batch_formatter.h"

#include "yb/util/enums.h"
#include "yb/util/monotime.h"
#include "yb/util/operation_counter.h"

namespace yb {
namespace docdb {

YB_DEFINE_ENUM(ValueRefType, (kPb)(kValueType));

// This class references value that should be inserted to DocWriteBatch.
// Also it contains various options for this value.
class ValueRef {
 public:
  explicit ValueRef(const QLValuePB& value_pb,
                    SortingType sorting_type = SortingType::kNotSpecified,
                    bfql::TSOpcode write_instruction = bfql::TSOpcode::kScalarInsert)
      : value_pb_(&value_pb), sorting_type_(sorting_type), write_instruction_(write_instruction),
        list_extend_order_(dockv::ListExtendOrder::APPEND),
        value_type_(dockv::ValueEntryType::kInvalid) {
  }

  explicit ValueRef(const QLValuePB& value_pb,
                    const ValueRef& value_ref)
      : value_pb_(&value_pb), sorting_type_(value_ref.sorting_type_),
        write_instruction_(value_ref.write_instruction_),
        list_extend_order_(value_ref.list_extend_order_),
        value_type_(dockv::ValueEntryType::kInvalid) {
  }

  explicit ValueRef(const QLValuePB& value_pb,
                    dockv::ListExtendOrder list_extend_order)
      : value_pb_(&value_pb), sorting_type_(SortingType::kNotSpecified),
        write_instruction_(bfql::TSOpcode::kScalarInsert),
        list_extend_order_(list_extend_order),
        value_type_(dockv::ValueEntryType::kInvalid) {
  }

  explicit ValueRef(dockv::ValueEntryType key_entry_type);

  explicit ValueRef(std::reference_wrapper<const Slice> encoded_value)
      : encoded_value_(&encoded_value.get()) {}

  const QLValuePB& value_pb() const {
    return *value_pb_;
  }

  void set_sorting_type(SortingType value) {
    sorting_type_ = value;
  }

  SortingType sorting_type() const {
    return sorting_type_;
  }

  dockv::ListExtendOrder list_extend_order() const {
    return list_extend_order_;
  }

  void set_list_extend_order(dockv::ListExtendOrder value) {
    list_extend_order_ = value;
  }

  void set_custom_value_type(dockv::ValueEntryType value) {
    value_type_ = value;
  }

  dockv::ValueEntryType custom_value_type() const {
    return value_type_;
  }

  bfql::TSOpcode write_instruction() const {
    return write_instruction_;
  }

  void set_write_instruction(bfql::TSOpcode value) {
    write_instruction_ = value;
  }

  const Slice* encoded_value() const {
    return encoded_value_;
  }

  bool is_array() const;

  bool is_set() const;

  bool is_map() const;

  dockv::ValueEntryType ContainerValueType() const;

  bool IsTombstoneOrPrimitive() const;

  std::string ToString() const;

 private:
  const QLValuePB* value_pb_;
  SortingType sorting_type_;
  bfql::TSOpcode write_instruction_;
  dockv::ListExtendOrder list_extend_order_;
  dockv::ValueEntryType value_type_;
  const Slice* encoded_value_ = nullptr;
};

// This controls whether "init markers" are required at all intermediate levels.
YB_DEFINE_ENUM(InitMarkerBehavior,
               // This is used in Redis. We need to keep track of document types such as strings,
               // hashes, sets, because there is no schema and due to Redis's error checking.
               (kRequired)

               // This is used in CQL. Existence of "a.b.c" implies existence of "a" and "a.b",
               // unless there are delete markers / TTL expiration involved.
               (kOptional));

YB_STRONGLY_TYPED_BOOL(HasAncestor);

// We store key/value as string to be able to move them to KeyValuePairPB later.
struct DocWriteBatchEntry {
  std::string key;
  std::string value;
};

// The DocWriteBatch class is used to build a RocksDB write batch for a DocDB batch of operations
// that may include a mix of write (set) or delete operations. It may read from RocksDB while
// writing, and builds up an internal rocksdb::WriteBatch while handling the operations.
// When all the operations are applied, the rocksdb::WriteBatch should be taken as output.
// Take ownership of it using std::move if it needs to live longer than this DocWriteBatch.
class DocWriteBatch {
 public:
  explicit DocWriteBatch(const DocDB& doc_db,
                         InitMarkerBehavior init_marker_behavior,
                         std::reference_wrapper<const ScopedRWOperation> pending_op,
                         std::atomic<int64_t>* monotonic_counter = nullptr);

  // Custom write_id could specified. Such write_id should be previously allocated with
  // ReserveWriteId. In this case the value will be put to batch into preallocated position.
  Status SetPrimitive(
      const dockv::DocPath& doc_path,
      const dockv::ValueControlFields& control_fields,
      const ValueRef& value,
      const ReadOperationData& read_operation_data = {},
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      std::optional<IntraTxnWriteId> write_id = {});

  Status SetPrimitive(
      const dockv::DocPath& doc_path,
      const dockv::ValueControlFields& control_fields,
      const ValueRef& value,
      std::unique_ptr<IntentAwareIterator> intent_iter);

  Status SetPrimitive(
      const dockv::DocPath& doc_path,
      const ValueRef& value,
      const ReadOperationData& read_operation_data = {},
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      UserTimeMicros user_timestamp = dockv::ValueControlFields::kInvalidTimestamp) {
    return SetPrimitive(
        doc_path, dockv::ValueControlFields { .timestamp = user_timestamp }, value,
        read_operation_data, query_id);
  }

  // Extend the SubDocument in the given key. We'll support List with Append and Prepend mode later.
  // TODO(akashnil): 03/20/17 ENG-1107
  // In each SetPrimitive call, some common work is repeated. It may be made more
  // efficient by not calling SetPrimitive internally.
  Status ExtendSubDocument(
      const dockv::DocPath& doc_path,
      const ValueRef& value,
      const ReadOperationData& read_operation_data = {},
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      MonoDelta ttl = dockv::ValueControlFields::kMaxTtl,
      UserTimeMicros user_timestamp = dockv::ValueControlFields::kInvalidTimestamp);

  Status InsertSubDocument(
      const dockv::DocPath& doc_path,
      const ValueRef& value,
      const ReadOperationData& read_operation_data = {},
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      MonoDelta ttl = dockv::ValueControlFields::kMaxTtl,
      UserTimeMicros user_timestamp = dockv::ValueControlFields::kInvalidTimestamp,
      bool init_marker_ttl = true);

  Status ExtendList(
      const dockv::DocPath& doc_path,
      const ValueRef& value,
      const ReadOperationData& read_operation_data = {},
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      MonoDelta ttl = dockv::ValueControlFields::kMaxTtl,
      UserTimeMicros user_timestamp = dockv::ValueControlFields::kInvalidTimestamp);

  // 'indices' must be sorted. List indexes are not zero indexed, the first element is list[1].
  Status ReplaceRedisInList(
      const dockv::DocPath& doc_path,
      int64_t index,
      const ValueRef& value,
      const ReadOperationData& read_operation_data,
      const rocksdb::QueryId query_id,
      const Direction dir = Direction::kForward,
      const int64_t start_index = 0,
      std::vector<std::string>* results = nullptr,
      MonoDelta default_ttl = dockv::ValueControlFields::kMaxTtl,
      MonoDelta write_ttl = dockv::ValueControlFields::kMaxTtl);

  Status ReplaceCqlInList(
      const dockv::DocPath &doc_path,
      const int index,
      const ValueRef& value,
      const ReadOperationData& read_operation_data,
      const rocksdb::QueryId query_id,
      MonoDelta default_ttl = dockv::ValueControlFields::kMaxTtl,
      MonoDelta write_ttl = dockv::ValueControlFields::kMaxTtl);

  Status DeleteSubDoc(
      const dockv::DocPath& doc_path,
      const ReadOperationData& read_operation_data = {},
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      UserTimeMicros user_timestamp = dockv::ValueControlFields::kInvalidTimestamp);

  void Clear();
  bool IsEmpty() const { return put_batch_.empty(); }

  size_t size() const { return put_batch_.size(); }

  const std::vector<DocWriteBatchEntry>& key_value_pairs() const {
    return put_batch_;
  }

  void MoveToWriteBatchPB(LWKeyValueWriteBatchPB *kv_pb);

  // This method has worse performance comparing to MoveToWriteBatchPB and intented to be used in
  // testing. Consider using MoveToWriteBatchPB in production code.
  void TEST_CopyToWriteBatchPB(LWKeyValueWriteBatchPB *kv_pb) const;

  // This is used in tests when measuring the number of seeks that a given update to this batch
  // performs. The internal seek count is reset.
  int GetAndResetNumRocksDBSeeks();

  const DocDB& doc_db() { return doc_db_; }

  std::reference_wrapper<const ScopedRWOperation> pending_op() { return pending_op_; }

  boost::optional<DocWriteBatchCache::Entry> LookupCache(
      const dockv::KeyBytes& encoded_key_prefix) {
    return cache_.Get(encoded_key_prefix);
  }

  DocWriteBatchEntry& AddRaw() {
    put_batch_.emplace_back();
    return put_batch_.back();
  }

  void UpdateMaxValueTtl(const MonoDelta& ttl);

  int64_t ttl_ns() const {
    return ttl_.ToNanoseconds();
  }

  bool has_ttl() const {
    return ttl_.Initialized();
  }

  // See SetPrimitive above.
  IntraTxnWriteId ReserveWriteId() {
    put_batch_.emplace_back();
    return narrow_cast<IntraTxnWriteId>(put_batch_.size()) - 1;
  }

  void RollbackReservedWriteId() {
    put_batch_.pop_back();
  }

  void SetDocReadContext(const DocReadContextPtr& doc_read_context) {
    doc_read_context_ = doc_read_context;
  }

 private:
  struct LazyIterator;

  // Set the primitive at the given path to the given value. Intermediate subdocuments are created
  // if necessary and possible.
  Status DoSetPrimitive(
      const dockv::DocPath& doc_path,
      const dockv::ValueControlFields& control_fields,
      const ValueRef& value,
      LazyIterator* doc_iter,
      std::optional<IntraTxnWriteId> write_id);

  Status SeekToKeyPrefix(LazyIterator* doc_iter, HasAncestor has_ancestor);
  Status SeekToKeyPrefix(IntentAwareIterator* doc_iter, HasAncestor has_ancestor);

  // This member function performs the necessary operations to set a primitive value for a given
  // docpath assuming the appropriate operations have been taken care of for subkeys with index <
  // subkey_index. This method assumes responsibility of ensuring the proper DocDB structure
  // (e.g: init markers) is maintained for subdocuments starting at the given subkey_index.
  Status SetPrimitiveInternal(
      const dockv::DocPath& doc_path,
      const dockv::ValueControlFields& control_fields,
      const ValueRef& value,
      LazyIterator* doc_iter,
      bool is_deletion,
      std::optional<IntraTxnWriteId> write_id);

  // Handle the user provided timestamp during writes.
  Result<bool> SetPrimitiveInternalHandleUserTimestamp(
      const dockv::ValueControlFields& control_fields, LazyIterator* doc_iter);

  bool required_init_markers() {
    return init_marker_behavior_ == InitMarkerBehavior::kRequired;
  }

  bool optional_init_markers() {
    return init_marker_behavior_ == InitMarkerBehavior::kOptional;
  }

  DocWriteBatchCache cache_;

  DocDB doc_db_;
  InitMarkerBehavior init_marker_behavior_;
  std::reference_wrapper<const ScopedRWOperation> pending_op_;
  DocReadContextPtr doc_read_context_;
  std::atomic<int64_t>* monotonic_counter_;

  std::vector<DocWriteBatchEntry> put_batch_;

  // Taken from internal_doc_iterator
  dockv::KeyBytes key_prefix_;
  bool subdoc_exists_ = true;
  DocWriteBatchCache::Entry current_entry_;

  KeyBuffer packed_row_key_;
  const dockv::SchemaPacking* packed_row_packing_;
  ValueBuffer packed_row_value_;
  EncodedDocHybridTime packed_row_write_time_;

  MonoDelta ttl_;
};

// A helper handler for converting a RocksDB write batch to a string.
class DocWriteBatchFormatter : public WriteBatchFormatter {
 public:
  DocWriteBatchFormatter(
      StorageDbType storage_db_type,
      BinaryOutputFormat binary_output_format,
      WriteBatchOutputFormat batch_output_format,
      std::string line_prefix,
      SchemaPackingProvider* schema_packing_provider);

 protected:
  std::string FormatKey(const Slice& key) override;

  std::string FormatValue(const Slice& key, const Slice& value) override;

 private:
  StorageDbType storage_db_type_;
  SchemaPackingProvider* schema_packing_provider_;
};

// Converts a RocksDB WriteBatch to a string.
// line_prefix is the prefix to be added to each line of the result. Could be used for indentation.
Result<std::string> WriteBatchToString(
    const rocksdb::WriteBatch& write_batch,
    StorageDbType storage_db_type,
    BinaryOutputFormat binary_output_format,
    WriteBatchOutputFormat batch_output_format,
    std::string line_prefix,
    SchemaPackingProvider* schema_packing_provider /*null ok*/);

}  // namespace docdb
}  // namespace yb

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

#ifndef YB_DOCDB_DOC_WRITE_BATCH_H
#define YB_DOCDB_DOC_WRITE_BATCH_H

#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_write_batch_cache.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/rocksdb/cache.h"
#include "yb/util/enums.h"

namespace rocksdb {
class DB;
}

namespace yb {
namespace docdb {

class KeyValueWriteBatchPB;
class InternalDocIterator;

// This controls whether "init markers" are required at all intermediate levels.
YB_DEFINE_ENUM(InitMarkerBehavior,
               // This is used in Redis. We need to keep track of document types such as strings,
               // hashes, sets, because there is no schema and due to Redis's error checking.
               (REQUIRED)

               // This is used in CQL. Existence of "a.b.c" implies existence of "a" and "a.b",
               // unless there are delete markers / TTL expiration involved.
               (OPTIONAL));

// Used for extending a list.
YB_DEFINE_ENUM(ListExtendOrder, (APPEND)(PREPEND))

// The DocWriteBatch class is used to build a RocksDB write batch for a DocDB batch of operations
// that may include a mix or write (set) or delete operations. It may read from RocksDB while
// writing, and builds up an internal rocksdb::WriteBatch while handling the operations.
// When all the operations are applied, the rocksdb::WriteBatch should be taken as output.
// Take ownership of it using std::move if it needs to live longer than this DocWriteBatch.
class DocWriteBatch {
 public:
  explicit DocWriteBatch(rocksdb::DB* rocksdb,
                         std::atomic<int64_t>* monotonic_counter = nullptr);

  // Set the primitive at the given path to the given value. Intermediate subdocuments are created
  // if necessary and possible.
  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path, const Value& value,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::REQUIRED,
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId);

  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path,
      const PrimitiveValue& value,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::REQUIRED,
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp) {
    return SetPrimitive(doc_path, Value(value, Value::kMaxTtl, user_timestamp), use_init_marker,
                        query_id);
  }

  // Extend the SubDocument in the given key. We'll support List with Append and Prepend mode later.
  // TODO(akashnil): 03/20/17 ENG-1107
  // In each SetPrimitive call, some common work is repeated. It may be made more
  // efficient by not calling SetPrimitive internally.
  CHECKED_STATUS ExtendSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL,
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      MonoDelta ttl = Value::kMaxTtl,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp);

  CHECKED_STATUS InsertSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL,
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      MonoDelta ttl = Value::kMaxTtl,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp);

  CHECKED_STATUS ExtendList(
      const DocPath& doc_path,
      const SubDocument& value,
      ListExtendOrder extend_order = ListExtendOrder::APPEND,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL,
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      MonoDelta ttl = Value::kMaxTtl,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp);

  // 'indexes' must be sorted. List indexes are not zero indexed, the first element is list[1].
  CHECKED_STATUS ReplaceInList(
      const DocPath &doc_path,
      const std::vector<int>& indexes,
      const std::vector<SubDocument>& values,
      const HybridTime& current_time,
      const rocksdb::QueryId query_id,
      MonoDelta table_ttl = Value::kMaxTtl,
      MonoDelta write_ttl = Value::kMaxTtl,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL);

  CHECKED_STATUS DeleteSubDoc(
      const DocPath& doc_path,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::REQUIRED,
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp);

  void Clear();
  bool IsEmpty() const { return put_batch_.empty(); }

  size_t size() const { return put_batch_.size(); }

  const std::vector<std::pair<std::string, std::string>>& key_value_pairs() const {
    return put_batch_;
  }

  void MoveToWriteBatchPB(KeyValueWriteBatchPB *kv_pb);

  // This method has worse performance comparing to MoveToWriteBatchPB and intented to be used in
  // testing. Consider using MoveToWriteBatchPB in production code.
  void TEST_CopyToWriteBatchPB(KeyValueWriteBatchPB *kv_pb) const;

  // This is used in tests when measuring the number of seeks that a given update to this batch
  // performs. The internal seek count is reset.
  int GetAndResetNumRocksDBSeeks();

  rocksdb::DB* rocksdb() { return rocksdb_; }

  boost::optional<DocWriteBatchCache::Entry> LookupCache(const KeyBytes& encoded_key_prefix) {
    return cache_.Get(encoded_key_prefix);
  }

 private:
  // This member function performs the necessary operations to set a primitive value for a given
  // docpath assuming the appropriate operations have been taken care of for subkeys with index <
  // subkey_index. This method assumes responsibility of ensuring the proper DocDB structure
  // (e.g: init markers) is maintained for subdocuments starting at the given subkey_index.
  CHECKED_STATUS SetPrimitiveInternal(
      const DocPath& doc_path,
      const Value& value,
      InternalDocIterator *doc_iter,
      bool is_deletion,
      int num_subkeys,
      InitMarkerBehavior use_init_marker);

  // Handle the user provided timestamp during writes.
  Result<bool> SetPrimitiveInternalHandleUserTimestamp(const Value &value,
                                                       InternalDocIterator* doc_iter);

  DocWriteBatchCache cache_;

  rocksdb::DB* rocksdb_;
  std::atomic<int64_t>* monotonic_counter_;
  std::vector<std::pair<std::string, std::string>> put_batch_;

  int num_rocksdb_seeks_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_WRITE_BATCH_H

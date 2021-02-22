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

#include "yb/common/hybrid_time.h"
#include "yb/util/enums.h"

#include "yb/common/read_hybrid_time.h"

#include "yb/rocksdb/cache.h"
#include "yb/rocksutil/write_batch_formatter.h"

#include "yb/docdb/docdb_types.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_write_batch_cache.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/util/monotime.h"

namespace rocksdb {
class DB;
}

namespace yb {
namespace docdb {

class KeyValueWriteBatchPB;
class IntentAwareIterator;

struct LazyIterator {
  std::function<std::unique_ptr<IntentAwareIterator>()>* creator;
  std::unique_ptr<IntentAwareIterator> iterator;

  explicit LazyIterator(std::function<std::unique_ptr<IntentAwareIterator>()>* c)
    : iterator(nullptr) {
    creator = c;
  }

  explicit LazyIterator(std::unique_ptr<IntentAwareIterator> i) {
    iterator = std::move(i);
  }

  ~LazyIterator() {}

  IntentAwareIterator* Iterator() {
    if (!iterator)
      iterator = (*creator)();
    return iterator.get();
  }
};

// This controls whether "init markers" are required at all intermediate levels.
YB_DEFINE_ENUM(InitMarkerBehavior,
               // This is used in Redis. We need to keep track of document types such as strings,
               // hashes, sets, because there is no schema and due to Redis's error checking.
               (kRequired)

               // This is used in CQL. Existence of "a.b.c" implies existence of "a" and "a.b",
               // unless there are delete markers / TTL expiration involved.
               (kOptional));

// The DocWriteBatch class is used to build a RocksDB write batch for a DocDB batch of operations
// that may include a mix or write (set) or delete operations. It may read from RocksDB while
// writing, and builds up an internal rocksdb::WriteBatch while handling the operations.
// When all the operations are applied, the rocksdb::WriteBatch should be taken as output.
// Take ownership of it using std::move if it needs to live longer than this DocWriteBatch.
class DocWriteBatch {
 public:
  explicit DocWriteBatch(const DocDB& doc_db,
                         InitMarkerBehavior init_marker_behavior,
                         std::atomic<int64_t>* monotonic_counter = nullptr);

  Status SeekToKeyPrefix(LazyIterator* doc_iter, bool has_ancestor = false);
  Status SeekToKeyPrefix(IntentAwareIterator* doc_iter, bool has_ancestor);

  // Set the primitive at the given path to the given value. Intermediate subdocuments are created
  // if necessary and possible.
  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path,
      const Value& value,
      LazyIterator* doc_iter);

  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path,
      const Value& value,
      const ReadHybridTime& read_ht = ReadHybridTime::Max(),
      const CoarseTimePoint deadline = CoarseTimePoint::max(),
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId);

  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path,
      const Value& value,
      std::unique_ptr<IntentAwareIterator> intent_iter) {
    LazyIterator iter(std::move(intent_iter));
    return SetPrimitive(doc_path, value, &iter);
  }


  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path,
      const PrimitiveValue& value,
      const ReadHybridTime& read_ht = ReadHybridTime::Max(),
      const CoarseTimePoint deadline = CoarseTimePoint::max(),
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp) {
    return SetPrimitive(doc_path, Value(value, Value::kMaxTtl, user_timestamp),
                        read_ht, deadline, query_id);
  }

  // Extend the SubDocument in the given key. We'll support List with Append and Prepend mode later.
  // TODO(akashnil): 03/20/17 ENG-1107
  // In each SetPrimitive call, some common work is repeated. It may be made more
  // efficient by not calling SetPrimitive internally.
  CHECKED_STATUS ExtendSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      const ReadHybridTime& read_ht = ReadHybridTime::Max(),
      const CoarseTimePoint deadline = CoarseTimePoint::max(),
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      MonoDelta ttl = Value::kMaxTtl,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp);

  CHECKED_STATUS InsertSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      const ReadHybridTime& read_ht = ReadHybridTime::Max(),
      const CoarseTimePoint deadline = CoarseTimePoint::max(),
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      MonoDelta ttl = Value::kMaxTtl,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp,
      bool init_marker_ttl = true);

  CHECKED_STATUS ExtendList(
      const DocPath& doc_path,
      const SubDocument& value,
      const ReadHybridTime& read_ht = ReadHybridTime::Max(),
      const CoarseTimePoint deadline = CoarseTimePoint::max(),
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      MonoDelta ttl = Value::kMaxTtl,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp);

  // 'indices' must be sorted. List indexes are not zero indexed, the first element is list[1].
  CHECKED_STATUS ReplaceRedisInList(
      const DocPath &doc_path,
      const std::vector<int>& indices,
      const std::vector<SubDocument>& values,
      const ReadHybridTime& read_ht,
      const CoarseTimePoint deadline,
      const rocksdb::QueryId query_id,
      const Direction dir = Direction::kForward,
      const int64_t start_index = 0,
      std::vector<string>* results = nullptr,
      MonoDelta default_ttl = Value::kMaxTtl,
      MonoDelta write_ttl = Value::kMaxTtl);

  CHECKED_STATUS ReplaceCqlInList(
      const DocPath &doc_path,
      const int index,
      const SubDocument& value,
      const ReadHybridTime& read_ht,
      const CoarseTimePoint deadline,
      const rocksdb::QueryId query_id,
      MonoDelta default_ttl = Value::kMaxTtl,
      MonoDelta write_ttl = Value::kMaxTtl);

  CHECKED_STATUS DeleteSubDoc(
      const DocPath& doc_path,
      const ReadHybridTime& read_ht = ReadHybridTime::Max(),
      const CoarseTimePoint deadline = CoarseTimePoint::max(),
      rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      UserTimeMicros user_timestamp = Value::kInvalidUserTimestamp) {
    return SetPrimitive(doc_path, PrimitiveValue::kTombstone,
                        read_ht, deadline, query_id, user_timestamp);
  }

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

  const DocDB& doc_db() { return doc_db_; }

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
      LazyIterator* doc_iter,
      bool is_deletion,
      int num_subkeys);

  // Handle the user provided timestamp during writes.
  Result<bool> SetPrimitiveInternalHandleUserTimestamp(const Value &value,
                                                       LazyIterator* doc_iter);

  bool required_init_markers() {
    return init_marker_behavior_ == InitMarkerBehavior::kRequired;
  }

  bool optional_init_markers() {
    return init_marker_behavior_ == InitMarkerBehavior::kOptional;
  }

  DocWriteBatchCache cache_;

  DocDB doc_db_;

  InitMarkerBehavior init_marker_behavior_;
  std::atomic<int64_t>* monotonic_counter_;
  std::vector<std::pair<std::string, std::string>> put_batch_;

  // Taken from internal_doc_iterator
  KeyBytes key_prefix_;
  bool subdoc_exists_ = true;
  DocWriteBatchCache::Entry current_entry_;
};

// Converts a RocksDB WriteBatch to a string.
// line_prefix is the prefix to be added to each line of the result. Could be used for indentation.
Result<std::string> WriteBatchToString(
    const rocksdb::WriteBatch& write_batch,
    StorageDbType storage_db_type,
    BinaryOutputFormat binary_output_format,
    WriteBatchOutputFormat batch_output_format,
    const std::string& line_prefix);

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_WRITE_BATCH_H

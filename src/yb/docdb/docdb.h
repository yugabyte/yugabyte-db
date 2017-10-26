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

#ifndef YB_DOCDB_DOCDB_H_
#define YB_DOCDB_DOCDB_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>
#include <boost/function.hpp>

#include "yb/rocksdb/db.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/doc_operation.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_write_batch_cache.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/internal_doc_iterator.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/shared_lock_manager_fwd.h"
#include "yb/docdb/value.h"
#include "yb/docdb/subdocument.h"

#include "yb/tablet/mvcc.h"
#include "yb/util/status.h"

// Document DB mapping on top of the key-value map in RocksDB:
// <document_key> <hybrid_time> -> <doc_type>
// <document_key> <hybrid_time> <key_a> <gen_ts_a> -> <subdoc_a_type_or_value>
//
// Assuming the type of subdocument corresponding to key_a in the above example is "object", the
// contents of that subdocument are stored in a similar way:
// <document_key> <hybrid_time> <key_a> <gen_ts_a> <key_aa> <gen_ts_aa> -> <subdoc_aa_type_or_value>
// <document_key> <hybrid_time> <key_a> <gen_ts_a> <key_ab> <gen_ts_ab> -> <subdoc_ab_type_or_value>
// ...
//
// See doc_key.h for the encoding of the <document_key> part.
//
// <key_a>, <key_aa> are subkeys indicating a path inside a document.
// Their encoding is as follows:
//   <value_type> -- one byte, see the ValueType enum.
//   <value_specific_encoding> -- e.g. a big-endian 8-byte integer, or a string in a "zero encoded"
//                                format. This is empty for null or true/false values.
//
// <hybrid_time>, <gen_ts_a>, <gen_ts_ab> are "generation hybrid_times" corresponding to hybrid
// clock hybrid_times of the last time a particular top-level document / subdocument was fully
// overwritten or deleted.
//
// <subdoc_a_type_or_value>, <subdoc_aa_type_or_value>, <subdoc_ab_type_or_value> are values of the
// following form:
//   - One-byte value type (see the ValueType enum below).
//   - For primitive values, the encoded value. Note: the value encoding may be different from the
//     key encoding for the same data type. E.g. we only flip the sign bit for signed 64-bit
//     integers when encoded as part of a RocksDB key, not value.
//
// Also see this document for a high-level overview of how we lay out JSON documents on top of
// RocksDB:
// https://docs.google.com/document/d/1uEOHUqGBVkijw_CGD568FMt8UOJdHtiE3JROUOppYBU/edit

namespace yb {

class Histogram;

namespace docdb {

enum class InitMarkerBehavior {
  REQUIRED = 0,
  OPTIONAL = 1
};

// Used for extending a list:
enum class ListExtendOrder {
  APPEND = 0,
  PREPEND = 1
};

// The DocWriteBatch class is used to build a RocksDB write batch for a DocDB batch of operations
// that may include a mix or write (set) or delete operations. It may read from RocksDB while
// writing, and builds up an internal rocksdb::WriteBatch while handling the operations.
// When all the operations are applied, the rocksdb::WriteBatch should be taken as output.
// Take ownership of it using std::move if it needs to live longer than this DocWriteBatch.
class DocWriteBatch {
 public:
  explicit DocWriteBatch(rocksdb::DB* rocksdb, std::atomic<int64_t>* monotonic_counter = nullptr);

  // Set the primitive at the given path to the given value. Intermediate subdocuments are created
  // if necessary and possible.
  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path, const Value& value,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::REQUIRED);

  CHECKED_STATUS SetPrimitive(
      const DocPath& doc_path,
      const PrimitiveValue& value,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::REQUIRED) {
    return SetPrimitive(doc_path, Value(value), use_init_marker);
  }

  // Extend the SubDocument in the given key. We'll support List with Append and Prepend mode later.
  // TODO(akashnil): 03/20/17 ENG-1107
  // In each SetPrimitive call, some common work is repeated. It may be made more
  // efficient by not calling SetPrimitive internally.
  CHECKED_STATUS ExtendSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL,
      MonoDelta ttl = Value::kMaxTtl);

  CHECKED_STATUS InsertSubDocument(
      const DocPath& doc_path,
      const SubDocument& value,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL,
      MonoDelta ttl = Value::kMaxTtl);

  CHECKED_STATUS ExtendList(
      const DocPath& doc_path,
      const SubDocument& value,
      ListExtendOrder extend_order = ListExtendOrder::APPEND,
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::OPTIONAL,
      MonoDelta ttl = Value::kMaxTtl);

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
      InitMarkerBehavior use_init_marker = InitMarkerBehavior::REQUIRED);

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

  DocWriteBatchCache cache_;

  rocksdb::DB* rocksdb_;
  std::atomic<int64_t>* monotonic_counter_;
  std::vector<std::pair<std::string, std::string>> put_batch_;

  int num_rocksdb_seeks_;
};

// This function prepares the transaction by taking locks. The set of keys locked are returned to
// the caller via the keys_locked argument (because they need to be saved and unlocked when the
// transaction commits). A flag is also returned to indicate if any of the write operations
// requires a clean read snapshot to be taken before being applied (see DocOperation for details).
//
// Example: doc_write_ops might consist of the following operations:
// a.b = {}, a.b.c = 1, a.b.d = 2, e.d = 3
// We will generate all the lock_prefixes for the keys with lock types
// a - shared, a.b - exclusive, a - shared, a.b - shared, a.b.c - exclusive ...
// Then we will deduplicate the keys and promote shared locks to exclusive, and sort them.
// Finally, the locks taken will be in order:
// a - shared, a.b - exclusive, a.b.c - exclusive, a.b.d - exclusive, e - shared, e.d - exclusive.
// Then the sorted lock key list will be returned. (Type is not returned because it is not needed
// for unlocking)
// TODO(akashnil): If a.b is exclusive, we don't need to lock any sub-paths under it.
//
// Input: doc_write_ops
// Context: lock_manager
// Outputs: write_batch, need_read_snapshot
void PrepareDocWriteOperation(const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
                              SharedLockManager *lock_manager,
                              LockBatch *keys_locked,
                              bool *need_read_snapshot,
                              const scoped_refptr<Histogram>& write_lock_latency);

// This function reads from rocksdb and constructs the write batch.
//
// Input: doc_write_ops, read snapshot hybrid_time if requested in PrepareDocWriteOperation().
// Context: rocksdb
// Outputs: keys_locked, write_batch
Status ApplyDocWriteOperation(const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
                              const HybridTime& hybrid_time,
                              rocksdb::DB *rocksdb,
                              KeyValueWriteBatchPB* write_batch,
                              std::atomic<int64_t>* monotonic_counter);

void PrepareNonTransactionWriteBatch(
    const docdb::KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::WriteBatch* rocksdb_write_batch);

// Enumerates intents corresponding to provided key value pairs.
// For each key in generates a strong intent and for each parent of each it generates a weak one.
// functor should accept 3 arguments:
// intent_kind - kind of intent weak or strong
// value_slice - value of intent
// key - pointer to key in format kIntentPrefix + SubDocKey (no ht)
// TODO(dtxn) don't expose this method outside of DocDB if TransactionConflictResolver is moved
// inside DocDB.
// Note: From https://stackoverflow.com/a/17278470/461529:
// "As of GCC 4.8.1, the std::function in libstdc++ optimizes only for pointers to functions and
// methods. So regardless the size of your functor (lambdas included), initializing a std::function
// from it triggers heap allocation."
// So, we use boost::function which doesn't have such issue:
// http://www.boost.org/doc/libs/1_65_1/doc/html/function/misc.html
CHECKED_STATUS EnumerateIntents(
    const google::protobuf::RepeatedPtrField<yb::docdb::KeyValuePairPB> &kv_pairs,
    boost::function<Status(IntentKind, Slice, KeyBytes*)> functor);

void PrepareTransactionWriteBatch(
    const docdb::KeyValueWriteBatchPB& put_batch,
    HybridTime hybrid_time,
    rocksdb::WriteBatch* rocksdb_write_batch,
    const TransactionId& transaction_id,
    IsolationLevel isolation_level);

// A visitor class that could be overridden to consume results of scanning SubDocuments.
// See e.g. SubDocumentBuildingVisitor (used in implementing GetSubDocument) as example usage.
// We can scan any SubDocument from a node in the document tree.
class DocVisitor {
 public:
  DocVisitor() {}
  virtual ~DocVisitor() {}

  // Called once in the beginning of every new subdocument.
  virtual CHECKED_STATUS StartSubDocument(const SubDocKey &key) = 0;

  // Called in the end of a document.
  virtual CHECKED_STATUS EndSubDocument() = 0;

  // VisitKey and VisitValue are called as part of enumerating key-value pairs in an object, e.g.
  // VisitKey(key1), VisitValue(value1), VisitKey(key2), VisitValue(value2), etc.

  virtual CHECKED_STATUS VisitKey(const PrimitiveValue& key) = 0;
  virtual CHECKED_STATUS VisitValue(const PrimitiveValue& value) = 0;

  // Called in the beginning of an object, before any key/value pairs.
  virtual CHECKED_STATUS StartObject() = 0;

  // Called after all key/value pairs in an object.
  virtual CHECKED_STATUS EndObject() = 0;

  // Called before enumerating elements of an array. Not used as of 9/26/2016.
  virtual CHECKED_STATUS StartArray() = 0;

  // Called after enumerating elements of an array. Not used as of 9/26/2016.
  virtual CHECKED_STATUS EndArray() = 0;
};

// Returns the whole SubDocument below some node identified by subdocument_key.
// subdocument_key should not have a timestamp.
// Before the function is called, if is_iter_valid == true, the iterator is expected to be
// positioned on or before the first key.
// After this, the iter should be positioned just outside the SubDocument (unless high_subkey is
// specified, see below for details).
// This function works with or without object init markers present.
// If tombstone and other values are inserted at the same timestamp, it results in undefined
// behavior. TODO: We should have write-id's to make sure timestamps are always unique.
// The projection, if set, restricts the scan to a subset of keys in the first level.
// The projection is used for QL selects to get only a subset of columns.
// If low and high subkey are specified, only first level keys in the subdocument within that
// range(inclusive) are returned and the iterator is positioned after high_subkey and not
// necessarily outside the SubDocument.
yb::Status GetSubDocument(
    IntentAwareIterator *db_iter,
    const SubDocKey& subdocument_key,
    SubDocument *result,
    bool *doc_found,
    HybridTime scan_ts = HybridTime::kMax,
    MonoDelta table_ttl = Value::kMaxTtl,
    const std::vector<PrimitiveValue>* projection = nullptr,
    bool return_type_only = false,
    const bool is_iter_valid = true,
    const PrimitiveValue& low_subkey = PrimitiveValue(ValueType::kInvalidValueType),
    const PrimitiveValue& high_subkey = PrimitiveValue(ValueType::kInvalidValueType));

// This version of GetSubDocument creates a new iterator every time. This is not recommended for
// multiple calls to subdocs that are sequential or near each other, in eg. doc_rowwise_iterator.
// low_subkey and high_subkey are optional ranges that we can specify for the subkeys to ensure
// that we include only a particular set of subkeys for the first level of the subdocument that
// we're looking for.
yb::Status GetSubDocument(
    rocksdb::DB* db,
    const SubDocKey& subdocument_key,
    const rocksdb::QueryId query_id,
    const TransactionOperationContextOpt& txn_op_context,
    SubDocument* result,
    bool* doc_found,
    HybridTime scan_ts = HybridTime::kMax,
    MonoDelta table_ttl = Value::kMaxTtl,
    bool return_type_only = false,
    const PrimitiveValue& low_subkey = PrimitiveValue(ValueType::kInvalidValueType),
    const PrimitiveValue& high_subkey = PrimitiveValue(ValueType::kInvalidValueType));

// Create a debug dump of the document database. Tries to decode all keys/values despite failures.
// Reports all errors to the output stream and returns the status of the first failed operation,
// if any.
yb::Status DocDBDebugDump(rocksdb::DB* rocksdb, std::ostream& out, bool include_binary = false);

std::string DocDBDebugDumpToStr(rocksdb::DB* rocksdb, bool include_binary = false);

void ConfigureDocDBRocksDBOptions(rocksdb::Options* options);

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, docdb::KeyBytes* out);

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOCDB_H_

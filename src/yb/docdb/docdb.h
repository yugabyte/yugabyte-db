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
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/doc_operation.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/doc_write_batch_cache.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/internal_doc_iterator.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/shared_lock_manager_fwd.h"
#include "yb/docdb/value.h"
#include "yb/docdb/subdocument.h"

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
                              const scoped_refptr<Histogram>& write_lock_latency,
                              IsolationLevel isolation_level,
                              SharedLockManager *lock_manager,
                              LockBatch *keys_locked,
                              bool *need_read_snapshot);

// This constructs a DocWriteBatch using the given list of DocOperations, reading the previous
// state of data from RocksDB when necessary.
//
// Input: doc_write_ops, read snapshot hybrid_time if requested in PrepareDocWriteOperation().
// Context: rocksdb
// Outputs: keys_locked, write_batch
// TODO: rename this to something other than "apply" to avoid confusing it with the "apply"
// operation that happens after Raft replication.
CHECKED_STATUS ExecuteDocWriteOperation(
    const std::vector<std::unique_ptr<DocOperation>>& doc_write_ops,
    const ReadHybridTime& read_time,
    rocksdb::DB *rocksdb,
    KeyValueWriteBatchPB* write_batch,
    InitMarkerBehavior init_marker_behavior,
    std::atomic<int64_t>* monotonic_counter,
    HybridTime* restart_read_ht);

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

// Represents a general SubDocKey with information on whether this bound is lower/upper and whether
// it is exclusive. Used in range requests.
class SubDocKeyBound : public SubDocKey {
 public:
  SubDocKeyBound()
      : is_exclusive_(false),
        is_lower_bound_(false) {
  }

  SubDocKeyBound(const SubDocKey& subdoc_key,
                 bool is_exclusive,
                 bool is_lower_bound)
      : SubDocKey(subdoc_key),
        is_exclusive_(is_exclusive),
        is_lower_bound_(is_lower_bound) {
  }

  // Indicates whether or not the given PrimitiveValue can be included as part of this bound.
  bool CanInclude(const SubDocKey& other) const {
    if (is_lower_bound_) {
      return (is_exclusive_) ? (*this < other) : (*this <= other);
    } else {
      return (is_exclusive_) ? (*this > other) : (*this >= other);
    }
  }

  bool is_exclusive() const {
    return is_exclusive_;
  }

  static const SubDocKeyBound& Empty();

 private:
  const bool is_exclusive_;
  const bool is_lower_bound_;
};

class IndexBound {
 public:
  IndexBound() :
      index_(-1),
      is_exclusive_(false),
      is_lower_bound_(false) {}

  IndexBound(int64 index, bool is_exclusive, bool is_lower_bound) :
      index_(index),
      is_exclusive_(is_exclusive),
      is_lower_bound_(is_lower_bound) {}

  bool CanInclude(int64 curr_index) const {
    if (index_ == -1 ) {
      return true;
    }
    if (is_lower_bound_) {
      return (is_exclusive_) ? (index_ < curr_index) : (index_ <= curr_index);
    } else {
      return (is_exclusive_) ? (index_ > curr_index) : (index_ >= curr_index);
    }
  }

  static const IndexBound& Empty();

 private:
  const int64 index_;
  const bool is_exclusive_;
  const bool is_lower_bound_;
};

// Pass data to GetSubDocument function.
struct GetSubDocumentData {
  GetSubDocumentData(
      const SubDocKey* subdoc_key, SubDocument* result_, bool* doc_found_ = nullptr)
      : subdocument_key(subdoc_key), result(result_), doc_found(doc_found_) {}

  const SubDocKey* subdocument_key;
  SubDocument* result;
  bool* doc_found;

  MonoDelta table_ttl = Value::kMaxTtl;
  bool return_type_only = false;
  // Represent bounds on the first and last subkey to be considered.
  const SubDocKeyBound* low_subkey = &SubDocKeyBound::Empty();
  const SubDocKeyBound* high_subkey = &SubDocKeyBound::Empty();
  // Represent bounds on the first and last ranks to be considered.
  const IndexBound* low_index = &IndexBound::Empty();
  const IndexBound* high_index = &IndexBound::Empty();

  GetSubDocumentData Adjusted(
      const SubDocKey* subdoc_key, SubDocument* result_, bool* doc_found_ = nullptr) const {
    GetSubDocumentData result(subdoc_key, result_, doc_found_);
    result.table_ttl = table_ttl;
    result.return_type_only = return_type_only;
    result.low_subkey = low_subkey;
    result.high_subkey = high_subkey;
    result.low_index = low_index;
    result.high_index = high_index;
    return result;
  }

  std::string ToString() const {
    return Format("{ subdocument_key: $0 table_ttl: $1 return_type_only: $2 low_subkey: $3 "
                      "high_subkey: $4 }",
                  *subdocument_key, table_ttl, return_type_only, *low_subkey, *high_subkey);
  }
};

inline std::ostream& operator<<(std::ostream& out, const GetSubDocumentData& data) {
  return out << data.ToString();
}

// Returns the whole SubDocument below some node identified by subdocument_key.
// subdocument_key should not have a timestamp.
// Before the function is called, if is_iter_valid == true, the iterator is expected to be
// positioned on or before the first key.
// After this, the iter should be positioned just outside the SubDocument (unless high_subkey is
// specified, see below for details).
// This function works with or without object init markers present.
// If tombstone and other values are inserted at the same timestamp, it results in undefined
// behavior.
// The projection, if set, restricts the scan to a subset of keys in the first level.
// The projection is used for QL selects to get only a subset of columns.
// If low and high subkey are specified, only first level keys in the subdocument within that
// range(inclusive) are returned and the iterator is positioned after high_subkey and not
// necessarily outside the SubDocument.
yb::Status GetSubDocument(
    IntentAwareIterator *db_iter,
    const GetSubDocumentData& data,
    const std::vector<PrimitiveValue>* projection = nullptr,
    const bool is_iter_valid = true);

// This version of GetSubDocument creates a new iterator every time. This is not recommended for
// multiple calls to subdocs that are sequential or near each other, in eg. doc_rowwise_iterator.
// low_subkey and high_subkey are optional ranges that we can specify for the subkeys to ensure
// that we include only a particular set of subkeys for the first level of the subdocument that
// we're looking for.
yb::Status GetSubDocument(
    rocksdb::DB* db,
    const GetSubDocumentData& data,
    const rocksdb::QueryId query_id,
    const TransactionOperationContextOpt& txn_op_context,
    const ReadHybridTime& read_time = ReadHybridTime::Max());

YB_STRONGLY_TYPED_BOOL(IncludeBinary);

// Create a debug dump of the document database. Tries to decode all keys/values despite failures.
// Reports all errors to the output stream and returns the status of the first failed operation,
// if any.
void DocDBDebugDump(
    rocksdb::DB* rocksdb, std::ostream& out, IncludeBinary include_binary = IncludeBinary::kFalse);

std::string DocDBDebugDumpToStr(
    rocksdb::DB* rocksdb, IncludeBinary include_binary = IncludeBinary::kFalse);

void ConfigureDocDBRocksDBOptions(rocksdb::Options* options);

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, docdb::KeyBytes* out);

// Buffer for encoding DocHybridTime
class DocHybridTimeBuffer {
 public:
  DocHybridTimeBuffer();

  Slice EncodeWithValueType(const DocHybridTime& doc_ht);

  Slice EncodeWithValueType(HybridTime ht, IntraTxnWriteId write_id) {
    return EncodeWithValueType(DocHybridTime(ht, write_id));
  }
 private:
  std::array<char, 1 + kMaxBytesPerEncodedHybridTime> buffer_;
};


}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOCDB_H_

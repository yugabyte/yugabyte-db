// Copyright (c) YugaByte, Inc.

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/redis_protocol.pb.h"
#include "yb/common/timestamp.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/internal_doc_iterator.h"
#include "yb/docdb/value.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value_type.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/server/hybrid_clock.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/docdb/docdb_compaction_filter.h"

using std::endl;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::shared_ptr;
using std::stack;
using std::vector;
using std::make_shared;

using yb::Timestamp;
using yb::util::FormatBytesAsStr;
using yb::util::LockType;
using yb::FormatRocksDBSliceAsStr;
using strings::Substitute;


namespace yb {
namespace docdb {

namespace {
// This a zero-terminated string for safety, even though we only intend to use one byte.
const char kObjectValueType[] = { static_cast<char>(ValueType::kObject), 0 };

}  // namespace


Status StartDocWriteTransaction(rocksdb::DB *rocksdb,
                                vector<unique_ptr<DocOperation>>* doc_write_ops,
                                util::SharedLockManager *lock_manager,
                                vector<string> *keys_locked,
                                KeyValueWriteBatchPB* dest) {
  unordered_map<string, LockType> lock_type_map;
  for (const unique_ptr<DocOperation>& doc_op : *doc_write_ops) {
    // TODO(akashnil): avoid materializing the vector, do the work of the called function in place.
    const vector<string> doc_op_locks = doc_op->DocPathToLock().GetLockPrefixKeys();
    assert(doc_op_locks.size() > 0);
    for (int i = 0; i < doc_op_locks.size(); i++) {
      auto iter = lock_type_map.find(doc_op_locks[i]);
      if (iter == lock_type_map.end()) {
        lock_type_map[doc_op_locks[i]] = LockType::SHARED;
        keys_locked->push_back(doc_op_locks[i]);
      }
    }
    lock_type_map[doc_op_locks[doc_op_locks.size() - 1]] = LockType::EXCLUSIVE;
  }
  // Sort the set of locks to be taken for this transaction, to make sure deadlocks don't occur.
  std::sort(keys_locked->begin(), keys_locked->end());
  for (string key : *keys_locked) {
    lock_manager->Lock(key, lock_type_map[key]);
  }
  DocWriteBatch doc_write_batch(rocksdb);
  for (const unique_ptr<DocOperation>& doc_op : *doc_write_ops) {
    RETURN_NOT_OK(doc_op->Apply(&doc_write_batch));
  }
  doc_write_batch.MoveToWriteBatchPB(dest);
  return Status::OK();
}

Status HandleRedisReadTransaction(rocksdb::DB *rocksdb,
                                  const vector<unique_ptr<RedisReadOperation>>& doc_read_ops,
                                  Timestamp timestamp) {
  for (const unique_ptr<RedisReadOperation>& doc_op : doc_read_ops) {
    RETURN_NOT_OK(doc_op->Execute(rocksdb, timestamp));
  }
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// DocWriteBatch
// ------------------------------------------------------------------------------------------------

DocWriteBatch::DocWriteBatch(rocksdb::DB* rocksdb)
    : rocksdb_(rocksdb),
      num_rocksdb_seeks_(0) {
}

// This codepath is used to handle both primitive upserts and deletions.
Status DocWriteBatch::SetPrimitive(const DocPath& doc_path,
                                   const Value& value,
                                   Timestamp timestamp) {
  DOCDB_DEBUG_LOG("Called with doc_path=$0, value=$1, timestamp=$2",
                  doc_path.ToString(), value.ToString(), timestamp.ToDebugString());
  const KeyBytes& encoded_doc_key = doc_path.encoded_doc_key();
  const int num_subkeys = doc_path.num_subkeys();
  const bool is_deletion = value.primitive_value().value_type() == ValueType::kTombstone;

  InternalDocIterator doc_iter(rocksdb_, &cache_, &num_rocksdb_seeks_);

  if (num_subkeys > 0 || is_deletion) {
    // Navigate to the root of the document. We don't yet know whether the document exists or when
    // it was last updated.
    RETURN_NOT_OK(doc_iter.SeekToDocument(encoded_doc_key));
    DOCDB_DEBUG_LOG("Top-level document exists: $0", doc_iter.subdoc_exists());
    if (!doc_iter.subdoc_exists() & is_deletion) {
      DOCDB_DEBUG_LOG("We're performing a deletion, and the document is not present. "
                      "Nothing to do.");
      return Status::OK();
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

  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    if (subkey.value_type() == ValueType::kArrayIndex) {
      return STATUS(NotSupported, "Setting values at a given array index is not supported yet");
    }
    if (doc_iter.subdoc_exists()) {
      if (doc_iter.subdoc_type() != ValueType::kObject) {
        return STATUS_SUBSTITUTE(IllegalState,
            "Cannot set values inside a subdocument of type $0",
            ValueTypeToStr(doc_iter.subdoc_type()));
      }
      if (subkey_index == num_subkeys - 1 && !is_deletion) {
        // We don't need to perform a RocksDB read at the last level for upserts, we just
        // overwrite the value within the last subdocument with what we're trying to write.
        // We still perform the read for deletions, because we try to avoid writing a new tombstone
        // if the data is not there anyway.
        doc_iter.AppendSubkeyInExistingSubDoc(subkey);
      } else {
        // We need to check if the subdocument at this subkey exists.
        RETURN_NOT_OK(doc_iter.SeekToSubDocument(subkey));
        if (is_deletion && !doc_iter.subdoc_exists()) {
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
      KeyBytes parent_key(doc_iter.key_prefix());
      parent_key.AppendValueType(ValueType::kTimestamp);
      parent_key.AppendTimestamp(timestamp);
      // TODO: std::move from parent_key's string buffer into put_batch_.
      put_batch_.emplace_back(parent_key.AsStringRef(), kObjectValueType);

      // Update our local cache to record the fact that we're adding this subdocument, so that
      // future operations in this DocWriteBatch don't have to add it or look for it in RocksDB.
      cache_.Put(KeyBytes(doc_iter.key_prefix().AsSlice()), timestamp, ValueType::kObject);

      doc_iter.AppendToPrefix(subkey);
    }
  }

  // The key we use in the DocWriteBatchCache does not have a final timestamp, because that's the
  // key we expect to look up.
  cache_.Put(doc_iter.key_prefix(), timestamp, value.primitive_value().value_type());

  // Close the group of subkeys of the SubDocKey, and append the timestamp as the final component.
  doc_iter.mutable_key_prefix()->AppendValueType(ValueType::kTimestamp);
  doc_iter.AppendTimestampToPrefix(timestamp);

  put_batch_.emplace_back(doc_iter.key_prefix().AsStringRef(), value.Encode());

  return Status::OK();
}

Status DocWriteBatch::DeleteSubDoc(const DocPath& doc_path, const Timestamp timestamp) {
  return SetPrimitive(doc_path, PrimitiveValue(ValueType::kTombstone), timestamp);
}

string DocWriteBatch::ToDebugString() {
  WriteBatchFormatter formatter;
  rocksdb::WriteBatch rocksdb_write_batch;
  PopulateRocksDBWriteBatchInTest(&rocksdb_write_batch);
  rocksdb::Status iteration_status = rocksdb_write_batch.Iterate(&formatter);
  CHECK(iteration_status.ok());
  return formatter.str();
}

void DocWriteBatch::Clear() {
  put_batch_.clear();
  cache_.Clear();
}

void DocWriteBatch::PopulateRocksDBWriteBatchInTest(rocksdb::WriteBatch *rocksdb_write_batch,
                                                    Timestamp timestamp) const {
  for (const auto& entry : put_batch_) {
    SubDocKey subdoc_key;
    // We don't expect any invalid encoded keys in the write batch.
    CHECK_OK_PREPEND(subdoc_key.FullyDecodeFrom(entry.first),
                     Substitute("when decoding key: $0", FormatBytesAsStr(entry.first)));
    if (timestamp != Timestamp::kMax) {
      subdoc_key.ReplaceMaxTimestampWith(timestamp);
    }

    rocksdb_write_batch->Put(subdoc_key.Encode().AsSlice(), entry.second);
  }
}

rocksdb::Status DocWriteBatch::WriteToRocksDBInTest(
    const Timestamp timestamp,
    const rocksdb::WriteOptions &write_options) const {
  if (IsEmpty()) {
    return rocksdb::Status::OK();
  }

  rocksdb::WriteBatch rocksdb_write_batch;
  PopulateRocksDBWriteBatchInTest(&rocksdb_write_batch, timestamp);
  return rocksdb_->Write(write_options, &rocksdb_write_batch);
}

void DocWriteBatch::MoveToWriteBatchPB(KeyValueWriteBatchPB *kv_pb) {
  for (auto& entry : put_batch_) {
    KeyValuePairPB* kv_pair = kv_pb->add_kv_pairs();
    kv_pair->mutable_key()->swap(entry.first);
    kv_pair->mutable_value()->swap(entry.second);
  }
}

int DocWriteBatch::GetAndResetNumRocksDBSeeks() {
  const int ret_val = num_rocksdb_seeks_;
  num_rocksdb_seeks_ = 0;
  return ret_val;
}

void DocWriteBatch::CheckBelongsToSameRocksDB(const rocksdb::DB* const rocksdb) const {
  CHECK_EQ(rocksdb, rocksdb_);
}


// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

namespace {

static yb::Status ScanSubDocument(const SubDocKey& higher_level_key,
                                  rocksdb::Iterator* rocksdb_iter,
                                  DocVisitor* visitor,
                                  Timestamp scan_ts,
                                  Timestamp lowest_ts);

// @param higher_level_key
//     The SubDocKey corresponding to the root of the subdocument tree to scan. This is the expected
//     to be the parsed version of the key/value pair the RocksDB iterator is currently positioned
//     at.
//
// @param rocksdb_iter
//     A RocksDB iterator that is expected to be positioned at the "root" key/value pair of the
//     subdocument or primitive value we are trying to scan.
//
// @param scan_ts The timestamp we are trying to scan the state of the database at.
// @param lowest_ts
//     The lowest timestamp that we need to consider while walking the nested document tree. This is
//     based on init markers or tombstones seen at higher levels. E.g. if we've already seen an init
//     marker at timestamp t, then lowest_ts will be t, or if we've seen a tombstone at timestamp t,
//     lowest_ts will be t + 1 (because that's the earliest timestamp we need to care about after
//     a deletion at timestamp t). The latter case is only relevant for the case when optional
//     init markers are enabled, which is not supported as of 12/06/2016.
static yb::Status ScanPrimitiveValueOrObject(const SubDocKey& higher_level_key,
                                             rocksdb::Iterator* rocksdb_iter,
                                             DocVisitor* visitor,
                                             Timestamp scan_ts,
                                             Timestamp lowest_ts) {
  DOCDB_DEBUG_LOG("higher_level_key=$0, scan_ts=$1, lowest_ts=$2",
                  higher_level_key.ToString(), scan_ts.ToDebugString(), lowest_ts.ToDebugString());
  DCHECK_LE(higher_level_key.timestamp(), scan_ts)
      << "; higher_level_key=" << higher_level_key.ToString();
  DCHECK_GE(higher_level_key.timestamp(), lowest_ts)
      << "; higher_level_key=" << higher_level_key.ToString();

  Value top_level_value;
  RETURN_NOT_OK(top_level_value.Decode(rocksdb_iter->value()));

  if (top_level_value.primitive_value().value_type() == ValueType::kTombstone) {
    // TODO(mbautin): If we allow optional init markers here, we still need to scan deeper levels
    // and interpret values with timestamps >= lowest_ts as values in a map that exists. As of
    // 11/11/2016 initialization markers (i.e. ValueType::kObject) are still required at the top of
    // each object's key range.
    return Status::OK();  // the document does not exist
  }

  if (top_level_value.primitive_value().IsPrimitive()) {
    RETURN_NOT_OK(visitor->VisitValue(top_level_value.primitive_value()));
    return Status::OK();
  }

  if (top_level_value.primitive_value().value_type() == ValueType::kObject) {
    rocksdb_iter->Next();
    RETURN_NOT_OK(visitor->StartObject());

    KeyBytes deeper_level_key(higher_level_key.Encode(/* include_timestamp = */ false));
    deeper_level_key.AppendValueType(kMinPrimitiveValueType);
    ROCKSDB_SEEK(rocksdb_iter, deeper_level_key.AsSlice());

    RETURN_NOT_OK(ScanSubDocument(higher_level_key, rocksdb_iter, visitor, scan_ts,
                                  higher_level_key.timestamp()));
    RETURN_NOT_OK(visitor->EndObject());
    return Status::OK();
  }

  return STATUS_SUBSTITUTE(Corruption, "Invalid value type at the top level of a document: $0",
                           ValueTypeToStr(top_level_value.primitive_value().value_type()));
}

// @param higher_level_key
//     The top-level key of the document we are scanning.
//
// @param rocksdb_iter
//     A RocksDB iterator positioned at the first key/value to scan one level below the top of the
//     document.
//
//     Example:
//
//     SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [TS(1000)]) -> "value1"
//     SubDocKey(DocKey([], ["mydockey", 123456]), [TS(2000)]) -> {}
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a", TS(2000)]) -> "value_a"
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", TS(7000)]) -> {}
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", TS(6000)]) -> DEL
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", TS(3000)]) -> {}
//  => SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c", TS(7000)]) -> "v_bc2"
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c", TS(5000)]) -> DEL
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c", TS(3000)]) -> "v_bc"
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d", TS(3500)]) -> "v_bd"
//
//     The following example corresponds to higher_level_key equal to
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", TS(7000)])
static yb::Status ScanSubDocument(const SubDocKey& higher_level_key,
                                  rocksdb::Iterator* const rocksdb_iter,
                                  DocVisitor* const visitor,
                                  Timestamp scan_ts,
                                  Timestamp lowest_ts) {
  DOCDB_DEBUG_LOG("higher_level_key=$0, scan_ts=$1, lowest_ts=$2",
                  higher_level_key.ToString(), scan_ts.ToDebugString(), lowest_ts.ToDebugString());
  SubDocKey higher_level_key_no_ts(higher_level_key);
  higher_level_key_no_ts.RemoveTimestamp();

  while (rocksdb_iter->Valid()) {
    SubDocKey subdoc_key;
    RETURN_NOT_OK(subdoc_key.FullyDecodeFrom(rocksdb_iter->key()));
    if (!subdoc_key.StartsWith(higher_level_key_no_ts)) {
      // We have reached the end of the subdocument we are trying to scan. This could also be the
      // end of the entire document.
      //
      //    1. SubDocKey(<doc_key1>, [TS(2000)]) -> {}
      //    2. SubDocKey(<doc_key1>, ["subkey_a", TS(2000)]) -> "value_a"
      // => 3. SubDocKey(<doc_key2>, [TS(3000)]) -> {}
      //
      // In the above example, higher_level_key = SubDocKey(<doc_key1>, [TS(2000)]), and
      // higher_level_key_no_ts = SubDocKey(<doc_key1>), so key/value pair #2 is still part of the
      // document, because it has the right prefix, while key/value pair #3 is not.
      break;
    }
    if (subdoc_key.timestamp() > scan_ts) {
      // This entry is still in the future compared to our scan timestamp. Adjust the timestamp
      // and try again.
      subdoc_key.set_timestamp(scan_ts);
      ROCKSDB_SEEK(rocksdb_iter, subdoc_key.Encode().AsSlice());
      continue;
    }
    DOCDB_DEBUG_LOG("subdoc_key=$0", subdoc_key.ToString());

    if (subdoc_key.num_subkeys() == higher_level_key.num_subkeys() + 2) {
      // We must have exhausted all the top-level subkeys of the document, and reached the section
      // containing sub-sub-keys.
      // TODO: what happens if we enable optional object init markers?
      break;
    }

    if (subdoc_key.num_subkeys() != higher_level_key.num_subkeys() + 1) {
      static const char* kErrorMsgPrefix =
          "A subdocument key must be nested exactly one level under the parent subdocument";
      // Log more details in debug mode.
      DLOG(WARNING) << Substitute(
          "$0.\nParent subdocument key:\n$1\nSubdocument key:\n$2",
          kErrorMsgPrefix, higher_level_key.ToString(), subdoc_key.ToString());
      return STATUS(Corruption, kErrorMsgPrefix);
    }

    if (subdoc_key.timestamp() >= lowest_ts &&
        DecodeValueType(rocksdb_iter->value()) != ValueType::kTombstone) {
      RETURN_NOT_OK(visitor->VisitKey(subdoc_key.last_subkey()));
      ScanPrimitiveValueOrObject(
          subdoc_key, rocksdb_iter, visitor, scan_ts, subdoc_key.timestamp());
    }

    // Get out of the subdocument we've just scanned and go to the next one.
    ROCKSDB_SEEK(rocksdb_iter, subdoc_key.AdvanceOutOfSubDoc().AsSlice());
  }
  return Status::OK();
}

}  // anonymous namespace

yb::Status ScanDocument(rocksdb::DB* rocksdb,
                        const KeyBytes& document_key,
                        DocVisitor* visitor,
                        Timestamp scan_ts) {
  auto rocksdb_iter = CreateRocksDBIterator(rocksdb);

  // TODO: Use a SubDocKey API to build the proper seek key without assuming anything about the
  //       internal structure of SubDocKey here.
  KeyBytes seek_key(document_key);
  seek_key.AppendValueType(ValueType::kTimestamp);
  seek_key.AppendTimestamp(scan_ts);

  ROCKSDB_SEEK(rocksdb_iter.get(), seek_key.AsSlice());
  if (!rocksdb_iter->Valid() || !document_key.IsPrefixOf(rocksdb_iter->key())) {
    // Suppose we seek to (<document_key>, <scan_timestamp>). We will most likely see something
    // like (<document_key>, <timestamp_lower_than_scan_timestamp>), so we only only expect the
    // original document_key to be the prefix of the actual RocksDB key we see, but we don't expect
    // the actual key to exactly match seek_key.
    return Status::OK();
  }

  SubDocKey doc_key;
  RETURN_NOT_OK(doc_key.FullyDecodeFrom(rocksdb_iter->key()));
  if (doc_key.num_subkeys() > 0) {
    // This could happen when we are trying to scan at an old timestamp at which the document does
    // not exist yet. In that case we'll jump directly into the section of the RocksDB key space
    // that contains deeper levels of the document.

    // Example (from a real test failure)
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Suppose the above RocksDB seek is as follows:
    //
    //        Seek key:       SubDocKey(DocKey([], [-7805187538405744458, false]), [TS(3)])
    //        Seek key (raw): "I\x13\xaegI\x98\x05 \xb6F!#\xff\xff\xff\xff\xff\xff\xff\xfc"
    //        Actual key:     SubDocKey(DocKey([], [-7805187538405744458, false]), [true; TS(4)])
    //        Actual value:   "{"
    //
    // and the relevant part of the RocksDB state is as follows (as SubDocKey and binary):
    //
    // SubDocKey(DocKey([], [-7805187538405744458, false]), [TS(4)]) -> {}
    // "I\x13\xaegI\x98\x05 \xb6F!#\xff\xff\xff\xff\xff\xff\xff\xfb" -> "{"
    //
    // SubDocKey(DocKey([], [-7805187538405744458, false]), [true; TS(4)]) -> {}  <--------------.
    // "I\x13\xaegI\x98\x05 \xb6F!T#\xff\xff\xff\xff\xff\xff\xff\xfb" -> "{"                     |
    //                                                                                           |
    // Then we'll jump directly here as we try to retrieve the document state at timestamp 3: ---/
    //
    // The right thing to do here is just to return, assuming the document does not exist.
    return Status::OK();
  }

  RETURN_NOT_OK(visitor->StartDocument(doc_key.doc_key()));
  RETURN_NOT_OK(ScanPrimitiveValueOrObject(
      doc_key, rocksdb_iter.get(), visitor, scan_ts, doc_key.timestamp()));
  RETURN_NOT_OK(visitor->EndDocument());

  return Status::OK();
}

namespace {

// This is used in the implementation of GetDocument. Builds a SubDocument from a stream of
// DocVisitor API calls made by the ScanDocument function.
class SubDocumentBuildingVisitor : public DocVisitor {
 public:
  SubDocumentBuildingVisitor() {}

  virtual ~SubDocumentBuildingVisitor() {}

  Status StartDocument(const DocKey& key) override {
    return Status::OK();
  }

  Status EndDocument() override {
    return Status::OK();
  }

  Status VisitKey(const PrimitiveValue& key) override {
    RETURN_NOT_OK(EnsureInsideObject(__func__));
    key_ = key;
    return Status::OK();
  }

  Status VisitValue(const PrimitiveValue& value) override {
    doc_found_ = true;
    if (stack_.empty()) {
      root_ = SubDocument(value);
      stack_.push(&root_);
    } else {
      auto top = stack_.top();
      const ValueType value_type = top->value_type();
      if (value_type == ValueType::kObject) {
        // TODO(mbautin): check for duplicate keys here.
        top->SetChildPrimitive(key_, value);
      } else {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Cannot set a value within a subdocument of type $0",
                                 ValueTypeToStr(value_type));
      }
    }
    return Status::OK();
  }

  Status StartObject() override {
    doc_found_ = true;
    if (stack_.empty()) {
      DCHECK_EQ(ValueType::kObject, root_.value_type());
      DCHECK_EQ(0, root_.object_num_keys());
      // root_ should already be initialized to an empty map.
      stack_.push(&root_);
    } else {
      auto top = stack_.top();
      const ValueType value_type = top->value_type();
      if (value_type == ValueType::kObject) {
        const auto new_subdoc_and_child_added = top->GetOrAddChild(key_);
        if (!new_subdoc_and_child_added.second) {
          // TODO(mbautin): include the duplicate key into status, ensuring the string
          //                representation is not too long.
          return STATUS_SUBSTITUTE(Corruption, "Duplicate key");
        }
        stack_.push(new_subdoc_and_child_added.first);
      } else {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Cannot set a value within a subdocument of type $0",
                                 ValueTypeToStr(value_type));
      }
    }
    return Status::OK();
  }

  Status EndObject() override {
    RETURN_NOT_OK(EnsureInsideObject(__func__));
    stack_.pop();
    return Status::OK();
  }

  Status StartArray() override {
    doc_found_ = true;
    LOG(FATAL) << __func__ << " not implemented yet";
    return Status::OK();
  }

  Status EndArray() override {
    LOG(FATAL) << __func__ << " not implemented yet";
    return Status::OK();
  }

  SubDocument ReleaseResult() {
    return std::move(root_);
  }

  bool doc_found() { return doc_found_; }

 private:
  Status EnsureInsideObject(const char* function_name) {
    if (stack_.empty()) {
      return STATUS_SUBSTITUTE(Corruption, "$0 called on an empty stack", function_name);
    }
    const ValueType value_type = stack_.top()->value_type();
    if (value_type != ValueType::kObject) {
      return STATUS_SUBSTITUTE(Corruption,
                               "$0 called but the current subdocument type is $1",
                               function_name, ValueTypeToStr(value_type));
    }
    return Status::OK();
  }

  stack<SubDocument*> stack_;
  SubDocument root_;
  PrimitiveValue key_;
  bool doc_found_ = false;
};

}  // namespace

yb::Status GetDocument(rocksdb::DB* rocksdb,
                       const KeyBytes& document_key,
                       SubDocument* result,
                       bool* doc_found,
                       const Timestamp scan_ts) {
  DOCDB_DEBUG_LOG("GetDocument for key $0", document_key.ToString());
  *doc_found = false;

  SubDocumentBuildingVisitor subdoc_builder;
  RETURN_NOT_OK(ScanDocument(rocksdb, document_key, &subdoc_builder, scan_ts));
  *result = subdoc_builder.ReleaseResult();
  *doc_found = subdoc_builder.doc_found();
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Debug output
// ------------------------------------------------------------------------------------------------

Status DocDBDebugDump(rocksdb::DB* rocksdb, ostream& out, const bool include_binary) {
  rocksdb::ReadOptions read_opts;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  iter->SeekToFirst();
  auto result_status = Status::OK();
  while (iter->Valid()) {
    SubDocKey subdoc_key;
    rocksdb::Slice key_slice(iter->key());
    Status subdoc_key_decode_status = subdoc_key.FullyDecodeFrom(key_slice);
    if (!subdoc_key_decode_status.ok()) {
      out << "Error: failed decoding RocksDB key " << FormatRocksDBSliceAsStr(iter->key()) << ": "
          << subdoc_key_decode_status.ToString() << endl;
      if (result_status.ok()) {
        result_status = subdoc_key_decode_status;
      }
      iter->Next();
      continue;
    }

    Value value;
    Status value_decode_status = value.Decode(iter->value());
    if (!value_decode_status.ok()) {
      out << "Error: failed to decode value for key " << subdoc_key.ToString() << endl;
      if (result_status.ok()) {
        result_status = value_decode_status;
      }
      iter->Next();
      continue;
    }

    out << subdoc_key.ToString() << " -> " << value.ToString() << endl;
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

}  // namespace docdb
}  // namespace yb

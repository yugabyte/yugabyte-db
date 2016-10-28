// Copyright (c) YugaByte, Inc.

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/timestamp.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/internal_doc_iterator.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value_type.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"

using std::endl;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::shared_ptr;
using std::stack;
using std::vector;

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
    doc_op->Apply(&doc_write_batch);
  }
  doc_write_batch.MoveToWriteBatchPB(dest);
  return Status::OK();
}

Status HandleRedisReadTransaction(rocksdb::DB *rocksdb,
                                  const vector<unique_ptr<RedisReadOperation>>& doc_read_ops,
                                  Timestamp timestamp) {
  for (const unique_ptr<RedisReadOperation>& doc_op : doc_read_ops) {
    doc_op->Execute(rocksdb, timestamp);
  }
  return Status::OK();
}

DocPath YSQLWriteOperation::DocPathToLock() const {
  return doc_path_;
}

Status YSQLWriteOperation::Apply(DocWriteBatch* doc_write_batch) {
  return doc_write_batch->SetPrimitive(doc_path_, value_, Timestamp::kMax);
}

DocPath RedisWriteOperation::DocPathToLock() const {
  CHECK_EQ(request_.redis_op_type(), RedisWriteRequestPB_Type_SET)
      << "Currently only SET is supported";
  return DocPath::DocPathFromRedisKey(request_.set_request().key_value().key());
}

Status RedisWriteOperation::Apply(DocWriteBatch* doc_write_batch) {
  CHECK_EQ(request_.redis_op_type(), RedisWriteRequestPB_Type_SET)
      << "Currently only SET is supported";
  const auto kv = request_.set_request().key_value();
  CHECK_EQ(kv.value().size(), 1)
      << "Set operations are expected have exactly one value, found " << kv.value().size();
  doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.key()), PrimitiveValue(kv.value(0)), Timestamp::kMax);
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

const RedisResponsePB& RedisWriteOperation::response() { return response_; }

Status RedisReadOperation::Execute(rocksdb::DB *rocksdb, Timestamp timestamp) {
  CHECK_EQ(request_.redis_op_type(), RedisReadRequestPB_Type_GET)
      << "Currently only GET is supported";
  const KeyBytes doc_key = DocKey::FromRedisStringKey(
      request_.get_request().key_value().key()).Encode();
  KeyBytes timestamped_key = doc_key;
  timestamped_key.AppendTimestamp(timestamp);
  rocksdb::ReadOptions read_opts;
  auto iter = std::unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  iter->Seek(timestamped_key.AsSlice());
  if (!iter->Valid()) {
    response_.set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
    return Status::OK();
  }
  const rocksdb::Slice key = iter->key();
  const rocksdb::Slice value = iter->value();
  if (!timestamped_key.OnlyDiffersByLastTimestampFrom(key)) {
    response_.set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
    return Status::OK();
  }
  PrimitiveValue pv;
  RETURN_NOT_OK(pv.DecodeFromValue(value));
  if (pv.value_type() != ValueType::kString) {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    return Status::OK();
  }
  pv.SwapStringValue(response_.mutable_string_response());
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

const RedisResponsePB& RedisReadOperation::response() {
  return response_;
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
                                   const PrimitiveValue& value,
                                   Timestamp timestamp) {
  DOCDB_DEBUG_LOG("Called with doc_path=$0, value=$1, timestamp=$2",
                  doc_path.ToString(), value.ToString(), timestamp.ToDebugString());
  const KeyBytes& encoded_doc_key = doc_path.encoded_doc_key();
  const int num_subkeys = doc_path.num_subkeys();
  const bool is_deletion = value.value_type() == ValueType::kTombstone;

  InternalDocIterator doc_iter(rocksdb_, &cache_, &num_rocksdb_seeks_);

  if (num_subkeys > 0 || is_deletion) {
    // Navigate to the root of the document, not including the generation timestamp. We don't yet
    // know whether the document exists or when it was last updated.
    doc_iter.SeekToDocument(encoded_doc_key);
    DOCDB_DEBUG_LOG("Top-level document exists: $0", doc_iter.subdoc_exists());
    if (!doc_iter.subdoc_exists()) {
      if (is_deletion) {
        DOCDB_DEBUG_LOG("We're performing a deletion, and the document is not present. "
                        "Nothing to do.");
        return Status::OK();
      }
    }
    doc_iter.AppendUpdateTimestampIfNotFound(timestamp);
  } else {
    // If we are overwriting an entire document with a primitive value (not deleting it), we don't
    // need to perform any reads from RocksDB at all.
    doc_iter.SetDocumentKey(encoded_doc_key);
    doc_iter.AppendTimestampToPrefix(timestamp);
  }

  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    assert(doc_iter.key_prefix_ends_with_ts());
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    if (subkey.value_type() == ValueType::kArrayIndex) {
      return STATUS(NotSupported, "Setting values at a given array index is not supported yet");
    }
    if (doc_iter.subdoc_exists()) {
      if (doc_iter.subdoc_type() != ValueType::kObject) {
        return STATUS(IllegalState, Substitute(
            "Cannot set values inside a subdocument of type $0",
            ValueTypeToStr(doc_iter.subdoc_type())));
      }
      if (subkey_index == num_subkeys - 1 && !is_deletion) {
        // We don't need to perform a RocksDB read at the last level for upserts, we just
        // overwrite the value within the last subdocument with what we're trying to write.
        // We still perform the read for deletions, because we try to avoid writing a new tombstone
        // if the data is not there anyway.
        doc_iter.AppendSubkeyInExistingSubDoc(subkey);
        doc_iter.AppendTimestampToPrefix(timestamp);
      } else {
        // We need to check if the subdocument at this subkey exists.
        doc_iter.SeekToSubDocument(subkey);
        if (is_deletion) {
          if (!doc_iter.subdoc_exists()) {
            // A parent subdocument of the value we're trying to delete, or that value itself,
            // does not exist, nothing to do.
            DOCDB_DEBUG_LOG("Subdocument does not exist at subkey level $0 (subkey: $1)",
                            subkey_index, subkey.ToString());
            return Status::OK();
          }
          if (subkey_index == num_subkeys - 1) {
            // Replace the last timestamp only at the final level as we're about to write the
            // tombstone.
            doc_iter.ReplaceTimestampInPrefix(timestamp);
          }
        } else {
          doc_iter.AppendUpdateTimestampIfNotFound(timestamp);
        }
      }
    } else {
      if (is_deletion) {
        // A parent subdocument of the subdocument we're trying to delete does not exist, nothing
        // to do.
        return Status::OK();
      }

      // The document/subdocument that this subkey is supposed to live in does not exist, create it.
      put_batch_.emplace_back(doc_iter.key_prefix().AsStringRef(), kObjectValueType);

      // Record the fact that we're adding this subdocument in our local cache so that future
      // operations in this document write batch don't have to add it or look for it in RocksDB.
      // Note that the key we're using in the cache does not have the timestamp at the end.
      cache_.Put(KeyBytes(doc_iter.key_prefix().AsSliceWithoutTimestamp()),
                 timestamp, ValueType::kObject);

      doc_iter.AppendToPrefix(subkey);
      doc_iter.AppendTimestampToPrefix(timestamp);
    }
  }

  put_batch_.emplace_back(doc_iter.key_prefix().AsStringRef(), value.ToValue());

  // The key we use in the DocWriteBatchCache does not have a final timestamp, because that's the
  // key we expect to look up.
  cache_.Put(KeyBytes(doc_iter.key_prefix().AsSliceWithoutTimestamp()),
             timestamp, value.value_type());

  return Status::OK();
}

Status DocWriteBatch::DeleteSubDoc(const DocPath& doc_path, const Timestamp timestamp) {
  return SetPrimitive(doc_path, PrimitiveValue(ValueType::kTombstone), timestamp);
}

string DocWriteBatch::ToDebugString() {
  WriteBatchFormatter formatter;
  rocksdb::WriteBatch rocksdb_write_batch;
  PopulateRocksDBWriteBatch(&rocksdb_write_batch);
  rocksdb::Status iteration_status = rocksdb_write_batch.Iterate(&formatter);
  CHECK(iteration_status.ok());
  return formatter.str();
}

void DocWriteBatch::Clear() {
  put_batch_.clear();
  cache_.Clear();
}

void DocWriteBatch::PopulateRocksDBWriteBatch(rocksdb::WriteBatch* rocksdb_write_batch,
                                              Timestamp timestamp) const {
  for (const auto& entry : put_batch_) {
    SubDocKey subdoc_key;
    // We don't expect any invalid encoded keys in the write batch.
    CHECK_OK(subdoc_key.DecodeFrom(entry.first));
    if (timestamp != Timestamp::kMax) {
      subdoc_key.ReplaceMaxTimestampWith(timestamp);
    }

    rocksdb_write_batch->Put(subdoc_key.Encode().AsSlice(), entry.second);
  }
}

rocksdb::Status DocWriteBatch::WriteToRocksDB(
    const Timestamp timestamp,
    const rocksdb::WriteOptions& write_options) const {
  if (IsEmpty()) {
    return rocksdb::Status::OK();
  }

  rocksdb::WriteBatch rocksdb_write_batch;
  PopulateRocksDBWriteBatch(&rocksdb_write_batch, timestamp);
  return rocksdb_->Write(write_options, &rocksdb_write_batch);
}

void DocWriteBatch::MoveToWriteBatchPB(KeyValueWriteBatchPB *kv_pb) {
  for (auto& entry : put_batch_) {
    KeyValuePairPB* kv_pair = kv_pb->add_kv_pairs();
    kv_pair->mutable_key()->swap(entry.first);
    kv_pair->mutable_value()->swap(entry.second);
  }
}

// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

static yb::Status ScanSubDocument(const SubDocKey& higher_level_key,
                                  rocksdb::Iterator* rocksdb_iter,
                                  DocVisitor* visitor);

static yb::Status ScanPrimitiveValueOrObject(const SubDocKey& higher_level_key,
                                             rocksdb::Iterator* rocksdb_iter,
                                             DocVisitor* visitor) {
  PrimitiveValue top_level_value;
  RETURN_NOT_OK(top_level_value.DecodeFromValue(rocksdb_iter->value()));

  if (top_level_value.value_type() == ValueType::kTombstone) {
    return Status::OK();  // the document does not exist
  }

  if (top_level_value.IsPrimitive()) {
    RETURN_NOT_OK(visitor->VisitValue(top_level_value));
    return Status::OK();
  }

  if (top_level_value.value_type() == ValueType::kObject) {
    rocksdb_iter->Next();
    RETURN_NOT_OK(visitor->StartObject());
    RETURN_NOT_OK(ScanSubDocument(higher_level_key, rocksdb_iter, visitor));
    RETURN_NOT_OK(visitor->EndObject());
    return Status::OK();
  }

  return STATUS(Corruption, Substitute("Invalid value type at the top level of a document: $0",
                                       ValueTypeToStr(top_level_value.value_type())));
}


static yb::Status ScanSubDocument(const SubDocKey& higher_level_key,
                                  rocksdb::Iterator* const rocksdb_iter,
                                  DocVisitor* const visitor) {
  DOCDB_DEBUG_LOG("higher_level_key=$0", higher_level_key.ToString());
  while (rocksdb_iter->Valid()) {
    SubDocKey subdoc_key;
    RETURN_NOT_OK(subdoc_key.DecodeFrom(rocksdb_iter->key()));
    if (!subdoc_key.StartsWith(higher_level_key)) {
      break;
    }
    DOCDB_DEBUG_LOG("subdoc_key=$0", subdoc_key.ToString());

    if (subdoc_key.num_subkeys() != higher_level_key.num_subkeys() + 1) {
      static const char* kErrorMsgPrefix =
          "A subdocument key must be nested exactly one level under the parent subdocument";
      // Log more details in debug mode.
      DLOG(WARNING) << Substitute(
          "$0. Got parent subdocument key: $1, subdocument key: $2.",
          kErrorMsgPrefix, higher_level_key.ToString(), subdoc_key.ToString());
      return STATUS(Corruption, kErrorMsgPrefix);
    }

    if (DecodeValueType(rocksdb_iter->value()) != ValueType::kTombstone) {
      RETURN_NOT_OK(visitor->VisitKey(subdoc_key.last_subkey()));
    }
    ScanPrimitiveValueOrObject(subdoc_key, rocksdb_iter, visitor);

    DOCDB_DEBUG_LOG("Performing a seek to $0",
                    subdoc_key.AdvanceToNextSubkey().Encode().ToString());
    rocksdb_iter->Seek(subdoc_key.AdvanceToNextSubkey().Encode().AsSlice());
  }
  return Status::OK();
}

yb::Status ScanDocument(rocksdb::DB* rocksdb,
                        const KeyBytes& document_key,
                        DocVisitor* visitor) {
  auto rocksdb_iter = InternalDocIterator::CreateRocksDBIterator(rocksdb);
  rocksdb_iter->Seek(document_key.AsSlice());
  if (!rocksdb_iter->Valid() || !document_key.IsPrefixOf(rocksdb_iter->key())) {
    return Status::OK();
  }

  SubDocKey doc_key;
  RETURN_NOT_OK(doc_key.DecodeFrom(rocksdb_iter->key()));
  if (doc_key.num_subkeys() > 0) {
    return STATUS(Corruption,
                  Substitute("A top-level document key is not supposed to contain any sub-keys: $0",
                             doc_key.ToString()));
  }

  RETURN_NOT_OK(visitor->StartDocument(doc_key.doc_key()));
  RETURN_NOT_OK(ScanPrimitiveValueOrObject(doc_key, rocksdb_iter.get(), visitor));
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
        return STATUS(Corruption,
                      Substitute("Cannot set a value within a subdocument of type $0",
                                 ValueTypeToStr(value_type)));
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
          return STATUS(Corruption, Substitute("Duplicate key"));
        }
        stack_.push(new_subdoc_and_child_added.first);
      } else {
        return STATUS(Corruption,
                      Substitute("Cannot set a value within a subdocument of type $0",
                                 ValueTypeToStr(value_type)));
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
      return STATUS(Corruption, Substitute("$0 called on an empty stack", function_name));
    }
    const ValueType value_type = stack_.top()->value_type();
    if (value_type != ValueType::kObject) {
      return STATUS(Corruption,
                    Substitute("$0 called but the current subdocument type is $1",
                               function_name, ValueTypeToStr(value_type)));
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
                       bool* doc_found) {
  DOCDB_DEBUG_LOG("GetDocument for key $0", document_key.ToString());
  *doc_found = false;

  SubDocumentBuildingVisitor subdoc_builder;
  RETURN_NOT_OK(ScanDocument(rocksdb, document_key, &subdoc_builder));
  *result = subdoc_builder.ReleaseResult();
  *doc_found = subdoc_builder.doc_found();
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Debug output
// ------------------------------------------------------------------------------------------------

Status DocDBDebugDump(rocksdb::DB* rocksdb, ostream& out) {
  rocksdb::ReadOptions read_opts;
  auto iter = unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  iter->SeekToFirst();
  auto result_status = Status::OK();
  while (iter->Valid()) {
    SubDocKey subdoc_key;
    rocksdb::Slice key_slice(iter->key());
    Status subdoc_key_decode_status = subdoc_key.DecodeFrom(key_slice);
    if (!subdoc_key_decode_status.ok()) {
      out << "Error: failed decoding RocksDB key " << FormatRocksDBSliceAsStr(iter->key()) << ": "
          << subdoc_key_decode_status.ToString() << endl;
      if (result_status.ok()) {
        result_status = subdoc_key_decode_status;
      }
      iter->Next();
      continue;
    }

    PrimitiveValue value;
    Status value_decode_status = value.DecodeFromValue(iter->value());
    if (!value_decode_status.ok()) {
      out << "Error: failed to decode value for key " << subdoc_key.ToString() << endl;
      if (result_status.ok()) {
        result_status = value_decode_status;
      }
      iter->Next();
      continue;
    }

    out << subdoc_key.ToString() << " -> " << value.ToString() << endl;

    iter->Next();
  }
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb

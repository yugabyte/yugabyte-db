// Copyright (c) YugaByte, Inc.

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/redis_protocol.pb.h"
#include "yb/common/hybrid_time.h"
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

using yb::HybridTime;
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


void PrepareDocWriteTransaction(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                                util::SharedLockManager *lock_manager,
                                vector<string> *keys_locked,
                                bool *need_read_snapshot) {
  *need_read_snapshot = false;
  unordered_map<string, LockType> lock_type_map;
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
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
    if (doc_op->RequireReadSnapshot()) {
      *need_read_snapshot = true;
    }
  }
  // Sort the set of locks to be taken for this transaction, to make sure deadlocks don't occur.
  std::sort(keys_locked->begin(), keys_locked->end());
  for (string key : *keys_locked) {
    lock_manager->Lock(key, lock_type_map[key]);
  }
}

Status ApplyDocWriteTransaction(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                                const HybridTime& hybrid_time,
                                rocksdb::DB *rocksdb,
                                KeyValueWriteBatchPB* write_batch) {
  DocWriteBatch doc_write_batch(rocksdb);
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    RETURN_NOT_OK(doc_op->Apply(&doc_write_batch, rocksdb, hybrid_time));
  }
  doc_write_batch.MoveToWriteBatchPB(write_batch);
  return Status::OK();
}

Status HandleRedisReadTransaction(rocksdb::DB *rocksdb,
                                  const vector<unique_ptr<RedisReadOperation>>& doc_read_ops,
                                  HybridTime hybrid_time) {
  for (const unique_ptr<RedisReadOperation>& doc_op : doc_read_ops) {
    RETURN_NOT_OK(doc_op->Execute(rocksdb, hybrid_time));
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
CHECKED_STATUS DocWriteBatch::SetPrimitiveInternal(const DocPath& doc_path, const Value& value,
                                                   InternalDocIterator *doc_iter,
                                                   const HybridTime hybrid_time,
                                                   const bool is_deletion, const int num_subkeys,
                                                   InitMarkerBehavior use_init_marker) {
  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    if (subkey.value_type() == ValueType::kArrayIndex) {
      return STATUS(NotSupported, "Setting values at a given array index is not supported yet");
    }
    // We don't need to check if intermediate documents should exist if init markers are optional.
    if (use_init_marker == InitMarkerBehavior::kOptional || doc_iter->subdoc_exists()) {
      if (use_init_marker == InitMarkerBehavior::kRequired &&
          doc_iter->subdoc_type() != ValueType::kObject) {
        // We raise this error only if init markers are mandatory.
        return STATUS_SUBSTITUTE(IllegalState,
                                 "Cannot set values inside a subdocument of type $0",
                                 ValueTypeToStr(doc_iter->subdoc_type()));
      }
      if ((subkey_index == num_subkeys - 1 && !is_deletion) ||
          use_init_marker == InitMarkerBehavior::kOptional) {
        // We don't need to perform a RocksDB read at the last level for upserts, we just
        // overwrite the value within the last subdocument with what we're trying to write.
        // We still perform the read for deletions, because we try to avoid writing a new tombstone
        // if the data is not there anyway.
        // Apart from the above case, if init markers are optional there is no point in
        // seeking to intermediate document levels to verify their existence.
        if (use_init_marker == InitMarkerBehavior::kOptional) {
          // In the case where init markers are optional, we don't need to check existence of
          // the current subdoc.
          doc_iter->AppendToPrefix(subkey);
        } else {
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
      parent_key.AppendValueType(ValueType::kHybridTime);
      parent_key.AppendHybridTime(hybrid_time);
      // TODO: std::move from parent_key's string buffer into put_batch_.
      put_batch_.emplace_back(parent_key.AsStringRef(), kObjectValueType);

      // Update our local cache to record the fact that we're adding this subdocument, so that
      // future operations in this DocWriteBatch don't have to add it or look for it in RocksDB.
      cache_.Put(KeyBytes(doc_iter->key_prefix().AsSlice()), hybrid_time, ValueType::kObject);

      doc_iter->AppendToPrefix(subkey);
    }
  }

  // The key we use in the DocWriteBatchCache does not have a final hybrid_time, because that's the
  // key we expect to look up.
  cache_.Put(doc_iter->key_prefix(), hybrid_time, value.primitive_value().value_type());

  // Close the group of subkeys of the SubDocKey, and append the hybrid_time as the final component.
  doc_iter->mutable_key_prefix()->AppendValueType(ValueType::kHybridTime);
  doc_iter->AppendHybridTimeToPrefix(hybrid_time);

  put_batch_.emplace_back(doc_iter->key_prefix().AsStringRef(), value.Encode());

  return Status::OK();
}

Status DocWriteBatch::SetPrimitive(const DocPath& doc_path,
                                   const Value& value,
                                   HybridTime hybrid_time,
                                   InitMarkerBehavior use_init_marker) {
  DOCDB_DEBUG_LOG("Called with doc_path=$0, value=$1, hybrid_time=$2",
                  doc_path.ToString(), value.ToString(), hybrid_time.ToDebugString());
  const KeyBytes& encoded_doc_key = doc_path.encoded_doc_key();
  const int num_subkeys = doc_path.num_subkeys();
  const bool is_deletion = value.primitive_value().value_type() == ValueType::kTombstone;

  InternalDocIterator doc_iter(rocksdb_, &cache_, &num_rocksdb_seeks_);

  if (num_subkeys > 0 || is_deletion) {
    doc_iter.SetDocumentKey(encoded_doc_key);
    if (use_init_marker == InitMarkerBehavior::kRequired) {
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

  return SetPrimitiveInternal(doc_path, value, &doc_iter, hybrid_time, is_deletion, num_subkeys,
                              use_init_marker);
}

Status DocWriteBatch::ExtendSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    HybridTime hybrid_time,
    InitMarkerBehavior use_init_marker,
    MonoDelta ttl) {
  if (value.value_type() == ValueType::kObject) {
    const auto& map = value.object_container();
    for (const auto& ent : map) {
      DocPath child_doc_path = doc_path;
      child_doc_path.AddSubKey(ent.first);
      RETURN_NOT_OK(
          ExtendSubDocument(child_doc_path, ent.second, hybrid_time, use_init_marker, ttl));
    }
  } else if (value.value_type() == ValueType::kArray) {
    // In future ExtendSubDocument will also support List types, will call ExtendList in this case.
    // For now it is not supported because it's clunky to pass monotonic counters in every function.
    return STATUS(InvalidArgument, "Cannot insert subdocument of list type");
  } else {
    if (!value.IsPrimitive() && value.value_type() != ValueType::kTombstone) {
      return STATUS_SUBSTITUTE(InvalidArgument,
          "Found unexpected value type $0. Expecting a PrimitiveType or a Tombstone",
          ValueTypeToStr(value.value_type()));
    }
    RETURN_NOT_OK(SetPrimitive(doc_path, Value(value, ttl), hybrid_time, use_init_marker));
  }
  return Status::OK();
}

Status DocWriteBatch::InsertSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    HybridTime hybrid_time,
    InitMarkerBehavior use_init_marker,
    MonoDelta ttl) {
  if (!value.IsPrimitive() && value.value_type() != ValueType::kTombstone) {
    RETURN_NOT_OK(SetPrimitive(
        doc_path, Value(PrimitiveValue(ValueType::kTombstone)), hybrid_time, use_init_marker));
  }
  return ExtendSubDocument(doc_path, value, hybrid_time, use_init_marker, ttl);
}

Status DocWriteBatch::DeleteSubDoc(const DocPath& doc_path, const HybridTime hybrid_time,
                                   InitMarkerBehavior use_init_marker) {
  return SetPrimitive(doc_path, PrimitiveValue(ValueType::kTombstone), hybrid_time,
                      use_init_marker);
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
                                                    HybridTime hybrid_time) const {
  for (const auto& entry : put_batch_) {
    SubDocKey subdoc_key;
    // We don't expect any invalid encoded keys in the write batch.
    CHECK_OK_PREPEND(subdoc_key.FullyDecodeFrom(entry.first),
                     Substitute("when decoding key: $0", FormatBytesAsStr(entry.first)));
    if (hybrid_time != HybridTime::kMax) {
      subdoc_key.ReplaceMaxHybridTimeWith(hybrid_time);
    }

    rocksdb_write_batch->Put(subdoc_key.Encode().AsSlice(), entry.second);
  }
}

rocksdb::Status DocWriteBatch::WriteToRocksDBInTest(
    const HybridTime hybrid_time,
    const rocksdb::WriteOptions &write_options) const {
  if (IsEmpty()) {
    return rocksdb::Status::OK();
  }

  rocksdb::WriteBatch rocksdb_write_batch;
  PopulateRocksDBWriteBatchInTest(&rocksdb_write_batch, hybrid_time);
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
                                  HybridTime scan_ht,
                                  HybridTime lowest_ht);

// @param higher_level_key
//     The SubDocKey corresponding to the root of the subdocument tree to scan. This is the expected
//     to be the parsed version of the key/value pair the RocksDB iterator is currently positioned
//     at. This could be a primitive value or an object.
//
// @param rocksdb_iter
//     A RocksDB iterator that is expected to be positioned at the "root" key/value pair of the
//     subdocument or primitive value we are trying to scan.
//
// @param scan_ht The hybrid_time we are trying to scan the state of the database at.
// @param lowest_ht
//     The lowest hybrid_time that we need to consider while walking the nested document tree. This
//     is based on init markers or tombstones seen at higher levels. E.g. if we've already seen an
//     init marker at hybrid_time t, then lowest_ht will be t, or if we've seen a tombstone at
//     hybrid_time t, lowest_ht will be t + 1 (because that's the earliest hybrid_time we need to
//     care about after a deletion at hybrid_time t). The latter case is only relevant for the case
//     when optional init markers are enabled, which is not supported as of 12/06/2016.
static yb::Status ScanPrimitiveValueOrObject(const SubDocKey& higher_level_key,
                                             rocksdb::Iterator* rocksdb_iter,
                                             DocVisitor* visitor,
                                             HybridTime scan_ht,
                                             HybridTime lowest_ht) {
  DOCDB_DEBUG_LOG("higher_level_key=$0, scan_ht=$1, lowest_ht=$2",
                  higher_level_key.ToString(), scan_ht.ToDebugString(), lowest_ht.ToDebugString());
  DCHECK_LE(higher_level_key.hybrid_time(), scan_ht)
      << "; higher_level_key=" << higher_level_key.ToString();
  DCHECK_GE(higher_level_key.hybrid_time(), lowest_ht)
      << "; higher_level_key=" << higher_level_key.ToString();

  Value top_level_value;
  RETURN_NOT_OK(top_level_value.Decode(rocksdb_iter->value()));

  if (top_level_value.value_type() == ValueType::kTombstone) {
    // TODO(mbautin): If we allow optional init markers here, we still need to scan deeper levels
    // and interpret values with hybrid_times >= lowest_ht as values in a map that exists. As of
    // 11/11/2016 initialization markers (i.e. ValueType::kObject) are still required at the top of
    // each object's key range.
    return Status::OK();  // the document does not exist
  }

  if (top_level_value.primitive_value().IsPrimitive()) {
    RETURN_NOT_OK(visitor->VisitValue(top_level_value.primitive_value()));
    return Status::OK();
  }

  if (top_level_value.value_type() == ValueType::kObject) {
    rocksdb_iter->Next();
    RETURN_NOT_OK(visitor->StartObject());

    KeyBytes deeper_level_key(higher_level_key.Encode(/* include_hybrid_time = */ false));
    deeper_level_key.AppendValueType(kMinPrimitiveValueType);
    ROCKSDB_SEEK(rocksdb_iter, deeper_level_key.AsSlice());

    RETURN_NOT_OK(ScanSubDocument(higher_level_key, rocksdb_iter, visitor, scan_ht,
                                  higher_level_key.hybrid_time()));
    RETURN_NOT_OK(visitor->EndObject());
    return Status::OK();
  }

  return STATUS_SUBSTITUTE(Corruption, "Invalid value type at the top level of a document: $0",
                           ValueTypeToStr(top_level_value.primitive_value().value_type()));
}

// @param higher_level_key
//     The top-level key of the document we are scanning.
//     Precondition: This key points to an object, not a PrimitiveValue: has subkeys under it.
//
// @param rocksdb_iter
//     A RocksDB iterator positioned at the first key/value to scan one level below the top of the
//     document.
//
//     Example:
//
//     SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(1000)]) -> "value1"
//     SubDocKey(DocKey([], ["mydockey", 123456]), [HT(2000)]) -> {}
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a", HT(2000)]) -> "value_a"
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", HT(7000)]) -> {}
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", HT(6000)]) -> DEL
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", HT(3000)]) -> {}
//  => SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c", HT(7000)]) -> "v_bc2"
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c", HT(5000)]) -> DEL
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c", HT(3000)]) -> "v_bc"
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d", HT(3500)]) -> "v_bd"
//
//     The following example corresponds to higher_level_key equal to
//     SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", HT(7000)])
static yb::Status ScanSubDocument(const SubDocKey& higher_level_key,
                                  rocksdb::Iterator* const rocksdb_iter,
                                  DocVisitor* const visitor,
                                  HybridTime scan_ht,
                                  HybridTime lowest_ht) {
  DOCDB_DEBUG_LOG("higher_level_key=$0, scan_ht=$1, lowest_ht=$2",
                  higher_level_key.ToString(), scan_ht.ToDebugString(), lowest_ht.ToDebugString());
  SubDocKey higher_level_key_no_ht(higher_level_key);
  higher_level_key_no_ht.RemoveHybridTime();

  while (rocksdb_iter->Valid()) {
    SubDocKey subdoc_key;
    RETURN_NOT_OK(subdoc_key.FullyDecodeFrom(rocksdb_iter->key()));
    if (!subdoc_key.StartsWith(higher_level_key_no_ht)) {
      // We have reached the end of the subdocument we are trying to scan. This could also be the
      // end of the entire document.
      //
      //    1. SubDocKey(<doc_key1>, [HT(2000)]) -> {}
      //    2. SubDocKey(<doc_key1>, ["subkey_a", HT(2000)]) -> "value_a"
      // => 3. SubDocKey(<doc_key2>, [HT(3000)]) -> {}
      //
      // In the above example, higher_level_key = SubDocKey(<doc_key1>, [HT(2000)]), and
      // higher_level_key_no_ht = SubDocKey(<doc_key1>), so key/value pair #2 is still part of the
      // document, because it has the right prefix, while key/value pair #3 is not.
      break;
    }
    if (subdoc_key.hybrid_time() > scan_ht) {
      // This entry is still in the future compared to our scan hybrid_time. Adjust the hybrid_time
      // and try again.
      subdoc_key.set_hybrid_time(scan_ht);
      ROCKSDB_SEEK(rocksdb_iter, subdoc_key.Encode().AsSlice());
      continue;
    }
    DOCDB_DEBUG_LOG("subdoc_key=$0", subdoc_key.ToString());

    if (subdoc_key.num_subkeys() != higher_level_key.num_subkeys() + 1) {
      static const char* kErrorMsgPrefix =
          "A subdocument key must be nested exactly one level under the parent subdocument";
      // Log more details in debug mode.
      DLOG(WARNING) << Substitute(
          "$0.\nParent subdocument key:\n$1\nSubdocument key:\n$2",
          kErrorMsgPrefix, higher_level_key.ToString(), subdoc_key.ToString());
      return STATUS(Corruption, kErrorMsgPrefix);
    }

    if (subdoc_key.hybrid_time() >= lowest_ht &&
        DecodeValueType(rocksdb_iter->value()) != ValueType::kTombstone) {
      RETURN_NOT_OK(visitor->VisitKey(subdoc_key.last_subkey()));
      ScanPrimitiveValueOrObject(
          subdoc_key, rocksdb_iter, visitor, scan_ht, subdoc_key.hybrid_time());
    }

    // Get out of the subdocument we've just scanned and go to the next one.
    ROCKSDB_SEEK(rocksdb_iter, subdoc_key.AdvanceOutOfSubDoc().AsSlice());
  }
  return Status::OK();
}

}  // anonymous namespace

yb::Status ScanSubDocument(rocksdb::DB *rocksdb,
    const KeyBytes &subdocument_key,
    DocVisitor *visitor,
    HybridTime scan_ht) {
  auto rocksdb_iter = CreateRocksDBIterator(rocksdb);

  // TODO: Use a SubDocKey API to build the proper seek key without assuming anything about the
  //       internal structure of SubDocKey here.
  KeyBytes seek_key(subdocument_key);

  SubDocKey found_subdoc_key;
  Value doc_value;
  bool is_found = false;

  Status s = SeekToValidKvAtTs(
      rocksdb_iter.get(), seek_key.AsSlice(), scan_ht, &found_subdoc_key, &doc_value, &is_found);

  if (!is_found || !subdocument_key.OnlyLacksHybridTimeFrom(rocksdb_iter->key())) {
    // This could happen when we are trying to scan at an old hybrid_time at which the subdocument
    // does not exist yet. In that case we'll jump directly into the section of the RocksDB key
    // space that contains deeper levels of the document.

    // Example (from a real test failure)
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Suppose the above RocksDB seek is as follows:
    //
    //        Seek key:       SubDocKey(DocKey([], [-7805187538405744458, false]), [HT(3)])
    //        Seek key (raw): "I\x13\xaegI\x98\x05 \xb6F!#\xff\xff\xff\xff\xff\xff\xff\xfc"
    //        Actual key:     SubDocKey(DocKey([], [-7805187538405744458, false]), [true; HT(4)])
    //        Actual value:   "{"
    //
    // and the relevant part of the RocksDB state is as follows (as SubDocKey and binary):
    //
    // SubDocKey(DocKey([], [-7805187538405744458, false]), [HT(4)]) -> {}
    // "I\x13\xaegI\x98\x05 \xb6F!#\xff\xff\xff\xff\xff\xff\xff\xfb" -> "{"
    //
    // SubDocKey(DocKey([], [-7805187538405744458, false]), [true; HT(4)]) -> {}  <--------------.
    // "I\x13\xaegI\x98\x05 \xb6F!T#\xff\xff\xff\xff\xff\xff\xff\xfb" -> "{"                     |
    //                                                                                           |
    // Then we'll jump directly here as we try to retrieve the document state at hybrid_time 3: ---/
    //
    // The right thing to do here is just to return, assuming the document does not exist.
    return Status::OK();
  }

  RETURN_NOT_OK(visitor->StartSubDocument(found_subdoc_key));
  RETURN_NOT_OK(ScanPrimitiveValueOrObject(
      found_subdoc_key, rocksdb_iter.get(), visitor, scan_ht, found_subdoc_key.hybrid_time()));
  RETURN_NOT_OK(visitor->EndSubDocument());

  return Status::OK();
}

namespace {

// This is used in the implementation of GetSubDocument. Builds a SubDocument from a stream of
// DocVisitor API calls made by the ScanDocument function.
class SubDocumentBuildingVisitor : public DocVisitor {
 public:
  SubDocumentBuildingVisitor() {}

  virtual ~SubDocumentBuildingVisitor() {}

  Status StartSubDocument(const SubDocKey &key) override {
    return Status::OK();
  }

  Status EndSubDocument() override {
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

// If there is a key equal to key_bytes + some timestamp, we return its hybridtime.
// This should not be used for leaf nodes.
// TODO: We could also check that the value is kTombStone or kObject type for sanity checking.
HybridTime FindLastWriteTime(const KeyBytes& key_bytes,
    HybridTime scan_ht, rocksdb::Iterator* iter) {
  KeyBytes key_with_ts = key_bytes;
  key_with_ts.AppendValueType(ValueType::kHybridTime);
  key_with_ts.AppendHybridTime(scan_ht);
  SeekForward(key_with_ts, iter);
  if (iter->Valid() && key_bytes.OnlyLacksHybridTimeFrom(iter->key())) {
    return DecodeHybridTimeFromKey(iter->key());
    // TODO when we support ttl on non-leaf nodes, we need to take that into account here.
  } else {
    return HybridTime::kMin;
  }
}

// When we replace HybridTime::kMin in the end of seek key, next seek will skip older
// versions of this key, but will not skip any subkeys in its subtree.
// If the iterator is already positioned far enough, does not perform a seek.
void SeekPastSubKey(const SubDocKey& sub_doc_key, rocksdb::Iterator* iter) {
  KeyBytes key_bytes = sub_doc_key.Encode(/* include_hybrid_time */ false);
  key_bytes.AppendValueType(ValueType::kHybridTime);
  key_bytes.AppendHybridTime(HybridTime::kMin);
  SeekForward(key_bytes, iter);
}

// This works similar to the ScanSubDocument function, but doesn't assume that object init_markers
// are present. If no init marker is present, or if a tombstone is found at some level,
// it still looks for subkeys inside it if they have larger timestamps.
// TODO(akashnil): ENG-1152: If object init markers were required, this read path may be optimized.
// We look at all rocksdb keys with prefix = subdocument_key, and construct a subdocument out of
// them, between the timestamp range high_ts and low_ts
// The iterator is expected to be placed at the smallest key that is subdocument_key or later, and
// after the function returns, the iterator should be placed just completely outside the
// subdocument_key prefix.
yb::Status BuildSubDocument(rocksdb::Iterator* iter,
    const SubDocKey &subdocument_key,
    SubDocument* subdocument,
    HybridTime high_ts,
    HybridTime low_ts,
    MonoDelta table_ttl) {
  DCHECK(!subdocument_key.has_hybrid_time());
  const KeyBytes encoded_key = subdocument_key.Encode();

  while (true) {

    SubDocKey found_key;
    Value doc_value;

    if (!iter->Valid()) {
      return Status::OK();
    }

    if (!iter->key().starts_with(encoded_key.AsSlice())) {
      return Status::OK();
    }

    RETURN_NOT_OK(found_key.FullyDecodeFrom(iter->key()));

    rocksdb::Slice value = iter->value();

    if (high_ts < found_key.hybrid_time()) {
      found_key.set_hybrid_time(high_ts);
      SeekForward(found_key.Encode(), iter);
      continue;
    }

    if (low_ts > found_key.hybrid_time()) {
      SeekPastSubKey(found_key, iter);
      continue;
    }

    RETURN_NOT_OK(doc_value.Decode(value));
    if (encoded_key.OnlyLacksHybridTimeFrom(iter->key())) {

      MonoDelta ttl;
      RETURN_NOT_OK(Value::DecodeTTL(&value, &ttl));
      ttl = ComputeTTL(ttl, table_ttl);

      HybridTime write_time = found_key.hybrid_time();

      if (!ttl.Equals(Value::kMaxTtl)) {
        const HybridTime expiry =
            server::HybridClock::AddPhysicalTimeToHybridTime(found_key.hybrid_time(), ttl);
        if (high_ts.CompareTo(expiry) > 0) {
          // Treat the value as a tombstone written at expiry time.
          if (low_ts > expiry) {
            // We should have expiry > hybrid time from key > low_ts.
            return STATUS_SUBSTITUTE(Corruption,
                "Unexpected expiry time $0 found, should be higher than $1",
                expiry.ToString(), low_ts.ToString());
          }
          doc_value = Value(PrimitiveValue(ValueType::kTombstone));
          write_time = expiry;
        }
      }

      // We have found some key that matches our entire subdocument_key, i.e. we didn't skip ahead
      // to a lower level key (with optional object init markers).
      if (doc_value.value_type() == ValueType::kObject ||
          doc_value.value_type() == ValueType::kTombstone) {
        if (low_ts < write_time) {
          low_ts = write_time;
        }
        if (doc_value.value_type() == ValueType::kObject) {
          *subdocument = SubDocument();
        }
        SeekPastSubKey(found_key, iter);
        continue;
      } else {
        if (!IsPrimitiveValueType(doc_value.value_type())) {
          return STATUS_SUBSTITUTE(Corruption,
              "Expected primitive value type, got $0", ValueTypeToStr(doc_value.value_type()));
        }
        *subdocument = SubDocument(doc_value.primitive_value());
        SeekForward(found_key.AdvanceOutOfSubDoc(), iter);
        return Status::OK();
      }
    }

    SubDocument descendant = SubDocument(PrimitiveValue(ValueType::kInvalidValueType));
    found_key.remove_hybrid_time();

    RETURN_NOT_OK(BuildSubDocument(iter, found_key, &descendant, high_ts, low_ts, table_ttl));
    if (descendant.value_type() == ValueType::kInvalidValueType) {
      // The document was not found in this level (maybe a tombstone was encountered).
      continue;
    }
    if (subdocument->value_type() != ValueType::kObject) {
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
    SubDocument *result,
    bool *doc_found,
    HybridTime scan_ht,
    MonoDelta table_ttl) {
  auto iter = CreateRocksDBIterator(db);
  iter->SeekToFirst();
  return GetSubDocument(iter.get(), subdocument_key, result, doc_found, scan_ht, table_ttl);
}

yb::Status GetSubDocument(rocksdb::Iterator *iterator,
    const SubDocKey& subdocument_key,
    SubDocument *result,
    bool *doc_found,
    HybridTime scan_ht,
    MonoDelta table_ttl,
    const vector<PrimitiveValue>* projection) {
  *doc_found = false;
  DOCDB_DEBUG_LOG("GetSubDocument for key $0", subdocument_key.ToString());
  HybridTime max_deleted_ts = HybridTime::kMin;

  SubDocKey found_subdoc_key;
  Value doc_value;
  bool is_found = false;

  KeyBytes key_bytes = subdocument_key.doc_key().Encode();

  SeekForward(key_bytes, iterator);

  // Check ancestors for init markers and tombstones, update max_deleted_ts with them.
  for (const PrimitiveValue& subkey : subdocument_key.subkeys()) {
    const HybridTime overwrite_ts = FindLastWriteTime(key_bytes, scan_ht, iterator);
    if (max_deleted_ts < overwrite_ts) {
      max_deleted_ts = overwrite_ts;
    }
    subkey.AppendToKey(&key_bytes);
  }
  if (projection == nullptr) {
    // This is only to initialize the iterator properly for BuildSubDocument call.
    SeekForward(key_bytes, iterator);
    *result = SubDocument(ValueType::kInvalidValueType);
    RETURN_NOT_OK(BuildSubDocument(iterator, subdocument_key, result, scan_ht, max_deleted_ts,
        table_ttl));
    *doc_found = result->value_type() != ValueType::kInvalidValueType;

    return Status::OK();
  }
  // Check for init-marker / tombstones at the top level, update max_deleted_ts.
  HybridTime overwrite_ts = FindLastWriteTime(key_bytes, scan_ht, iterator);
  if (max_deleted_ts < overwrite_ts) {
    max_deleted_ts = overwrite_ts;
  }
  // For each subkey in the projection, build subdocument.
  *result = SubDocument();
  for (const PrimitiveValue& subkey : *projection) {
    SubDocument descendant(ValueType::kInvalidValueType);
    SubDocKey projection_subdockey = subdocument_key;
    projection_subdockey.AppendSubKeysAndMaybeHybridTime(subkey);
    // This seek is to initialize the iterator for BuildSubDocument call.
    SeekForward(projection_subdockey.Encode(/* include_hybrid_time */ false), iterator);
    RETURN_NOT_OK(BuildSubDocument(iterator,
        projection_subdockey, &descendant, scan_ht, max_deleted_ts, table_ttl));
    if (descendant.value_type() != ValueType::kInvalidValueType) {
      *doc_found = true;
    }
    result->SetChild(subkey, std::move(descendant));
  }
  // Make sure the iterator is placed outside the whole document in the end.
  SeekForward(subdocument_key.AdvanceOutOfSubDoc(), iterator);
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

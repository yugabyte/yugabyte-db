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

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/common/hybrid_time.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/internal_doc_iterator.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/server/hybrid_clock.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/date_time.h"
#include "yb/util/logging.h"
#include "yb/util/shared_lock_manager.h"
#include "yb/util/status.h"

using std::endl;
using std::list;
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

// When we replace HybridTime::kMin in the end of seek key, next seek will skip older versions of
// this key, but will not skip any subkeys in its subtree. If the iterator is already positioned far
// enough, does not perform a seek.
void SeekPastSubKey(const SubDocKey& sub_doc_key, rocksdb::Iterator* iter) {
  KeyBytes key_bytes = sub_doc_key.Encode(/* include_hybrid_time */ false);
  AppendDocHybridTime(DocHybridTime::kMin, &key_bytes);
  SeekForward(key_bytes, iter);
}

}  // namespace


void PrepareDocWriteTransaction(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                                util::SharedLockManager *lock_manager,
                                LockBatch *keys_locked,
                                bool *need_read_snapshot) {
  *need_read_snapshot = false;
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    const list<DocPath> doc_paths = doc_op->DocPathsToLock();
    for (const auto& doc_path : doc_paths) {
      KeyBytes current_prefix = doc_path.encoded_doc_key();
      for (int i = 0; i < doc_path.num_subkeys(); i++) {
        const string& lock_string = current_prefix.AsStringRef();
        keys_locked->emplace(lock_string, LockType::SI_WRITE_WEAK);
        doc_path.subkey(i).AppendToKey(&current_prefix);
      }
      const string& lock_string = current_prefix.AsStringRef();
      (*keys_locked)[lock_string] = LockType::SI_WRITE_STRONG;
    }
    if (doc_op->RequireReadSnapshot()) {
      *need_read_snapshot = true;
    }
  }
  lock_manager->Lock(*keys_locked);
}

Status ApplyDocWriteTransaction(const vector<unique_ptr<DocOperation>>& doc_write_ops,
                                const HybridTime& hybrid_time,
                                rocksdb::DB *rocksdb,
                                KeyValueWriteBatchPB* write_batch,
                                std::atomic<int64_t>* monotonic_counter) {
  DocWriteBatch doc_write_batch(rocksdb, monotonic_counter);
  for (const unique_ptr<DocOperation>& doc_op : doc_write_ops) {
    RETURN_NOT_OK(doc_op->Apply(&doc_write_batch, rocksdb, hybrid_time));
  }
  doc_write_batch.MoveToWriteBatchPB(write_batch);
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// DocWriteBatch
// ------------------------------------------------------------------------------------------------

DocWriteBatch::DocWriteBatch(rocksdb::DB* rocksdb, std::atomic<int64_t>* monotonic_counter)
    : rocksdb_(rocksdb),
      monotonic_counter_(monotonic_counter),
      num_rocksdb_seeks_(0) {
}

CHECKED_STATUS DocWriteBatch::SetPrimitiveInternal(
    const DocPath& doc_path,
    const Value& value,
    InternalDocIterator *doc_iter,
    const bool is_deletion,
    const int num_subkeys,
    InitMarkerBehavior use_init_marker) {

  // The write_id is always incremented by one for each new element of the write batch.
  if (put_batch_.size() > numeric_limits<IntraTxnWriteId>::max()) {
    return STATUS_SUBSTITUTE(
        NotSupported,
        "Trying to add more than $0 key/value pairs in the same single-shard txn.",
        numeric_limits<IntraTxnWriteId>::max());
  }

  const auto write_id = static_cast<IntraTxnWriteId>(put_batch_.size());
  const DocHybridTime hybrid_time =
      DocHybridTime(HybridTime::kMax, write_id);

  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    // We don't need to check if intermediate documents already exist if init markers are optional,
    // or if we already know they exist (either from previous reads or our own writes in the same
    // single-shard txn.)
    if (use_init_marker == InitMarkerBehavior::OPTIONAL || doc_iter->subdoc_exists()) {
      if (use_init_marker == InitMarkerBehavior::REQUIRED &&
          doc_iter->subdoc_type() != ValueType::kObject &&
          doc_iter->subdoc_type() != ValueType::kRedisSet) {
        // We raise this error only if init markers are mandatory.
        return STATUS_FORMAT(IllegalState, "Cannot set values inside a subdocument of type $0",
            doc_iter->subdoc_type());
      }
      if ((subkey_index == num_subkeys - 1 && !is_deletion) ||
          use_init_marker == InitMarkerBehavior::OPTIONAL) {
        // We don't need to perform a RocksDB read at the last level for upserts, we just overwrite
        // the value within the last subdocument with what we're trying to write. We still perform
        // the read for deletions, because we try to avoid writing a new tombstone if the data is
        // not there anyway. Apart from the above case, if init markers are optional, there is no
        // point in seeking to intermediate document levels to verify their existence.
        if (use_init_marker == InitMarkerBehavior::OPTIONAL) {
          // In the case where init markers are optional, we don't need to check existence of
          // the current subdocument.
          doc_iter->AppendToPrefix(subkey);
        } else {
          // TODO: convert CHECKs inside the function below to a returned Status.
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

      // Add the parent key to key/value batch before appending the encoded HybridTime to it.
      // (We replicate key/value pairs without the HybridTime and only add it before writing to
      // RocksDB.)
      put_batch_.emplace_back(std::move(*parent_key.mutable_data()), kObjectValueType);

      // Update our local cache to record the fact that we're adding this subdocument, so that
      // future operations in this DocWriteBatch don't have to add it or look for it in RocksDB.
      cache_.Put(KeyBytes(doc_iter->key_prefix().AsSlice()), hybrid_time, ValueType::kObject);

      doc_iter->AppendToPrefix(subkey);
    }
  }

  // The key we use in the DocWriteBatchCache does not have a final hybrid_time, because that's the
  // key we expect to look up.
  cache_.Put(doc_iter->key_prefix(), hybrid_time, value.primitive_value().value_type());

  // The key in the key/value batch does not have an encoded HybridTime.
  put_batch_.emplace_back(doc_iter->key_prefix().AsStringRef(), value.Encode());

  return Status::OK();
}

Status DocWriteBatch::SetPrimitive(const DocPath& doc_path,
                                   const Value& value,
                                   InitMarkerBehavior use_init_marker) {
  DOCDB_DEBUG_LOG("Called with doc_path=$0, value=$1",
                  doc_path.ToString(), value.ToString());
  const KeyBytes& encoded_doc_key = doc_path.encoded_doc_key();
  const int num_subkeys = doc_path.num_subkeys();
  const bool is_deletion = value.primitive_value().value_type() == ValueType::kTombstone;

  InternalDocIterator doc_iter(rocksdb_, &cache_, BloomFilterMode::USE_BLOOM_FILTER,
      encoded_doc_key, rocksdb::kDefaultQueryId, &num_rocksdb_seeks_);

  if (num_subkeys > 0 || is_deletion) {
    doc_iter.SetDocumentKey(encoded_doc_key);
    if (use_init_marker == InitMarkerBehavior::REQUIRED) {
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

  return SetPrimitiveInternal(doc_path, value, &doc_iter, is_deletion, num_subkeys,
                              use_init_marker);
}

Status DocWriteBatch::ExtendSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    InitMarkerBehavior use_init_marker,
    MonoDelta ttl) {
  if (value.value_type() == ValueType::kObject || value.value_type() == ValueType::kRedisSet) {
    const auto& map = value.object_container();
    for (const auto& ent : map) {
      DocPath child_doc_path = doc_path;
      child_doc_path.AddSubKey(ent.first);
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, ent.second, use_init_marker, ttl));
    }
  } else if (value.value_type() == ValueType::kArray) {
      RETURN_NOT_OK(ExtendList(doc_path, value, ListExtendOrder::APPEND, use_init_marker, ttl));
  } else {
    if (!value.IsPrimitive() && value.value_type() != ValueType::kTombstone) {
      return STATUS_FORMAT(InvalidArgument,
          "Found unexpected value type $0. Expecting a PrimitiveType or a Tombstone",
          value.value_type());
    }
    RETURN_NOT_OK(SetPrimitive(doc_path, Value(value, ttl), use_init_marker));
  }
  return Status::OK();
}

Status DocWriteBatch::InsertSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    InitMarkerBehavior use_init_marker,
    MonoDelta ttl) {
  if (!value.IsPrimitive() && value.value_type() != ValueType::kTombstone) {
    RETURN_NOT_OK(SetPrimitive(
        doc_path, Value(PrimitiveValue(value.value_type()), ttl), use_init_marker));
  }
  return ExtendSubDocument(doc_path, value, use_init_marker, ttl);
}

Status DocWriteBatch::DeleteSubDoc(
    const DocPath& doc_path,
    InitMarkerBehavior use_init_marker) {
  return SetPrimitive(doc_path, PrimitiveValue(ValueType::kTombstone), use_init_marker);
}

Status DocWriteBatch::ExtendList(
    const DocPath& doc_path,
    const SubDocument& value,
    ListExtendOrder extend_order,
    InitMarkerBehavior use_init_marker,
    MonoDelta ttl) {
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
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, list[i], use_init_marker, ttl));
    }
  } else { // PREPEND - adding in reverse order with negated index
    for (size_t i = list.size(); i > 0; i--) {
      DocPath child_doc_path = doc_path;
      index++;
      child_doc_path.AddSubKey(PrimitiveValue::ArrayIndex(-index));
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, list[i - 1], use_init_marker, ttl));
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
    MonoDelta write_ttl,
    InitMarkerBehavior use_init_marker) {
  SubDocKey sub_doc_key;
  RETURN_NOT_OK(sub_doc_key.FromDocPath(doc_path));
  KeyBytes key_bytes = sub_doc_key.Encode( /*include_hybrid_time =*/ false);
  // Ensure we seek directly to indexes and skip init marker if it exists
  key_bytes.AppendValueType(ValueType::kArrayIndex);
  rocksdb::Slice seek_key = key_bytes.AsSlice();
  auto iter = CreateRocksDBIterator(rocksdb_, BloomFilterMode::USE_BLOOM_FILTER, seek_key,
      query_id);
  SubDocKey found_key;
  Value found_value;
  int current_index = 0;
  int replace_index = 0;
  ROCKSDB_SEEK(iter.get(), seek_key);
  while (true) {
    SubDocKey found_key;
    Value doc_value;

    if (indexes[replace_index] <= 0 || !iter->Valid() || !iter->key().starts_with(seek_key)) {
      return STATUS_SUBSTITUTE(
          QLError,
          "Unable to replace items into list, expecting index $0, reached end of list with size $1",
          indexes[replace_index],
          current_index);
    }

    RETURN_NOT_OK(found_key.FullyDecodeFrom(iter->key()));
    MonoDelta entry_ttl;
    rocksdb::Slice rocksdb_value = iter->value();
    RETURN_NOT_OK(Value::DecodeTTL(&rocksdb_value, &entry_ttl));
    entry_ttl = ComputeTTL(entry_ttl, table_ttl);

    if (!entry_ttl.Equals(Value::kMaxTtl)) {
      const HybridTime expiry =
          server::HybridClock::AddPhysicalTimeToHybridTime(found_key.hybrid_time(), entry_ttl);
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
      RETURN_NOT_OK(InsertSubDocument(child_doc_path, values[replace_index], use_init_marker,
          write_ttl));
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

int DocWriteBatch::GetAndResetNumRocksDBSeeks() {
  const int ret_val = num_rocksdb_seeks_;
  num_rocksdb_seeks_ = 0;
  return ret_val;
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

  return STATUS_FORMAT(Corruption, "Invalid value type at the top level of a document: $0",
      top_level_value.primitive_value().value_type());
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
      subdoc_key.SetHybridTimeForReadPath(scan_ht);
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
    const rocksdb::QueryId query_id,
    HybridTime scan_ht) {
  // TODO: Use a SubDocKey API to build the proper seek key without assuming anything about the
  //       internal structure of SubDocKey here.
  KeyBytes seek_key(subdocument_key);

  SubDocKey found_subdoc_key;
  Value doc_value;
  bool is_found = false;

  const Slice seek_key_as_slice = seek_key.AsSlice();
  auto rocksdb_iter = CreateRocksDBIterator(rocksdb, BloomFilterMode::USE_BLOOM_FILTER,
      seek_key_as_slice, query_id);

  Status s = SeekToValidKvAtTs(rocksdb_iter.get(), seek_key_as_slice, scan_ht, &found_subdoc_key,
                               &is_found, &doc_value);

  {
    bool found_same_subdoc_key_with_hybrid_time = false;
    RETURN_NOT_OK(subdocument_key.OnlyLacksHybridTimeFrom(
        rocksdb_iter->key(), &found_same_subdoc_key_with_hybrid_time));

    if (!is_found || !found_same_subdoc_key_with_hybrid_time) {
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
      // Then we'll jump directly here as we try to retrieve the document state at hybrid_time 3: -/
      //
      // The right thing to do here is just to return, assuming the document does not exist.
      return Status::OK();
    }
  }

  RETURN_NOT_OK(visitor->StartSubDocument(found_subdoc_key));
  RETURN_NOT_OK(ScanPrimitiveValueOrObject(
      found_subdoc_key, rocksdb_iter.get(), visitor, scan_ht, found_subdoc_key.hybrid_time()));
  RETURN_NOT_OK(visitor->EndSubDocument());

  return Status::OK();
}

namespace {

// If there is a key equal to key_bytes + some timestamp, we return its hybridtime.
// This should not be used for leaf nodes.
// TODO: We could also check that the value is kTombStone or kObject type for sanity checking.
Status FindLastWriteTime(
    const KeyBytes& key_bytes,
    HybridTime scan_ht,
    rocksdb::Iterator* iter,
    DocHybridTime* hybrid_time) {
  {
    KeyBytes key_with_ts = key_bytes;
    key_with_ts.AppendValueType(ValueType::kHybridTime);
    key_with_ts.AppendHybridTimeForSeek(scan_ht);
    SeekForward(key_with_ts, iter);
  }

  if (iter->Valid()) {
    bool only_lacks_ht = false;
        RETURN_NOT_OK(key_bytes.OnlyLacksHybridTimeFrom(iter->key(), &only_lacks_ht));
    if (only_lacks_ht) {
          RETURN_NOT_OK(DecodeHybridTimeFromEndOfKey(iter->key(), hybrid_time));
      // TODO when we support TTL on non-leaf nodes, we need to take that into account here.
      return Status::OK();
    }
  }

  *hybrid_time = DocHybridTime::kMin;
  return Status::OK();
}

// This works similar to the ScanSubDocument function, but doesn't assume that object init_markers
// are present. If no init marker is present, or if a tombstone is found at some level,
// it still looks for subkeys inside it if they have larger timestamps.
// TODO(akashnil): ENG-1152: If object init markers were required, this read path may be optimized.
// We look at all rocksdb keys with prefix = subdocument_key, and construct a subdocument out of
// them, between the timestamp range high_ts and low_ts.
//
// The iterator is expected to be placed at the smallest key that is subdocument_key or later, and
// after the function returns, the iterator should be placed just completely outside the
// subdocument_key prefix.
yb::Status BuildSubDocument(
    rocksdb::Iterator* iter,
    const SubDocKey &subdocument_key,
    SubDocument* subdocument,
    HybridTime high_ts,
    DocHybridTime low_ts,
    MonoDelta table_ttl) {
  DCHECK(!subdocument_key.has_hybrid_time());
  DOCDB_DEBUG_LOG("subdocument_key=$0, high_ts=$1, low_ts=$2, table_ttl=$3",
                  subdocument_key.ToString(),
                  high_ts.ToDebugString(),
                  low_ts.ToString(),
                  table_ttl.ToString());
  // TODO: rename this so that it is clear it does not have a hybrid time at the end.
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
      found_key.SetHybridTimeForReadPath(high_ts);
      SeekForward(found_key.Encode(), iter);
      continue;
    }

    if (low_ts > found_key.doc_hybrid_time()) {
      SeekPastSubKey(found_key, iter);
      continue;
    }

    RETURN_NOT_OK(doc_value.Decode(value));

    bool only_lacks_ht = false;
    RETURN_NOT_OK(encoded_key.OnlyLacksHybridTimeFrom(iter->key(), &only_lacks_ht));
    if (only_lacks_ht) {

      MonoDelta ttl;
      RETURN_NOT_OK(Value::DecodeTTL(&value, &ttl));

      ttl = ComputeTTL(ttl, table_ttl);

      DocHybridTime write_time = found_key.doc_hybrid_time();

      if (!ttl.Equals(Value::kMaxTtl)) {
        const HybridTime expiry =
            server::HybridClock::AddPhysicalTimeToHybridTime(found_key.hybrid_time(), ttl);
        if (high_ts.CompareTo(expiry) > 0) {
          // Treat the value as a tombstone written at expiry time.
          if (low_ts.hybrid_time() > expiry) {
            // We should have expiry > hybrid time from key > low_ts.
            return STATUS_SUBSTITUTE(Corruption,
                "Unexpected expiry time $0 found, should be higher than $1",
                expiry.ToString(), low_ts.ToString());
          }
          doc_value = Value(PrimitiveValue(ValueType::kTombstone));
          // Use a write id that could never be used by a real operation within a single-shard txn,
          // so that we don't split that operation into multiple parts.
          write_time = DocHybridTime(expiry, kMaxWriteId);
        }
      }

      // We have found some key that matches our entire subdocument_key, i.e. we didn't skip ahead
      // to a lower level key (with optional object init markers).
      if (doc_value.value_type() == ValueType::kObject ||
          doc_value.value_type() == ValueType::kArray ||
          doc_value.value_type() == ValueType::kRedisSet ||
          doc_value.value_type() == ValueType::kTombstone) {
        if (low_ts < write_time) {
          low_ts = write_time;
        }
        if (doc_value.value_type() == ValueType::kObject ||
            doc_value.value_type() == ValueType::kArray ||
            doc_value.value_type() == ValueType::kRedisSet) {
          *subdocument = SubDocument(doc_value.value_type());
        }
        SeekPastSubKey(found_key, iter);
        continue;
      } else {
        if (!IsPrimitiveValueType(doc_value.value_type())) {
          return STATUS_FORMAT(Corruption,
              "Expected primitive value type, got $0", doc_value.value_type());
        }

        DCHECK_GE(high_ts, write_time.hybrid_time());
        if (ttl.Equals(Value::kMaxTtl)) {
          doc_value.mutable_primitive_value()->SetTtl(-1);
        } else {
          int64_t time_since_write_seconds = (server::HybridClock::GetPhysicalValueMicros(high_ts) -
              server::HybridClock::GetPhysicalValueMicros(write_time.hybrid_time())) /
              MonoTime::kMicrosecondsPerSecond;
          int64_t ttl_seconds = std::max(static_cast<int64_t>(0),
              ttl.ToMilliseconds()/MonoTime::kMillisecondsPerSecond - time_since_write_seconds);
          doc_value.mutable_primitive_value()->SetTtl(ttl_seconds);
        }
        doc_value.mutable_primitive_value()->SetWritetime(write_time.hybrid_time().ToUint64());
        *subdocument = SubDocument(doc_value.primitive_value());
        SeekForward(found_key.AdvanceOutOfSubDoc(), iter);
        return Status::OK();
      }
    }

    SubDocument descendant = SubDocument(PrimitiveValue(ValueType::kInvalidValueType));
    // TODO: what if found_key is the same as before? We'll get into an infinite recursion then.
    found_key.remove_hybrid_time();

    RETURN_NOT_OK(BuildSubDocument(iter, found_key, &descendant, high_ts, low_ts, table_ttl));
    if (descendant.value_type() == ValueType::kInvalidValueType) {
      // The document was not found in this level (maybe a tombstone was encountered).
      continue;
    }
    if (subdocument->value_type() != ValueType::kObject &&
        subdocument->value_type() != ValueType::kRedisSet) {
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
    const rocksdb::QueryId query_id,
    HybridTime scan_ht,
    MonoDelta table_ttl,
    bool return_type_only) {
  const auto doc_key_encoded = subdocument_key.doc_key().Encode();
  auto iter = CreateRocksDBIterator(db, BloomFilterMode::USE_BLOOM_FILTER,
      doc_key_encoded.AsSlice(), query_id);
  return GetSubDocument(iter.get(), subdocument_key, result, doc_found, scan_ht, table_ttl,
      nullptr, return_type_only, false);
}

yb::Status GetSubDocument(
    rocksdb::Iterator *rocksdb_iter,
    const SubDocKey& subdocument_key,
    SubDocument *result,
    bool *doc_found,
    HybridTime scan_ht,
    MonoDelta table_ttl,
    const vector<PrimitiveValue>* projection,
    bool return_type_only,
    const bool is_iter_valid) {
  *doc_found = false;
  DOCDB_DEBUG_LOG("GetSubDocument for key $0 @ $1", subdocument_key.ToString(),
      scan_ht.ToDebugString());
  DocHybridTime max_deleted_ts(DocHybridTime::kMin);

  SubDocKey found_subdoc_key;

  DCHECK(!subdocument_key.has_hybrid_time());
  KeyBytes key_bytes = subdocument_key.doc_key().Encode();

  if (is_iter_valid) {
    SeekForward(key_bytes, rocksdb_iter);
  } else {
    ROCKSDB_SEEK(rocksdb_iter, key_bytes.AsSlice());
  }

  // Check ancestors for init markers and tombstones, update max_deleted_ts with them.
  for (const PrimitiveValue& subkey : subdocument_key.subkeys()) {
    DocHybridTime overwrite_ts;
    RETURN_NOT_OK(FindLastWriteTime(key_bytes, scan_ht, rocksdb_iter, &overwrite_ts));
    if (max_deleted_ts < overwrite_ts) {
      max_deleted_ts = overwrite_ts;
    }
    subkey.AppendToKey(&key_bytes);
  }

  // By this point key_bytes is the encoded representation of the DocKey and all the subkeys of
  // subdocument_key.

  // Check for init-marker / tombstones at the top level, update max_deleted_ts.
  DocHybridTime overwrite_ts;
  RETURN_NOT_OK(FindLastWriteTime(key_bytes, scan_ht, rocksdb_iter, &overwrite_ts));
  Value doc_value = Value(PrimitiveValue(ValueType::kInvalidValueType));
  if (max_deleted_ts < overwrite_ts) {
    max_deleted_ts = overwrite_ts;
    if (rocksdb_iter->key().starts_with(key_bytes.AsSlice())) {
      RETURN_NOT_OK(doc_value.Decode(rocksdb_iter->value()));
    }
  }

  if (return_type_only) {
    *doc_found = doc_value.value_type() != ValueType::kInvalidValueType;
    *result = SubDocument(doc_value.primitive_value());
    return Status::OK();
  }

  if (projection == nullptr) {
    *result = SubDocument(ValueType::kInvalidValueType);
    RETURN_NOT_OK(BuildSubDocument(rocksdb_iter, subdocument_key, result, scan_ht, max_deleted_ts,
        table_ttl));
    *doc_found = result->value_type() != ValueType::kInvalidValueType;
    if (*doc_found && doc_value.value_type() == ValueType::kRedisSet) {
      RETURN_NOT_OK(result->ConvertToRedisSet());
    }
    // TODO: Also could handle lists here.

    return Status::OK();
  }
  // For each subkey in the projection, build subdocument.
  *result = SubDocument();
  for (const PrimitiveValue& subkey : *projection) {
    SubDocument descendant(ValueType::kInvalidValueType);
    SubDocKey projection_subdockey = subdocument_key;
    projection_subdockey.AppendSubKeysAndMaybeHybridTime(subkey);
    // This seek is to initialize the iterator for BuildSubDocument call.
    SeekForward(projection_subdockey.Encode(/* include_hybrid_time */ false), rocksdb_iter);
    RETURN_NOT_OK(BuildSubDocument(rocksdb_iter,
        projection_subdockey, &descendant, scan_ht, max_deleted_ts, table_ttl));
    if (descendant.value_type() != ValueType::kInvalidValueType) {
      *doc_found = true;
    }
    result->SetChild(subkey, std::move(descendant));
  }
  // Make sure the iterator is placed outside the whole document in the end.
  SeekForward(subdocument_key.AdvanceOutOfSubDoc(), rocksdb_iter);
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Debug output
// ------------------------------------------------------------------------------------------------

Status DocDBDebugDump(rocksdb::DB* rocksdb, ostream& out, const bool include_binary) {
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
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
      out << "Error: failed to decode value for key " << subdoc_key.ToString()
          << ": " << value_decode_status.ToString() << endl;
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

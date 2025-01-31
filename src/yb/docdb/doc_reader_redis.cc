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

#include "yb/docdb/doc_reader_redis.h"

#include <string>
#include <vector>

#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/deadline_info.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/read_operation_data.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_ttl_util.h"
#include "yb/dockv/subdocument.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

using std::vector;

namespace yb {
namespace docdb {

using dockv::SubDocKey;
using dockv::SubDocument;
using dockv::ValueControlFields;
using dockv::ValueEntryType;

const SliceKeyBound& SliceKeyBound::Invalid() {
  static SliceKeyBound result;
  return result;
}

std::string SliceKeyBound::ToString() const {
  if (!is_valid()) {
    return "{ empty }";
  }
  return Format("{ $0$1 $2 }", is_lower() ? ">" : "<", is_exclusive() ? "" : "=",
                SubDocKey::DebugSliceToString(key_));
}

const IndexBound& IndexBound::Empty() {
  static IndexBound result;
  return result;
}


// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

namespace {

void SeekToLowerBound(const SliceKeyBound& lower_bound, IntentAwareIterator* iter) {
  if (lower_bound.is_exclusive()) {
    iter->SeekPastSubKey(lower_bound.key());
  } else {
    iter->SeekForward(lower_bound.key());
  }
}

// This function does not assume that object init_markers are present. If no init marker is present,
// or if a tombstone is found at some level, it still looks for subkeys inside it if they have
// larger timestamps.
//
// TODO(akashnil): ENG-1152: If object init markers were required, this read path may be optimized.
// We look at all rocksdb keys with prefix = subdocument_key, and construct a subdocument out of
// them, between the timestamp range high_ts and low_ts.
//
// The iterator is expected to be placed at the smallest key that is subdocument_key or later, and
// after the function returns, the iterator should be placed just completely outside the
// subdocument_key prefix. Although if high_subkey is specified, the iterator is only guaranteed
// to be positioned after the high_subkey and not necessarily outside the subdocument_key prefix.
// num_values_observed is used for queries on indices, and keeps track of the number of primitive
// values observed thus far. In a query with lower index bound k, ignore the first k primitive
// values before building the subdocument.
Status BuildSubDocument(
    IntentAwareIterator* iter,
    const GetRedisSubDocumentData& data,
    DocHybridTime low_ts,
    int64* num_values_observed) {
  VLOG(3) << "BuildSubDocument data: " << data << " read_time: " << iter->read_time()
          << " low_ts: " << low_ts;
  for (;;) {
    auto key_data = VERIFY_RESULT_REF(iter->Fetch());
    if (!key_data) {
      break;
    }
    if (data.deadline_info) {
      RETURN_NOT_OK(data.deadline_info->CheckDeadlinePassed());
    }
    // Since we modify num_values_observed on recursive calls, we keep a local copy of the value.
    int64 current_values_observed = *num_values_observed;
    auto key = key_data.key;
    const auto write_time = VERIFY_RESULT(key_data.write_time.Decode());
    VLOG(4) << "iter: " << SubDocKey::DebugSliceToString(key)
            << ", key: " << SubDocKey::DebugSliceToString(data.subdocument_key);
    DCHECK(key.starts_with(data.subdocument_key))
        << "iter: " << SubDocKey::DebugSliceToString(key)
        << ", key: " << SubDocKey::DebugSliceToString(data.subdocument_key);

    // Key could be invalidated because we could move iterator, so back it up.
    dockv::KeyBytes key_copy(key);
    key = key_copy.AsSlice();
    Slice value = key_data.value;
    // Checking that IntentAwareIterator returns an entry with correct time.
    DCHECK(key_data.same_transaction ||
           iter->read_time().global_limit >= write_time.hybrid_time())
        << "Bad key: " << SubDocKey::DebugSliceToString(key)
        << ", global limit: " << iter->read_time().global_limit
        << ", write time: " << write_time.hybrid_time();

    if (low_ts > write_time) {
      VLOG(3) << "SeekPastSubKey: " << SubDocKey::DebugSliceToString(key);
      iter->SeekPastSubKey(key);
      continue;
    }
    dockv::Value doc_value;
    RETURN_NOT_OK(doc_value.Decode(value));
    auto value_type = doc_value.value_type();
    if (key == data.subdocument_key) {
      if (write_time == DocHybridTime::kMin)
        return STATUS(Corruption, "No hybrid timestamp found on entry");

      // We may need to update the TTL in individual columns.
      if (write_time.hybrid_time() >= data.exp.write_ht) {
        // We want to keep the default TTL otherwise.
        if (doc_value.ttl() != ValueControlFields::kMaxTtl) {
          data.exp.write_ht = write_time.hybrid_time();
          data.exp.ttl = doc_value.ttl();
        } else if (data.exp.ttl.IsNegative()) {
          data.exp.ttl = -data.exp.ttl;
        }
      }

      // If the hybrid time is kMin, then we must be using default TTL.
      if (data.exp.write_ht == HybridTime::kMin) {
        data.exp.write_ht = write_time.hybrid_time();
      }

      // Treat an expired value as a tombstone written at the same time as the original value.
      if (dockv::HasExpiredTTL(data.exp.write_ht, data.exp.ttl, iter->read_time().read)) {
        doc_value = dockv::Value::Tombstone();
        value_type = ValueEntryType::kTombstone;
      }

      const bool is_collection = IsCollectionType(value_type);
      // We have found some key that matches our entire subdocument_key, i.e. we didn't skip ahead
      // to a lower level key (with optional object init markers).
      if (is_collection || value_type == ValueEntryType::kTombstone) {
        if (low_ts < write_time) {
          low_ts = write_time;
        }
        if (is_collection) {
          *data.result = SubDocument(value_type);
        }

        // If the subkey lower bound filters out the key we found, we want to skip to the lower
        // bound. If it does not, we want to seek to the next key. This prevents an infinite loop
        // where the iterator keeps seeking to itself if the key we found matches the low subkey.
        // TODO: why are not we doing this for arrays?
        if (IsObjectType(value_type) && !data.low_subkey->CanInclude(key)) {
          // Try to seek to the low_subkey for efficiency.
          SeekToLowerBound(*data.low_subkey, iter);
        } else {
          VLOG(3) << "SeekPastSubKey: " << SubDocKey::DebugSliceToString(key);
          iter->SeekPastSubKey(key);
        }
        continue;
      } else if (IsPrimitiveValueType(value_type)) {
        // Choose the user supplied timestamp if present.
        doc_value.mutable_primitive_value()->SetWriteTime(
            doc_value.has_timestamp()
                ? doc_value.timestamp()
                : write_time.hybrid_time().GetPhysicalValueMicros());
        if (!data.high_index->CanInclude(current_values_observed)) {
          iter->SeekOutOfSubDoc(SeekFilter::kAll, &key_copy);
          DCHECK(iter->Fetch().ok()); // Enforce call to Fetch in debug mode
          return Status::OK();
        }
        if (data.low_index->CanInclude(*num_values_observed)) {
          *data.result = SubDocument(doc_value.primitive_value());
        }
        (*num_values_observed)++;
        VLOG(3) << "SeekOutOfSubDoc: " << SubDocKey::DebugSliceToString(key);
        iter->SeekOutOfSubDoc(SeekFilter::kAll, &key_copy);
        DCHECK(iter->Fetch().ok()); // Enforce call to Fetch in debug mode
        return Status::OK();
      } else {
        return STATUS_FORMAT(Corruption, "Expected primitive value type, got $0", value_type);
      }
    }
    SubDocument descendant{dockv::PrimitiveValue(ValueEntryType::kInvalid)};
    // TODO: what if the key we found is the same as before?
    //       We'll get into an infinite recursion then.
    {
      char highest = dockv::KeyEntryTypeAsChar::kHighest;
      KeyBuffer upperbound_buffer(key, Slice(&highest, 1));
      IntentAwareIteratorUpperboundScope upperbound_scope(upperbound_buffer.AsSlice(), iter);
      RETURN_NOT_OK(BuildSubDocument(
          iter, data.Adjusted(key, &descendant), low_ts,
          num_values_observed));
    }
    iter->Revalidate(SeekFilter::kAll);
    if (descendant.value_type() == ValueEntryType::kInvalid) {
      // The document was not found in this level (maybe a tombstone was encountered).
      continue;
    }

    if (!data.low_subkey->CanInclude(key)) {
      VLOG(3) << "Filtered by low_subkey: " << data.low_subkey->ToString()
              << ", key: " << SubDocKey::DebugSliceToString(key);
      // The value provided is lower than what we are looking for, seek to the lower bound.
      SeekToLowerBound(*data.low_subkey, iter);
      continue;
    }

    // We use num_values_observed as a conservative figure for lower bound and
    // current_values_observed for upper bound so we don't lose any data we should be including.
    if (!data.low_index->CanInclude(*num_values_observed)) {
      continue;
    }

    if (!data.high_subkey->CanInclude(key)) {
      VLOG(3) << "Filtered by high_subkey: " << data.high_subkey->ToString()
              << ", key: " << SubDocKey::DebugSliceToString(key);
      // We have encountered a subkey higher than our constraints, we should stop here.
      return Status::OK();
    }

    if (!data.high_index->CanInclude(current_values_observed)) {
      return Status::OK();
    }

    if (!IsObjectType(data.result->value_type())) {
      *data.result = SubDocument();
    }

    SubDocument* current = data.result;
    size_t num_children;
    RETURN_NOT_OK(current->NumChildren(&num_children));
    if (data.limit != 0 && num_children >= data.limit) {
      // We have processed enough records.
      return Status::OK();
    }

    if (data.count_only) {
      // We need to only count the records that we found.
      data.record_count++;
    } else {
      Slice temp = key;
      temp.remove_prefix(data.subdocument_key.size());
      for (;;) {
        dockv::KeyEntryValue child;
        RETURN_NOT_OK(child.DecodeFromKey(&temp));
        if (temp.empty()) {
          current->SetChild(child, std::move(descendant));
          break;
        }
        current = current->GetOrAddChild(child).first;
      }
    }
  }

  return Status::OK();
}

// If there is a key equal to key_bytes_without_ht + some timestamp, which is later than
// max_overwrite_time, we update max_overwrite_time, and result_value (unless it is nullptr).
// If there is a TTL with write time later than the write time in expiration, it is updated with
// the new write time and TTL, unless its value is kMaxTTL.
// When the TTL found is kMaxTTL and it is not a merge record, then it is assumed not to be
// explicitly set. Because it does not override the default table ttl, exp, which was initialized
// to the table ttl, is not updated.
// Observe that exp updates based on the first record found, while max_overwrite_time updates
// based on the first non-merge record found.
// This should not be used for leaf nodes. - Why? Looks like it is already used for leaf nodes
// also.
// Note: it is responsibility of caller to make sure key_bytes_without_ht doesn't have hybrid
// time.
// TODO: We could also check that the value is kTombStone or kObject type for sanity checking - ?
// It could be a simple value as well, not necessarily kTombstone or kObject.
Status FindLastWriteTime(
    IntentAwareIterator* iter,
    const Slice& key_without_ht,
    DocHybridTime* max_overwrite_time,
    dockv::Expiration* exp,
    dockv::Value* result_value = nullptr) {
  Slice value;
  EncodedDocHybridTime pre_doc_ht(*max_overwrite_time);
  RETURN_NOT_OK(iter->FindLatestRecord(key_without_ht, &pre_doc_ht, &value));
  if (!VERIFY_RESULT_REF(iter->Fetch())) {
    return Status::OK();
  }

  auto value_copy = value;
  auto control_fields = VERIFY_RESULT(ValueControlFields::Decode(&value_copy));
  auto value_type = dockv::DecodeValueEntryType(value_copy);
  if (value_type == ValueEntryType::kInvalid) {
    return Status::OK();
  }

  auto doc_ht = VERIFY_RESULT(pre_doc_ht.Decode());

  // We update the expiration if and only if the write time is later than the write time
  // currently stored in expiration, and the record is not a regular record with default TTL.
  // This is done independently of whether the row is a TTL row.
  // In the case that the always_override flag is true, default TTL will not be preserved.
  dockv::Expiration new_exp = *exp;
  if (doc_ht.hybrid_time() >= exp->write_ht) {
    // We want to keep the default TTL otherwise.
    if (control_fields.ttl != ValueControlFields::kMaxTtl ||
        control_fields.merge_flags == ValueControlFields::kTtlFlag ||
        exp->always_override) {
      new_exp.write_ht = doc_ht.hybrid_time();
      new_exp.ttl = control_fields.ttl;
    } else if (exp->ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    }
  }

  // If we encounter a TTL row, we assign max_overwrite_time to be the write time of the
  // original value/init marker.
  if (control_fields.merge_flags == ValueControlFields::kTtlFlag) {
    auto key_data = VERIFY_RESULT(iter->NextFullValue());
    value = key_data.value;

    // There could be a case where the TTL row exists, but the value has been
    // compacted away. Then, it is treated as a Tombstone written at the time
    // of the TTL row.
    if (!key_data && !new_exp.ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    } else {
      RETURN_NOT_OK(dockv::Value::DecodePrimitiveValueType(value));
      // Because we still do not know whether we are seeking something expired,
      // we must take the max_overwrite_time as if the value were not expired.
      doc_ht = VERIFY_RESULT(key_data.write_time.Decode());
    }
  }

  if ((value_type == ValueEntryType::kTombstone || value_type == ValueEntryType::kInvalid) &&
      !new_exp.ttl.IsNegative()) {
    new_exp.ttl = -new_exp.ttl;
  }
  *exp = new_exp;

  if (doc_ht > *max_overwrite_time) {
    *max_overwrite_time = doc_ht;
    VLOG(4) << "Max overwritten time for " << key_without_ht.ToDebugHexString() << ": "
            << *max_overwrite_time;
  }

  if (result_value) {
    RETURN_NOT_OK(result_value->Decode(value));
  }

  return Status::OK();
}

}  // namespace

Status GetRedisSubDocument(
    const DocDB& doc_db,
    const GetRedisSubDocumentData& data,
    const rocksdb::QueryId query_id,
    const TransactionOperationContext& txn_op_context,
    const ReadOperationData& read_operation_data) {
  auto iter = CreateIntentAwareIterator(
      doc_db, BloomFilterOptions::Fixed(data.subdocument_key), query_id, txn_op_context,
      read_operation_data);
  return GetRedisSubDocument(iter.get(), data, nullptr /* projection */, SeekFwdSuffices::kFalse);
}

Status GetRedisSubDocument(
    IntentAwareIterator *db_iter,
    const GetRedisSubDocumentData& data,
    const dockv::KeyEntryValues* projection,
    const SeekFwdSuffices seek_fwd_suffices) {
  // TODO(dtxn) scan through all involved transactions first to cache statuses in a batch,
  // so during building subdocument we don't need to request them one by one.
  // TODO(dtxn) we need to restart read with scan_ht = commit_ht if some transaction was committed
  // at time commit_ht within [scan_ht; read_request_time + max_clock_skew). Also we need
  // to wait until time scan_ht = commit_ht passed.
  // TODO(dtxn) for each scanned key (and its subkeys) we need to avoid *new* values committed at
  // ht <= scan_ht (or just ht < scan_ht?)
  // Question: what will break if we allow later commit at ht <= scan_ht ? Need to write down
  // detailed example.
  *data.doc_found = false;
  DOCDB_DEBUG_LOG("GetRedisSubDocument for key $0 @ $1", data.subdocument_key.ToDebugHexString(),
                  db_iter->read_time().ToString());

  // The latest time at which any prefix of the given key was overwritten.
  DocHybridTime max_overwrite_ht(DocHybridTime::kMin);
  VLOG(4) << "GetRedisSubDocument(" << data << ")";

  SubDocKey found_subdoc_key;
  auto dockey_size = VERIFY_RESULT(dockv::DocKey::EncodedSize(
      data.subdocument_key, dockv::DocKeyPart::kWholeDocKey));

  Slice key_slice = data.subdocument_key.Prefix(dockey_size);

  char highest = dockv::KeyEntryTypeAsChar::kHighest;
  KeyBuffer upperbound_buffer(key_slice, Slice(&highest, 1));
  // First, check the descendants of the ID level for TTL or more recent writes.
  IntentAwareIteratorUpperboundScope upperbound_scope(upperbound_buffer.AsSlice(), db_iter);
  if (seek_fwd_suffices) {
    db_iter->SeekForward(key_slice);
  } else {
    db_iter->Seek(key_slice, SeekFilter::kAll);
  }
  {
    auto temp_key = data.subdocument_key;
    temp_key.remove_prefix(dockey_size);
    for (;;) {
      auto decode_result = VERIFY_RESULT(SubDocKey::DecodeSubkey(&temp_key));
      if (!decode_result) {
        break;
      }
      RETURN_NOT_OK(FindLastWriteTime(db_iter, key_slice, &max_overwrite_ht, &data.exp));
      key_slice = Slice(key_slice.data(), temp_key.data());
    }
  }

  // By this point, key_slice is the DocKey and all the subkeys of subdocument_key. Check for
  // init-marker / tombstones at the top level; update max_overwrite_ht.
  dockv::Value doc_value{dockv::PrimitiveValue(ValueEntryType::kInvalid)};
  RETURN_NOT_OK(FindLastWriteTime(db_iter, key_slice, &max_overwrite_ht, &data.exp, &doc_value));

  const auto value_type = doc_value.value_type();

  if (data.return_type_only) {
    *data.doc_found = value_type != ValueEntryType::kInvalid &&
      !data.exp.ttl.IsNegative();
    // Check for expiration.
    if (*data.doc_found && max_overwrite_ht != DocHybridTime::kMin) {
      *data.doc_found =
          !dockv::HasExpiredTTL(data.exp.write_ht, data.exp.ttl, db_iter->read_time().read);
    }
    if (*data.doc_found) {
      // Observe that this will have the right type but not necessarily the right value.
      *data.result = SubDocument(doc_value.primitive_value());
    }
    return Status::OK();
  }

  if (projection == nullptr) {
    *data.result = SubDocument(ValueEntryType::kInvalid);
    int64 num_values_observed = 0;
    upperbound_buffer.PopBack();
    upperbound_buffer.Append(key_slice.WithoutPrefix(upperbound_buffer.size()));
    upperbound_buffer.PushBack(dockv::KeyEntryTypeAsChar::kHighest);
    IntentAwareIteratorUpperboundScope upperbound_scope2(upperbound_buffer.AsSlice(), db_iter);
    db_iter->Revalidate(SeekFilter::kAll);
    RETURN_NOT_OK(BuildSubDocument(db_iter, data, max_overwrite_ht,
                                   &num_values_observed));
    *data.doc_found = data.result->value_type() != ValueEntryType::kInvalid;
    if (*data.doc_found) {
      if (value_type == ValueEntryType::kRedisSet) {
        RETURN_NOT_OK(data.result->ConvertToRedisSet());
      } else if (value_type == ValueEntryType::kRedisTS) {
        RETURN_NOT_OK(data.result->ConvertToRedisTS());
      } else if (value_type == ValueEntryType::kRedisSortedSet) {
        RETURN_NOT_OK(data.result->ConvertToRedisSortedSet());
      } else if (value_type == ValueEntryType::kRedisList) {
        RETURN_NOT_OK(data.result->ConvertToRedisList());
      }
    }
    return Status::OK();
  }
  // Seed key_bytes with the subdocument key. For each subkey in the projection, build subdocument
  // and reuse key_bytes while appending the subkey.
  *data.result = SubDocument();
  dockv::KeyBytes key_bytes;
  // Preallocate some extra space to avoid allocation for small subkeys.
  key_bytes.Reserve(data.subdocument_key.size() + kMaxBytesPerEncodedHybridTime + 32);
  key_bytes.AppendRawBytes(data.subdocument_key);
  const size_t subdocument_key_size = key_bytes.size();
  for (const auto& subkey : *projection) {
    // Append subkey to subdocument key. Reserve extra kMaxBytesPerEncodedHybridTime + 1 bytes in
    // key_bytes to avoid the internal buffer from getting reallocated and moved by SeekForward()
    // appending the hybrid time, thereby invalidating the buffer pointer saved by prefix_scope.
    subkey.AppendToKey(&key_bytes);
    key_bytes.AppendKeyEntryType(dockv::KeyEntryType::kHighest);
    // This seek is to initialize the iterator for BuildSubDocument call.
    IntentAwareIteratorUpperboundScope subkey_upperbound_scope(key_bytes, db_iter);
    auto key = key_bytes.AsSlice().WithoutSuffix(1);
    db_iter->SeekForward(key);
    SubDocument descendant(ValueEntryType::kInvalid);
    int64 num_values_observed = 0;
    RETURN_NOT_OK(BuildSubDocument(
        db_iter, data.Adjusted(key, &descendant), max_overwrite_ht,
        &num_values_observed));
    *data.doc_found = descendant.value_type() != ValueEntryType::kInvalid;
    data.result->SetChild(subkey, std::move(descendant));

    // Restore subdocument key by truncating the appended subkey.
    key_bytes.Truncate(subdocument_key_size);
  }
  // Make sure the iterator is placed outside the whole document in the end.
  key_bytes.Truncate(dockey_size);
  db_iter->SeekOutOfSubDoc(SeekFilter::kAll, &key_bytes);
  return Status::OK();
}

std::string GetRedisSubDocumentData::ToString() const {
  return Format("{ subdocument_key: $0 exp.ttl: $1 exp.write_time: $2 return_type_only: $3 "
                    "low_subkey: $4 high_subkey: $5 }",
                SubDocKey::DebugSliceToString(subdocument_key), exp.ttl,
                exp.write_ht, return_type_only, low_subkey, high_subkey);
}


}  // namespace docdb
}  // namespace yb

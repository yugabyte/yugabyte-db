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

#include "yb/docdb/doc_reader.h"

#include <string>
#include <vector>

#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/doc_ttl_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/subdoc_reader.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"
#include "yb/docdb/deadline_info.h"
#include "yb/docdb/docdb_types.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/status.h"

using std::vector;

using yb::HybridTime;

namespace yb {
namespace docdb {

// ------------------------------------------------------------------------------------------------
// Standalone functions
// ------------------------------------------------------------------------------------------------

namespace {

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
    Expiration* exp,
    Value* result_value = nullptr) {
  Slice value;
  DocHybridTime doc_ht = *max_overwrite_time;
  RETURN_NOT_OK(iter->FindLatestRecord(key_without_ht, &doc_ht, &value));
  if (!iter->valid()) {
    return Status::OK();
  }

  uint64_t merge_flags = 0;
  MonoDelta ttl;
  ValueType value_type;
  RETURN_NOT_OK(Value::DecodePrimitiveValueType(value, &value_type, &merge_flags, &ttl));
  if (value_type == ValueType::kInvalid) {
    return Status::OK();
  }

  // We update the expiration if and only if the write time is later than the write time
  // currently stored in expiration, and the record is not a regular record with default TTL.
  // This is done independently of whether the row is a TTL row.
  // In the case that the always_override flag is true, default TTL will not be preserved.
  Expiration new_exp = *exp;
  if (doc_ht.hybrid_time() >= exp->write_ht) {
    // We want to keep the default TTL otherwise.
    if (ttl != Value::kMaxTtl || merge_flags == Value::kTtlFlag || exp->always_override) {
      new_exp.write_ht = doc_ht.hybrid_time();
      new_exp.ttl = ttl;
    } else if (exp->ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    }
  }

  // If we encounter a TTL row, we assign max_overwrite_time to be the write time of the
  // original value/init marker.
  if (merge_flags == Value::kTtlFlag) {
    DocHybridTime new_ht;
    RETURN_NOT_OK(iter->NextFullValue(&new_ht, &value));

    // There could be a case where the TTL row exists, but the value has been
    // compacted away. Then, it is treated as a Tombstone written at the time
    // of the TTL row.
    if (!iter->valid() && !new_exp.ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    } else {
      ValueType value_type;
      RETURN_NOT_OK(Value::DecodePrimitiveValueType(value, &value_type));
      // Because we still do not know whether we are seeking something expired,
      // we must take the max_overwrite_time as if the value were not expired.
      doc_ht = new_ht;
    }
  }

  if ((value_type == ValueType::kTombstone || value_type == ValueType::kInvalid) &&
      !new_exp.ttl.IsNegative()) {
    new_exp.ttl = -new_exp.ttl;
  }
  *exp = new_exp;

  if (doc_ht > *max_overwrite_time) {
    *max_overwrite_time = doc_ht;
    VLOG(4) << "Max overwritten time for " << key_without_ht.ToDebugHexString() << ": "
            << *max_overwrite_time;
  }

  if (result_value)
    RETURN_NOT_OK(result_value->Decode(value));

  return Status::OK();
}

}  // namespace

yb::Status GetSubDocument(
    const DocDB& doc_db,
    const GetSubDocumentData& data,
    const rocksdb::QueryId query_id,
    const TransactionOperationContextOpt& txn_op_context,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time) {
  auto iter = CreateIntentAwareIterator(
      doc_db, BloomFilterMode::USE_BLOOM_FILTER, data.subdocument_key, query_id,
      txn_op_context, deadline, read_time);
  return GetSubDocument(iter.get(), data, nullptr /* projection */, SeekFwdSuffices::kFalse);
}

yb::Status GetSubDocument(
    IntentAwareIterator *db_iter,
    const GetSubDocumentData& data,
    const vector<PrimitiveValue>* projection,
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
  DOCDB_DEBUG_LOG("GetSubDocument for key $0 @ $1", data.subdocument_key.ToDebugHexString(),
                  db_iter->read_time().ToString());

  // The latest time at which any prefix of the given key was overwritten.
  DocHybridTime max_overwrite_ht(DocHybridTime::kMin);
  VLOG(4) << "GetSubDocument(" << data << ")";

  SubDocKey found_subdoc_key;
  auto dockey_size =
      VERIFY_RESULT(DocKey::EncodedSize(data.subdocument_key, DocKeyPart::kWholeDocKey));

  Slice key_slice(data.subdocument_key.data(), dockey_size);

  // Check ancestors for init markers, tombstones, and expiration, tracking the expiration and
  // corresponding most recent write time in exp, and the general most recent overwrite time in
  // max_overwrite_ht.
  //
  // First, check for an ancestor at the ID level: a table tombstone.  Currently, this is only
  // supported for YSQL colocated tables.  Since iterators only ever pertain to one table, there is
  // no need to create a prefix scope here.
  if (data.table_tombstone_time && *data.table_tombstone_time == DocHybridTime::kInvalid) {
    // Only check for table tombstones if the table is colocated, as signified by the prefix of
    // kPgTableOid.
    // TODO: adjust when fixing issue #3551
    if (key_slice[0] == ValueTypeAsChar::kPgTableOid) {
      // Seek to the ID level to look for a table tombstone.  Since this seek is expensive, cache
      // the result in data.table_tombstone_time to avoid double seeking for the lifetime of the
      // DocRowwiseIterator.
      DocKey empty_key;
      RETURN_NOT_OK(empty_key.DecodeFrom(key_slice, DocKeyPart::kUpToId));
      db_iter->Seek(empty_key);
      Value doc_value = Value(PrimitiveValue(ValueType::kInvalid));
      RETURN_NOT_OK(FindLastWriteTime(
          db_iter,
          empty_key.Encode(),
          &max_overwrite_ht,
          &data.exp,
          &doc_value));
      if (doc_value.value_type() == ValueType::kTombstone) {
        SCHECK_NE(max_overwrite_ht, DocHybridTime::kInvalid, Corruption,
                  "Invalid hybrid time for table tombstone");
        *data.table_tombstone_time = max_overwrite_ht;
      } else {
        *data.table_tombstone_time = DocHybridTime::kMin;
      }
    } else {
      *data.table_tombstone_time = DocHybridTime::kMin;
    }
  } else if (data.table_tombstone_time) {
    // Use the cached result.  Don't worry about exp as YSQL does not support TTL, yet.
    max_overwrite_ht = *data.table_tombstone_time;
  }
  // Second, check the descendants of the ID level.
  IntentAwareIteratorPrefixScope prefix_scope(key_slice, db_iter);
  if (seek_fwd_suffices) {
    db_iter->SeekForward(key_slice);
  } else {
    db_iter->Seek(key_slice);
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
      key_slice = Slice(key_slice.data(), temp_key.data() - key_slice.data());
    }
  }

  // By this point, key_slice is the DocKey and all the subkeys of subdocument_key. Check for
  // init-marker / tombstones at the top level; update max_overwrite_ht.
  Value doc_value = Value(PrimitiveValue(ValueType::kInvalid));
  RETURN_NOT_OK(FindLastWriteTime(db_iter, key_slice, &max_overwrite_ht, &data.exp, &doc_value));

  *data.result = SubDocument();

  if (projection == nullptr) {
    KeyBytes subdoc_key_copy(data.subdocument_key);
    SubDocumentReader reader(
      subdoc_key_copy, db_iter, data.deadline_info, max_overwrite_ht, data.exp);
    RETURN_NOT_OK(reader.Get(data.result));
    *data.doc_found = data.result->value_type() != ValueType::kInvalid
                   && data.result->value_type() != ValueType::kTombstone;
    return Status::OK();
  }
  // Seed key_bytes with the subdocument key. For each subkey in the projection, build subdocument
  // and reuse key_bytes while appending the subkey.
  KeyBytes key_bytes;
  // Preallocate some extra space to avoid allocation for small subkeys.
  key_bytes.Reserve(data.subdocument_key.size() + kMaxBytesPerEncodedHybridTime + 32);
  key_bytes.AppendRawBytes(data.subdocument_key);
  const size_t subdocument_key_size = key_bytes.size();
  *data.doc_found = false;
  for (const PrimitiveValue& subkey : *projection) {
    // Append subkey to subdocument key. Reserve extra kMaxBytesPerEncodedHybridTime + 1 bytes in
    // key_bytes to avoid the internal buffer from getting reallocated and moved by SeekForward()
    // appending the hybrid time, thereby invalidating the buffer pointer saved by prefix_scope.
    subkey.AppendToKey(&key_bytes);
    key_bytes.Reserve(key_bytes.size() + kMaxBytesPerEncodedHybridTime + 1);
    // This seek is to initialize the iterator for BuildSubDocument call.
    IntentAwareIteratorPrefixScope prefix_scope(key_bytes, db_iter);
    db_iter->SeekForward(&key_bytes);
    SubDocument descendant;
    SubDocumentReader reader(
        key_bytes, db_iter, data.deadline_info, max_overwrite_ht, data.exp);
    RETURN_NOT_OK(reader.Get(&descendant));
    *data.doc_found = *data.doc_found || (
        descendant.value_type() != ValueType::kInvalid
        && descendant.value_type() != ValueType::kTombstone);
    data.result->SetChild(subkey, std::move(descendant));

    // Restore subdocument key by truncating the appended subkey.
    key_bytes.Truncate(subdocument_key_size);
  }
  // Make sure the iterator is placed outside the whole document in the end.
  key_bytes.Truncate(dockey_size);
  db_iter->SeekOutOfSubDoc(&key_bytes);
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb

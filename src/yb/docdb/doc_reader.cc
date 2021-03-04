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

#include "yb/common/doc_hybrid_time.h"
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
  VLOG(4) << "GetSubDocument(" << data << ")";
  DocDBTableReader doc_reader(db_iter, data.deadline_info, seek_fwd_suffices);
  RETURN_NOT_OK(doc_reader.UpdateTableTombstoneTime(
      data.subdocument_key, data.table_tombstone_time));
  doc_reader.SetTableTtl(data.exp);

  *data.result = SubDocument();

  if (projection == nullptr) {
    // TODO - investigate whether we can avoid this copy. Would require changing SubDocumentReader
    // to take a Slice in the constructor instead of KeyBytes. Avoiding this copy may not be useful
    // at all, though, if we end up pulling this logic up further.
    const KeyBytes full_subdoc_key_copy(data.subdocument_key);
    *data.doc_found = VERIFY_RESULT(doc_reader.Get(full_subdoc_key_copy, data.result));
  } else {
    *data.doc_found = VERIFY_RESULT(doc_reader.Get(data.subdocument_key, projection, data.result));
  }

  return Status::OK();
}

DocDBTableReader::DocDBTableReader(
    IntentAwareIterator* iter,
    DeadlineInfo* deadline_info,
    SeekFwdSuffices seek_fwd_suffices)
    : iter_(iter),
      deadline_info_(deadline_info),
      seek_fwd_suffices_(seek_fwd_suffices),
      subdoc_reader_builder_(iter_, deadline_info_) {}

void DocDBTableReader::SetTableTtl(Expiration table_ttl) {
  table_expiration_ = table_ttl;
}

Status DocDBTableReader::UpdateTableTombstoneTime(
    const Slice& root_doc_key, DocHybridTime* table_tombstone_time) {
  if (table_tombstone_time != nullptr && root_doc_key[0] == ValueTypeAsChar::kPgTableOid) {
    // Update table_tombstone_time based on what is written to RocksDB if its not already set.
    // Otherwise, just accept its value.
    // TODO -- this is a bit of a hack to allow DocRowwiseIterator to pass along the table tombstone
    // time read at a previous invocation of this same code. If instead the DocRowwiseIterator owned
    // an instance of SubDocumentReaderBuilder, and this method call was hoisted up to that level,
    // passing around this table_tombstone_time would no longer be necessary.
    if (*table_tombstone_time == DocHybridTime::kInvalid) {
      DocKey table_id;
      RETURN_NOT_OK(table_id.DecodeFrom(root_doc_key, DocKeyPart::kUpToId));
      iter_->Seek(table_id);

      Slice value;
      auto table_id_encoded = table_id.Encode();
      DocHybridTime doc_ht = DocHybridTime::kMin;

      RETURN_NOT_OK(iter_->FindLatestRecord(table_id_encoded, &doc_ht, &value));
      ValueType value_type;
      RETURN_NOT_OK(Value::DecodePrimitiveValueType(value, &value_type));
      if (value_type == ValueType::kTombstone) {
        SCHECK_NE(doc_ht, DocHybridTime::kInvalid, Corruption,
                  "Invalid hybrid time for table tombstone");
        table_tombstone_time_ = doc_ht;
      }
      *table_tombstone_time = table_tombstone_time_;
    } else {
      table_tombstone_time_ = *table_tombstone_time;
    }
  }
  return Status::OK();;
}

CHECKED_STATUS DocDBTableReader::InitForKey(const Slice& sub_doc_key) {
  auto dockey_size =
      VERIFY_RESULT(DocKey::EncodedSize(sub_doc_key, DocKeyPart::kWholeDocKey));
  const Slice root_doc_key(sub_doc_key.data(), dockey_size);
  SeekTo(root_doc_key);
  RETURN_NOT_OK(subdoc_reader_builder_.InitObsolescenceInfo(
      table_tombstone_time_, table_expiration_, root_doc_key, sub_doc_key));
  return Status::OK();
}

void DocDBTableReader::SeekTo(const Slice& subdoc_key) {
  if (seek_fwd_suffices_) {
    iter_->SeekForward(subdoc_key);
  } else {
    iter_->Seek(subdoc_key);
  }
}

Result<bool> DocDBTableReader::Get(const KeyBytes& sub_doc_key, SubDocument* result) {
  RETURN_NOT_OK(InitForKey(sub_doc_key));
  SeekTo(sub_doc_key);
  auto reader = VERIFY_RESULT(subdoc_reader_builder_.Build(sub_doc_key));

  RETURN_NOT_OK(reader->Get(result));
  return result->value_type() != ValueType::kInvalid &&
         result->value_type() != ValueType::kTombstone;
}

Result<bool> DocDBTableReader::Get(
    const Slice& sub_doc_key, const vector<PrimitiveValue>* projection, SubDocument* result) {
  RETURN_NOT_OK(InitForKey(sub_doc_key));

  // Seed key_bytes with the subdocument key. For each subkey in the projection, build subdocument
  // and reuse key_bytes while appending the subkey.
  KeyBytes key_bytes;
  // Preallocate some extra space to avoid allocation for small subkeys.
  key_bytes.Reserve(sub_doc_key.size() + kMaxBytesPerEncodedHybridTime + 32);
  key_bytes.AppendRawBytes(sub_doc_key);
  const size_t subdocument_key_size = key_bytes.size();
  bool doc_found = false;
  for (const PrimitiveValue& subkey : *projection) {
    // Append subkey to subdocument key. Reserve extra kMaxBytesPerEncodedHybridTime + 1 bytes in
    // key_bytes to avoid the internal buffer from getting reallocated and moved by SeekForward()
    // appending the hybrid time, thereby invalidating the buffer pointer saved by prefix_scope.
    subkey.AppendToKey(&key_bytes);
    key_bytes.Reserve(key_bytes.size() + kMaxBytesPerEncodedHybridTime + 1);
    // This seek is to initialize the iterator for BuildSubDocument call.
    iter_->SeekForward(&key_bytes);
    SubDocument descendant;
    auto reader = VERIFY_RESULT(subdoc_reader_builder_.Build(key_bytes));
    RETURN_NOT_OK(reader->Get(&descendant));
    doc_found = doc_found || (
        descendant.value_type() != ValueType::kInvalid
        && descendant.value_type() != ValueType::kTombstone);
    result->SetChild(subkey, std::move(descendant));

    // Restore subdocument key by truncating the appended subkey.
    key_bytes.Truncate(subdocument_key_size);
  }
  iter_->SeekOutOfSubDoc(sub_doc_key);
  return doc_found;
}

}  // namespace docdb
}  // namespace yb

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
#include "yb/docdb/docdb_fwd.h"
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


  // TODO(dtxn) scan through all involved transactions first to cache statuses in a batch,
  // so during building subdocument we don't need to request them one by one.
  // TODO(dtxn) we need to restart read with scan_ht = commit_ht if some transaction was committed
  // at time commit_ht within [scan_ht; read_request_time + max_clock_skew). Also we need
  // to wait until time scan_ht = commit_ht passed.
  // TODO(dtxn) for each scanned key (and its subkeys) we need to avoid *new* values committed at
  // ht <= scan_ht (or just ht < scan_ht?)
  // Question: what will break if we allow later commit at ht <= scan_ht ? Need to write down
  // detailed example.

Result<boost::optional<SubDocument>> TEST_GetSubDocument(
    const Slice& sub_doc_key,
    const DocDB& doc_db,
    const rocksdb::QueryId query_id,
    const TransactionOperationContextOpt& txn_op_context,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    const std::vector<PrimitiveValue>* projection) {
  auto iter = CreateIntentAwareIterator(
      doc_db, BloomFilterMode::USE_BLOOM_FILTER, sub_doc_key, query_id,
      txn_op_context, deadline, read_time);
  DeadlineInfo deadline_info(deadline);
  DOCDB_DEBUG_LOG("GetSubDocument for key $0 @ $1", sub_doc_key.ToDebugHexString(),
                  iter->read_time().ToString());
  DocDBTableReader doc_reader(iter.get(), &deadline_info, SeekFwdSuffices::kFalse);
  RETURN_NOT_OK(doc_reader.UpdateTableTombstoneTime(sub_doc_key));

  SubDocument result;
  if (VERIFY_RESULT(doc_reader.Get(sub_doc_key, projection, &result))) {
    return result;
  }
  return boost::none;
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

Status DocDBTableReader::UpdateTableTombstoneTime(const Slice& root_doc_key) {
  if (root_doc_key[0] == ValueTypeAsChar::kPgTableOid) {
    // Update table_tombstone_time based on what is written to RocksDB if its not already set.
    // Otherwise, just accept its value.
    // TODO -- this is a bit of a hack to allow DocRowwiseIterator to pass along the table tombstone
    // time read at a previous invocation of this same code. If instead the DocRowwiseIterator owned
    // an instance of SubDocumentReaderBuilder, and this method call was hoisted up to that level,
    // passing around this table_tombstone_time would no longer be necessary.
    if (!table_tombstone_time_.is_valid()) {
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
    }
  }
  if (!table_tombstone_time_.is_valid()) {
    table_tombstone_time_ = DocHybridTime::kMin;
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

Result<bool> DocDBTableReader::Get(
    const Slice& root_doc_key, const vector<PrimitiveValue>* projection, SubDocument* result) {
  RETURN_NOT_OK(InitForKey(root_doc_key));
  // Seed key_bytes with the subdocument key. For each subkey in the projection, build subdocument
  // and reuse key_bytes while appending the subkey.
  KeyBytes key_bytes;
  // Preallocate some extra space to avoid allocation for small subkeys.
  key_bytes.Reserve(root_doc_key.size() + kMaxBytesPerEncodedHybridTime + 32);
  key_bytes.AppendRawBytes(root_doc_key);
  if (projection != nullptr) {
    bool doc_found = false;
    const size_t subdocument_key_size = key_bytes.size();
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
    if (doc_found) {
      iter_->SeekOutOfSubDoc(root_doc_key);
      return true;
    }
  }

  // If doc is not found, decide if some non-projection column exists.
  // Currently we read the whole doc here,
  // may be optimized by exiting on the first column in future.
  // TODO -- is resetting *result = SubDocument() needed here?
  // TODO -- Add some metrics to understand:
  // (a) how often we scan back
  // (b) how often it's useful
  // Also maybe in debug mode add some every-n logging of the rocksdb values for which it is
  // useful
  iter_->Seek(key_bytes);
  auto reader = VERIFY_RESULT(subdoc_reader_builder_.Build(key_bytes));
  RETURN_NOT_OK(reader->Get(result));
  return result->value_type() != ValueType::kInvalid
      && result->value_type() != ValueType::kTombstone;
}

}  // namespace docdb
}  // namespace yb

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

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/common/partition.h"
#include "yb/common/transaction.h"
#include "yb/common/ql_scanspec.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/subdocument.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksdb/db/compaction.h"
#include "yb/rocksutil/yb_rocksdb.h"

using std::string;

using yb::FormatRocksDBSliceAsStr;

namespace yb {
namespace docdb {

DocRowwiseIterator::DocRowwiseIterator(
    const Schema &projection,
    const Schema &schema,
    const TransactionOperationContextOpt& txn_op_context,
    rocksdb::DB *db,
    const ReadHybridTime& read_time,
    yb::util::PendingOperationCounter* pending_op_counter)
    : projection_(projection),
      schema_(schema),
      txn_op_context_(txn_op_context),
      read_time_(read_time),
      db_(db),
      has_bound_key_(false),
      pending_op_(pending_op_counter),
      done_(false) {
  projection_subkeys_.reserve(projection.num_columns() + 1);
  projection_subkeys_.push_back(PrimitiveValue::SystemColumnId(SystemColumnIds::kLivenessColumn));
  for (size_t i = projection_.num_key_columns(); i < projection.num_columns(); i++) {
    projection_subkeys_.emplace_back(projection.column_id(i));
  }
  std::sort(projection_subkeys_.begin(), projection_subkeys_.end());
}

DocRowwiseIterator::~DocRowwiseIterator() {
}

Status DocRowwiseIterator::Init() {
  auto query_id = rocksdb::kDefaultQueryId;

  db_iter_ = CreateIntentAwareIterator(
      db_, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none /* user_key_for_filter */,
      query_id, txn_op_context_, read_time_);

  row_key_ = DocKey();
  db_iter_->Seek(row_key_);
  row_ready_ = false;
  has_bound_key_ = false;

  return Status::OK();
}

Status DocRowwiseIterator::Init(const common::QLScanSpec& spec) {
  const DocQLScanSpec& doc_spec = dynamic_cast<const DocQLScanSpec&>(spec);
  is_forward_scan_ = doc_spec.is_forward_scan();

  VLOG(4) << "Initializing iterator direction: " << (is_forward_scan_ ? "FORWARD" : "BACKWARD");

  DocKey lower_doc_key;
  DocKey upper_doc_key;
  RETURN_NOT_OK(doc_spec.lower_bound(&lower_doc_key));
  RETURN_NOT_OK(doc_spec.upper_bound(&upper_doc_key));
  VLOG(4) << "DocKey Bounds " << lower_doc_key.ToString() << ", " << upper_doc_key.ToString();

  // TOOD(bogdan): decide if this is a good enough heuristic for using blooms for scans.
  const bool is_fixed_point_get = !lower_doc_key.empty() &&
      upper_doc_key.HashedComponentsEqual(lower_doc_key);
  const auto mode = is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER :
      BloomFilterMode::DONT_USE_BLOOM_FILTER;

  const KeyBytes row_key_encoded = lower_doc_key.Encode();
  const Slice row_key_encoded_as_slice = row_key_encoded.AsSlice();

  db_iter_ = CreateIntentAwareIterator(
      db_, mode, row_key_encoded_as_slice, doc_spec.QueryId(), txn_op_context_, read_time_,
      doc_spec.CreateFileFilter());

  db_iter_->SeekWithoutHt(row_key_encoded);
  row_ready_ = false;

  if (is_forward_scan_) {
    has_bound_key_ = !upper_doc_key.empty();
    if (has_bound_key_) {
      bound_key_ = upper_doc_key;
    }
  } else {
    has_bound_key_ = !lower_doc_key.empty();
    if (has_bound_key_) {
      bound_key_ = lower_doc_key;
    }
  }

  if (is_forward_scan_) {
    if (has_bound_key_) {
       db_iter_->Seek(lower_doc_key);
    }
  } else {
    if (has_bound_key_) {
      db_iter_->PrevDocKey(upper_doc_key);
    } else {
      db_iter_->SeekToLastDocKey();
    }
  }

  return Status::OK();
}

Status DocRowwiseIterator::EnsureIteratorPositionCorrect() const {
  if (!is_forward_scan_) {
    db_iter_->PrevDocKey(row_key_);
  }
  return Status::OK();
}


bool DocRowwiseIterator::HasNext() const {
  if (!status_.ok() || row_ready_) {
    // If row is ready, then HasNext returns true. In case of error, NextRow() will
    // eventually report the error. HasNext is unable to return an error status.
    return true;
  }

  if (done_) return false;

  bool doc_found = false;
  while (!doc_found) {
    if (!db_iter_->valid()) {
      done_ = true;
      return false;
    }
    auto fetched_key = db_iter_->FetchKey();
    if (!fetched_key.ok()) {
      status_ = fetched_key.status();
      return true;
    }
    {
      Slice key_copy = *fetched_key;
      status_ = row_key_.DecodeFrom(&key_copy);
    }
    if (!status_.ok()) {
      // Defer error reporting to NextRow().
      return true;
    }

    if (has_bound_key_ && is_forward_scan_ == (row_key_ >= bound_key_)) {
      done_ = true;
      return false;
    }

    KeyBytes old_key(*fetched_key);
    // The iterator is positioned by the previous GetSubDocument call
    // (which places the iterator outside the previous doc_key).
    SubDocKey sub_doc_key(row_key_);
    GetSubDocumentData data = { &sub_doc_key, &row_, &doc_found };
    data.table_ttl = TableTTL(schema_);
    status_ = GetSubDocument(db_iter_.get(), data, &projection_subkeys_);
    // After this, the iter should be positioned right after the subdocument.
    if (!status_.ok()) {
      // Defer error reporting to NextRow().
      return true;
    }

    if (!doc_found) {
      SubDocument full_row;
      // If doc is not found, decide if some non-projection column exists.
      // Currently we read the whole doc here,
      // may be optimized by exiting on the first column in future.
      db_iter_->Seek(row_key_);  // Position it for GetSubDocument.
      data.result = &full_row;
      status_ = GetSubDocument(db_iter_.get(), data);
      if (!status_.ok()) {
        // Defer error reporting to NextRow().
        return true;
      }
    }
    // GetSubDocument must ensure that iterator is pushed forward, to avoid loops.
    if (db_iter_->valid()) {
      auto iter_key = db_iter_->FetchKey();
      if (!iter_key.ok()) {
        status_ = iter_key.status();
        return true;
      }
      if (old_key.AsSlice().compare(*iter_key) >= 0) {
        status_ = STATUS_SUBSTITUTE(Corruption, "Infinite loop detected at $0",
            FormatRocksDBSliceAsStr(old_key.AsSlice()));
        VLOG(1) << status_;
        return true;
      }
    }
    status_ = EnsureIteratorPositionCorrect();
    if (!status_.ok()) {
      // Defer error reporting to NextRow().
      return true;
    }
  }
  row_ready_ = true;
  return true;
}

string DocRowwiseIterator::ToString() const {
  return "DocRowwiseIterator";
}

namespace {

// Set primary key column values (hashed or range columns) in a QL row value map.
CHECKED_STATUS SetQLPrimaryKeyColumnValues(const Schema& schema,
                                           const size_t begin_index,
                                           const size_t column_count,
                                           const char* column_type,
                                           const vector<PrimitiveValue>& values,
                                           QLTableRow* table_row) {
  if (values.size() != column_count) {
    return STATUS_SUBSTITUTE(Corruption, "$0 $1 primary key columns found but $2 expected",
                             values.size(), column_type, column_count);
  }
  if (begin_index + column_count > schema.num_columns()) {
    return STATUS_SUBSTITUTE(
        Corruption,
        "$0 primary key columns between positions $1 and $2 go beyond table columns $3",
        column_type, begin_index, begin_index + column_count - 1, schema.num_columns());
  }
  for (size_t i = 0, j = begin_index; i < column_count; i++, j++) {
    const auto ql_type = schema.column(j).type();
    QLTableColumn& column = table_row->AllocColumn(schema.column_id(j));
    PrimitiveValue::ToQLValuePB(values[i], ql_type, &column.value);
  }
  return Status::OK();
}

} // namespace

void DocRowwiseIterator::SkipRow() {
  row_ready_ = false;
}

HybridTime DocRowwiseIterator::RestartReadHt() {
  auto max_seen_ht = db_iter_->max_seen_ht();
  if (max_seen_ht.is_valid() && max_seen_ht > db_iter_->read_time().read) {
    VLOG(4) << "Restart read: " << max_seen_ht << ", original: " << db_iter_->read_time();
    return max_seen_ht;
  }
  return HybridTime::kInvalid;
}

bool DocRowwiseIterator::IsNextStaticColumn() const {
  return schema_.has_statics() && row_key_.range_group().empty();
}

Status DocRowwiseIterator::DoNextRow(const Schema& projection, QLTableRow* table_row) {
  if (!status_.ok()) {
    // An error happened in HasNext.
    return status_;
  }

  if (PREDICT_FALSE(done_)) {
    return STATUS(NotFound, "end of iter");
  }

  // Ensure row is ready to be read. HasNext() must be called before reading the first row, or
  // again after the previous row has been read or skipped.
  if (!row_ready_) {
    return STATUS(InternalError, "next row has not be prepared for reading");
  }

  // Populate the key column values from the doc key. The key column values in doc key were
  // written in the same order as in the table schema (see DocKeyFromQLKey). If the range columns
  // are present, read them also.
  RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
      schema_, 0, schema_.num_hash_key_columns(),
      "hash", row_key_.hashed_group(), table_row));
  if (!row_key_.range_group().empty()) {
    RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
        schema_, schema_.num_hash_key_columns(), schema_.num_range_key_columns(),
        "range", row_key_.range_group(), table_row));
  }

  for (size_t i = projection.num_key_columns(); i < projection.num_columns(); i++) {
    const auto& column_id = projection.column_id(i);
    const auto ql_type = projection.column(i).type();
    const SubDocument* column_value = row_.GetChild(PrimitiveValue(column_id));
    if (column_value != nullptr) {
      QLTableColumn& column = table_row->AllocColumn(column_id);
      SubDocument::ToQLValuePB(*column_value, ql_type, &column.value);
      column.ttl_seconds = column_value->GetTtl();
      column.write_time = column_value->GetWriteTime();
    }
  }
  row_ready_ = false;
  return Status::OK();
}

CHECKED_STATUS DocRowwiseIterator::GetNextReadSubDocKey(SubDocKey* sub_doc_key) const {
  if (db_iter_ == nullptr) {
    return STATUS(Corruption, "Iterator not initialized.");
  }

  // There are no more rows to fetch, so no next SubDocKey to read.
  if (!HasNext()) {
    DVLOG(3) << "No Next SubDocKey";
    return Status::OK();
  }
  *sub_doc_key = SubDocKey(row_key_, read_time_.read);
  DVLOG(3) << "Next SubDocKey: " << sub_doc_key->ToString();
  return Status::OK();
}

CHECKED_STATUS DocRowwiseIterator::SetPagingStateIfNecessary(const QLReadRequestPB& request,
                                                             QLResponsePB* response) const {
  // When the "limit" number of rows are returned and we are asked to return the paging state,
  // return the partition key and row key of the next row to read in the paging state if there are
  // still more rows to read. Otherwise, leave the paging state empty which means we are done
  // reading from this tablet.
  if (request.return_paging_state()) {
    SubDocKey next_key;
    RETURN_NOT_OK(GetNextReadSubDocKey(&next_key));
    if (!next_key.doc_key().empty()) {
      QLPagingStatePB* paging_state = response->mutable_paging_state();
      paging_state->set_next_partition_key(
          PartitionSchema::EncodeMultiColumnHashValue(next_key.doc_key().hash()));
      paging_state->set_next_row_key(next_key.Encode(true /* include_hybrid_time */).data());
    }
  }
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb

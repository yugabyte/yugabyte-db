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
#include <iterator>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "yb/common/common.pb.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/ql_value.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_reader.h"
#include "yb/docdb/doc_scanspec_util.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/expiration.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/scan_choices.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/rocksdb/db/compaction.h"
#include "yb/rocksutil/yb_rocksdb.h"

#include "yb/rocksdb/db.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/strongly_typed_bool.h"

using std::string;

namespace yb {
namespace docdb {

DocRowwiseIterator::DocRowwiseIterator(
    const Schema &projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    RWOperationCounter* pending_op_counter)
    : projection_(projection),
      doc_read_context_(doc_read_context),
      txn_op_context_(txn_op_context),
      deadline_(deadline),
      read_time_(read_time),
      doc_db_(doc_db),
      has_bound_key_(false),
      pending_op_(pending_op_counter),
      done_(false) {
  projection_subkeys_.reserve(projection.num_columns() + 1);
  projection_subkeys_.push_back(KeyEntryValue::kLivenessColumn);
  for (size_t i = projection_.num_key_columns(); i < projection.num_columns(); i++) {
    projection_subkeys_.push_back(KeyEntryValue::MakeColumnId(projection.column_id(i)));
  }
  std::sort(projection_subkeys_.begin(), projection_subkeys_.end());
}

DocRowwiseIterator::~DocRowwiseIterator() {
}

Status DocRowwiseIterator::Init(TableType table_type, const Slice& sub_doc_key) {
  db_iter_ = CreateIntentAwareIterator(
      doc_db_,
      BloomFilterMode::DONT_USE_BLOOM_FILTER,
      boost::none /* user_key_for_filter */,
      rocksdb::kDefaultQueryId,
      txn_op_context_,
      deadline_,
      read_time_);
  if (!sub_doc_key.empty()) {
    iter_key_.Reset(sub_doc_key);
  } else {
    DocKeyEncoder(&iter_key_).Schema(doc_read_context_.schema);
  }
  row_key_ = iter_key_;
  row_hash_key_ = row_key_;
  VLOG(3) << __PRETTY_FUNCTION__ << " Seeking to " << row_key_;
  db_iter_->Seek(row_key_);
  row_ready_ = false;
  has_bound_key_ = false;
  table_type_ = table_type;
  if (table_type == TableType::PGSQL_TABLE_TYPE) {
    ignore_ttl_ = true;
  }

  return Status::OK();
}

Result<bool> DocRowwiseIterator::InitScanChoices(
    const DocQLScanSpec& doc_spec, const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key) {
  scan_choices_ = ScanChoices::Create(
      doc_read_context_.schema, doc_spec, lower_doc_key, upper_doc_key);

  if (scan_choices_ && scan_choices_->IsInitialPositionKnown()) {
    // Let's not seek to the lower doc key or upper doc key. We know exactly what we want.
    RETURN_NOT_OK(AdvanceIteratorToNextDesiredRow());
    return true;
  }

  return false;
}

Result<bool> DocRowwiseIterator::InitScanChoices(
    const DocPgsqlScanSpec& doc_spec, const KeyBytes& lower_doc_key,
    const KeyBytes& upper_doc_key) {
  scan_choices_ = ScanChoices::Create(
      doc_read_context_.schema, doc_spec, lower_doc_key, upper_doc_key);

  if (scan_choices_ && scan_choices_->IsInitialPositionKnown()) {
    // Let's not seek to the lower doc key or upper doc key. We know exactly what we want.
    RETURN_NOT_OK(AdvanceIteratorToNextDesiredRow());
    return true;
  }

  return false;
}

template <class T>
Status DocRowwiseIterator::DoInit(const T& doc_spec) {
  is_forward_scan_ = doc_spec.is_forward_scan();

  VLOG(4) << "Initializing iterator direction: " << (is_forward_scan_ ? "FORWARD" : "BACKWARD");

  auto lower_doc_key = VERIFY_RESULT(doc_spec.LowerBound());
  auto upper_doc_key = VERIFY_RESULT(doc_spec.UpperBound());
  VLOG(4) << "DocKey Bounds " << DocKey::DebugSliceToString(lower_doc_key.AsSlice())
          << ", " << DocKey::DebugSliceToString(upper_doc_key.AsSlice());

  // TODO(bogdan): decide if this is a good enough heuristic for using blooms for scans.
  const bool is_fixed_point_get =
      !lower_doc_key.empty() &&
      VERIFY_RESULT(HashedOrFirstRangeComponentsEqual(lower_doc_key, upper_doc_key));
  const auto mode = is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER
                                       : BloomFilterMode::DONT_USE_BLOOM_FILTER;

  db_iter_ = CreateIntentAwareIterator(
      doc_db_, mode, lower_doc_key.AsSlice(), doc_spec.QueryId(), txn_op_context_,
      deadline_, read_time_, doc_spec.CreateFileFilter());

  row_ready_ = false;

  if (is_forward_scan_) {
    has_bound_key_ = !upper_doc_key.empty();
    if (has_bound_key_) {
      bound_key_ = std::move(upper_doc_key);
      db_iter_->SetUpperbound(bound_key_);
    }
  } else {
    has_bound_key_ = !lower_doc_key.empty();
    if (has_bound_key_) {
      bound_key_ = std::move(lower_doc_key);
    }
  }

  if (!VERIFY_RESULT(InitScanChoices(doc_spec,
        !is_forward_scan_ && has_bound_key_ ? bound_key_ : lower_doc_key,
        is_forward_scan_ && has_bound_key_ ? bound_key_ : upper_doc_key))) {
    if (is_forward_scan_) {
      VLOG(3) << __PRETTY_FUNCTION__ << " Seeking to " << DocKey::DebugSliceToString(lower_doc_key);
      db_iter_->Seek(lower_doc_key);
    } else {
      // TODO consider adding an operator bool to DocKey to use instead of empty() here.
      if (!upper_doc_key.empty()) {
        db_iter_->PrevDocKey(upper_doc_key);
      } else {
        db_iter_->SeekToLastDocKey();
      }
    }
  }

  return Status::OK();
}

Status DocRowwiseIterator::Init(const QLScanSpec& spec) {
  table_type_ = TableType::YQL_TABLE_TYPE;
  return DoInit(down_cast<const DocQLScanSpec&>(spec));
}

Status DocRowwiseIterator::Init(const PgsqlScanSpec& spec) {
  table_type_ = TableType::PGSQL_TABLE_TYPE;
  ignore_ttl_ = true;
  return DoInit(down_cast<const DocPgsqlScanSpec&>(spec));
}

Status DocRowwiseIterator::AdvanceIteratorToNextDesiredRow() const {
  if (scan_choices_) {
    if (!IsNextStaticColumn()
        && !scan_choices_->CurrentTargetMatchesKey(row_key_)) {
      return scan_choices_->SeekToCurrentTarget(db_iter_.get());
    }
  } else {
    if (!is_forward_scan_) {
      VLOG(4) << __PRETTY_FUNCTION__ << " setting as PrevDocKey";
      db_iter_->PrevDocKey(row_key_);
    }
  }

  return Status::OK();
}

Result<bool> DocRowwiseIterator::HasNext() {
  VLOG(4) << __PRETTY_FUNCTION__;

  // Repeated HasNext calls (without Skip/NextRow in between) should be idempotent:
  // 1. If a previous call failed we returned the same status.
  // 2. If a row is already available (row_ready_), return true directly.
  // 3. If we finished all target rows for the scan (done_), return false directly.
  RETURN_NOT_OK(has_next_status_);
  if (row_ready_) {
    // If row is ready, then HasNext returns true.
    return true;
  }
  if (done_) {
    return false;
  }

  bool doc_found = false;
  while (!doc_found) {
    if (!db_iter_->valid() || (scan_choices_ && scan_choices_->FinishedWithScanChoices())) {
      done_ = true;
      return false;
    }

    const auto key_data = db_iter_->FetchKey();
    if (!key_data.ok()) {
      VLOG(4) << __func__ << ", key data: " << key_data.status();
      has_next_status_ = key_data.status();
      return has_next_status_;
    }

    VLOG(4) << "*fetched_key is " << SubDocKey::DebugSliceToString(key_data->key);
    if (debug_dump_) {
      LOG(INFO) << __func__ << ", fetched key: " << SubDocKey::DebugSliceToString(key_data->key)
                << ", " << key_data->key.ToDebugHexString();
    }

    // The iterator is positioned by the previous GetSubDocument call (which places the iterator
    // outside the previous doc_key). Ensure the iterator is pushed forward/backward indeed. We
    // check it here instead of after GetSubDocument() below because we want to avoid the extra
    // expensive FetchKey() call just to fetch and validate the key.
    if (!iter_key_.data().empty() &&
        (is_forward_scan_ ? iter_key_.CompareTo(key_data->key) >= 0
                          : iter_key_.CompareTo(key_data->key) <= 0)) {
      // TODO -- could turn this check off in TPCC?
      has_next_status_ = STATUS_SUBSTITUTE(Corruption, "Infinite loop detected at $0",
                                           FormatSliceAsStr(key_data->key));
      return has_next_status_;
    }
    iter_key_.Reset(key_data->key);
    VLOG(4) << " Current iter_key_ is " << iter_key_;

    const auto dockey_sizes = DocKey::EncodedHashPartAndDocKeySizes(iter_key_);
    if (!dockey_sizes.ok()) {
      has_next_status_ = dockey_sizes.status();
      return has_next_status_;
    }
    row_hash_key_ = iter_key_.AsSlice().Prefix(dockey_sizes->hash_part_size);
    row_key_ = iter_key_.AsSlice().Prefix(dockey_sizes->doc_key_size);

    // e.g in cotable, row may point outside table bounds
    if (!DocKeyBelongsTo(row_key_, doc_read_context_.schema) ||
        (has_bound_key_ && is_forward_scan_ == (row_key_.compare(bound_key_) >= 0))) {
      done_ = true;
      return false;
    }

    // Prepare the DocKey to get the SubDocument. Trim the DocKey to contain just the primary key.
    Slice doc_key = row_key_;
    VLOG(4) << " sub_doc_key part of iter_key_ is " << DocKey::DebugSliceToString(doc_key);

    bool is_static_column = IsNextStaticColumn();
    if (scan_choices_ && !is_static_column) {
      if (!scan_choices_->CurrentTargetMatchesKey(row_key_)) {
        // We must have seeked past the target key we are looking for (no result) so we can safely
        // skip all scan targets between the current target and row key (excluding row_key_ itself).
        // Update the target key and iterator and call HasNext again to try the next target.
        RETURN_NOT_OK(scan_choices_->SkipTargetsUpTo(row_key_));

        // We updated scan target above, if it goes past the row_key_ we will seek again, and
        // process the found key in the next loop.
        if (!scan_choices_->CurrentTargetMatchesKey(row_key_)) {
          RETURN_NOT_OK(scan_choices_->SeekToCurrentTarget(db_iter_.get()));
          continue;
        }
      }
      // We found a match for the target key or a static column, so we move on to getting the
      // SubDocument.
    }
    if (doc_reader_ == nullptr) {
      doc_reader_ = std::make_unique<DocDBTableReader>(
          db_iter_.get(), deadline_, &projection_subkeys_, table_type_,
          doc_read_context_.schema_packing_storage);
      RETURN_NOT_OK(doc_reader_->UpdateTableTombstoneTime(doc_key));
      if (!ignore_ttl_) {
        doc_reader_->SetTableTtl(doc_read_context_.schema);
      }
    }

    DCHECK(row_.type() == ValueEntryType::kObject);
    row_.object_container().clear();
    auto doc_found_res = doc_reader_->Get(doc_key, &row_);
    if (!doc_found_res.ok()) {
      has_next_status_ = doc_found_res.status();
      return has_next_status_;
    } else {
      doc_found = *doc_found_res;
    }
    if (scan_choices_ && !is_static_column) {
      has_next_status_ = scan_choices_->DoneWithCurrentTarget();
      RETURN_NOT_OK(has_next_status_);
    }
    has_next_status_ = AdvanceIteratorToNextDesiredRow();
    RETURN_NOT_OK(has_next_status_);
    VLOG(4) << __func__ << ", iter: " << db_iter_->valid();
  }
  row_ready_ = true;
  return true;
}

string DocRowwiseIterator::ToString() const {
  return "DocRowwiseIterator";
}

namespace {

// Set primary key column values (hashed or range columns) in a QL row value map.
Status SetQLPrimaryKeyColumnValues(const Schema& schema,
                                   const size_t begin_index,
                                   const size_t column_count,
                                   const char* column_type,
                                   DocKeyDecoder* decoder,
                                   QLTableRow* table_row) {
  if (begin_index + column_count > schema.num_columns()) {
    return STATUS_SUBSTITUTE(
        Corruption,
        "$0 primary key columns between positions $1 and $2 go beyond table columns $3",
        column_type, begin_index, begin_index + column_count - 1, schema.num_columns());
  }
  KeyEntryValue key_entry_value;
  for (size_t i = 0, j = begin_index; i < column_count; i++, j++) {
    const auto ql_type = schema.column(j).type();
    QLTableColumn& column = table_row->AllocColumn(schema.column_id(j));
    RETURN_NOT_OK(decoder->DecodeKeyEntryValue(&key_entry_value));
    key_entry_value.ToQLValuePB(ql_type, &column.value);
  }
  return decoder->ConsumeGroupEnd();
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
  return doc_read_context_.schema.has_statics() && row_hash_key_.end() + 1 == row_key_.end();
}

Status DocRowwiseIterator::DoNextRow(const Schema& projection, QLTableRow* table_row) {
  VLOG(4) << __PRETTY_FUNCTION__;

  if (PREDICT_FALSE(done_)) {
    return STATUS(NotFound, "end of iter");
  }

  // Ensure row is ready to be read. HasNext() must be called before reading the first row, or
  // again after the previous row has been read or skipped.
  if (!row_ready_) {
    return STATUS(InternalError, "next row has not be prepared for reading");
  }

  DocKeyDecoder decoder(row_key_);
  RETURN_NOT_OK(decoder.DecodeCotableId());
  RETURN_NOT_OK(decoder.DecodeColocationId());
  bool has_hash_components = VERIFY_RESULT(decoder.DecodeHashCode());

  // Populate the key column values from the doc key. The key column values in doc key were
  // written in the same order as in the table schema (see DocKeyFromQLKey). If the range columns
  // are present, read them also.
  if (has_hash_components) {
    RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
        doc_read_context_.schema, 0, doc_read_context_.schema.num_hash_key_columns(),
        "hash", &decoder, table_row));
  }
  if (!decoder.GroupEnded()) {
    RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
        doc_read_context_.schema, doc_read_context_.schema.num_hash_key_columns(),
        doc_read_context_.schema.num_range_key_columns(), "range", &decoder, table_row));
  }

  for (size_t i = projection.num_key_columns(); i < projection.num_columns(); i++) {
    const auto& column_id = projection.column_id(i);
    const auto ql_type = projection.column(i).type();
    const SubDocument* column_value = row_.GetChild(KeyEntryValue::MakeColumnId(column_id));
    if (column_value != nullptr) {
      QLTableColumn& column = table_row->AllocColumn(column_id);
      column_value->ToQLValuePB(ql_type, &column.value);
      column.ttl_seconds = column_value->GetTtl();
      if (column_value->IsWriteTimeSet()) {
        column.write_time = column_value->GetWriteTime();
      }
    }
  }

  VLOG_WITH_FUNC(4) << "Returning row: " << table_row->ToString();

  row_ready_ = false;
  return Status::OK();
}

bool DocRowwiseIterator::LivenessColumnExists() const {
  const SubDocument* subdoc = row_.GetChild(KeyEntryValue::kLivenessColumn);
  return subdoc != nullptr && subdoc->value_type() != ValueEntryType::kInvalid;
}

Status DocRowwiseIterator::GetNextReadSubDocKey(SubDocKey* sub_doc_key) {
  if (db_iter_ == nullptr) {
    return STATUS(Corruption, "Iterator not initialized.");
  }

  // There are no more rows to fetch, so no next SubDocKey to read.
  if (!VERIFY_RESULT(HasNext())) {
    DVLOG(3) << "No Next SubDocKey";
    return Status::OK();
  }

  DocKey doc_key;
  RETURN_NOT_OK(doc_key.FullyDecodeFrom(row_key_));
  *sub_doc_key = SubDocKey(doc_key, read_time_.read);
  DVLOG(3) << "Next SubDocKey: " << sub_doc_key->ToString();
  return Status::OK();
}

Result<Slice> DocRowwiseIterator::GetTupleId() const {
  // Return tuple id without cotable id / colocation id if any.
  Slice tuple_id = row_key_;
  if (tuple_id.starts_with(KeyEntryTypeAsChar::kTableId)) {
    tuple_id.remove_prefix(1 + kUuidSize);
  } else if (tuple_id.starts_with(KeyEntryTypeAsChar::kColocationId)) {
    tuple_id.remove_prefix(1 + sizeof(ColocationId));
  }
  return tuple_id;
}

Result<bool> DocRowwiseIterator::SeekTuple(const Slice& tuple_id) {
  // If cotable id / colocation id is present in the table schema, then
  // we need to prepend it in the tuple key to seek.
  if (doc_read_context_.schema.has_cotable_id() || doc_read_context_.schema.has_colocation_id()) {
    uint32_t size = doc_read_context_.schema.has_colocation_id() ? sizeof(ColocationId) : kUuidSize;
    if (!tuple_key_) {
      tuple_key_.emplace();
      tuple_key_->Reserve(1 + size + tuple_id.size());

      if (doc_read_context_.schema.has_cotable_id()) {
        std::string bytes;
        doc_read_context_.schema.cotable_id().EncodeToComparable(&bytes);
        tuple_key_->AppendKeyEntryType(KeyEntryType::kTableId);
        tuple_key_->AppendRawBytes(bytes);
      } else {
        tuple_key_->AppendKeyEntryType(KeyEntryType::kColocationId);
        tuple_key_->AppendUInt32(doc_read_context_.schema.colocation_id());
      }
    } else {
      tuple_key_->Truncate(1 + size);
    }
    tuple_key_->AppendRawBytes(tuple_id);
    db_iter_->Seek(*tuple_key_);
  } else {
    db_iter_->Seek(tuple_id);
  }

  iter_key_.Clear();
  row_ready_ = false;

  return VERIFY_RESULT(HasNext()) && VERIFY_RESULT(GetTupleId()) == tuple_id;
}

}  // namespace docdb
}  // namespace yb

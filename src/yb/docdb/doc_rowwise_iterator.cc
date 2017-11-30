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

#include "yb/common/iterator.h"
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

namespace {

CHECKED_STATUS ExpectValueType(ValueType expected_value_type,
                               const ColumnSchema& column,
                               const PrimitiveValue& actual_value) {
  if (expected_value_type == actual_value.value_type()) {
    return Status::OK();
  } else {
    return STATUS_FORMAT(Corruption,
        "Expected an internal value type $0 for column $1 of physical SQL type $2, got $3",
        expected_value_type,
        column.name(),
        column.type_info()->name(),
        actual_value.value_type());
  }
}

}  // namespace

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

Status DocRowwiseIterator::Init(ScanSpec *spec) {
  // TODO(bogdan): refactor this after we completely move away from the old ScanSpec. For now, just
  // default to not using bloom filters on scans for these codepaths.
  db_iter_ = CreateIntentAwareIterator(
      db_, BloomFilterMode::DONT_USE_BLOOM_FILTER, boost::none /* user_key_for_filter */,
      spec->query_id(), txn_op_context_, read_time_);

  if (spec != nullptr && spec->lower_bound_key() != nullptr) {
    row_key_ = KuduToDocKey(*spec->lower_bound_key());
  } else {
    row_key_ = DocKey();
  }
  RETURN_NOT_OK(db_iter_->Seek(row_key_));
  row_ready_ = false;

  if (spec != nullptr && spec->exclusive_upper_bound_key() != nullptr) {
    has_bound_key_ = true;
    bound_key_ = KuduToDocKey(*spec->exclusive_upper_bound_key());
  } else {
    has_bound_key_ = false;
  }
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

  RETURN_NOT_OK(db_iter_->SeekWithoutHt(row_key_encoded));
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
       RETURN_NOT_OK(db_iter_->Seek(lower_doc_key));
    }
  } else {
    if (has_bound_key_) {
      RETURN_NOT_OK(db_iter_->PrevDocKey(upper_doc_key));
    } else {
      RETURN_NOT_OK(db_iter_->SeekToLastDocKey());
    }
  }

  return Status::OK();
}

Status DocRowwiseIterator::EnsureIteratorPositionCorrect() const {
  if (!is_forward_scan_) {
    RETURN_NOT_OK(db_iter_->PrevDocKey(row_key_));
  }
  return Status::OK();
}


bool DocRowwiseIterator::HasNext() const {
  if (!status_.ok() || row_ready_) {
    // If row is ready, then HasNext returns true. In case of error, NextBlock() / NextRow() will
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
    rocksdb::Slice rocksdb_key(db_iter_->key());
    status_ = row_key_.DecodeFrom(&rocksdb_key);
    if (!status_.ok()) {
      // Defer error reporting to NextBlock().
      return true;
    }

    if (has_bound_key_ && is_forward_scan_ == (row_key_ >= bound_key_)) {
      done_ = true;
      return false;
    }

    KeyBytes old_key(db_iter_->key());
    // The iterator is positioned by the previous GetSubDocument call
    // (which places the iterator outside the previous doc_key).
    status_ = GetSubDocument(
        db_iter_.get(), SubDocKey(row_key_), &row_, &doc_found, TableTTL(schema_),
        &projection_subkeys_);
    // After this, the iter should be positioned right after the subdocument.
    if (!status_.ok()) {
      // Defer error reporting to NextBlock().
      return true;
    }

    if (!doc_found) {
      SubDocument full_row;
      // If doc is not found, decide if some non-projection column exists.
      // Currently we read the whole doc here,
      // may be optimized by exiting on the first column in future.
      status_ = db_iter_->Seek(row_key_);  // Position it for GetSubDocument.
      if (!status_.ok()) {
        // Defer error reporting to NextBlock().
        return true;
      }
      status_ = GetSubDocument(
          db_iter_.get(), SubDocKey(row_key_), &full_row, &doc_found, TableTTL(schema_));
      if (!status_.ok()) {
        // Defer error reporting to NextBlock().
        return true;
      }
    }
    // GetSubDocument must ensure that iterator is pushed forward, to avoid loops.
    if (db_iter_->valid() && old_key.AsSlice().compare(db_iter_->key()) >= 0) {
      status_ = STATUS_SUBSTITUTE(Corruption, "Infinite loop detected at $0",
          FormatRocksDBSliceAsStr(old_key.AsSlice()));
      return true;
    }
    status_ = EnsureIteratorPositionCorrect();
    if (!status_.ok()) {
      // Defer error reporting to NextBlock().
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

// Convert from a PrimitiveValue read from RocksDB to a Kudu value in the given column of the
// given RowBlockRow. The destination row's schema must match that of the projection.
CHECKED_STATUS PrimitiveValueToKudu(const Schema& projection,
                                    const int column_index,
                                    const PrimitiveValue& value,
                                    RowBlockRow* dest_row) {
  const ColumnSchema& col_schema = projection.column(column_index);
  const DataType data_type = col_schema.type_info()->physical_type();
  void* const dest_ptr = dest_row->cell(column_index).mutable_ptr();
  Arena* const arena = dest_row->row_block()->arena();
  switch (data_type) {
    case DataType::BINARY: FALLTHROUGH_INTENDED;
    case DataType::STRING: {
      Slice cell_copy;
      RETURN_NOT_OK(ExpectValueType(ValueType::kString, col_schema, value));
      if (PREDICT_FALSE(!arena->RelocateSlice(value.GetStringAsSlice(), &cell_copy))) {
        return STATUS(IOError, "out of memory");
      }
      // Kudu represents variable-size values as Slices pointing to memory allocated from an
      // associated Arena.
      *(reinterpret_cast<Slice*>(dest_ptr)) = cell_copy;
      break;
    }
    case DataType::INT64: {
      RETURN_NOT_OK(ExpectValueType(ValueType::kInt64, col_schema, value));
      // TODO: double-check that we can just assign the 64-bit integer here.
      *(reinterpret_cast<int64_t*>(dest_ptr)) = value.GetInt64();
      break;
    }
    case DataType::INT32: {
      // TODO: update these casts when all data types are supported in docdb
      RETURN_NOT_OK(ExpectValueType(ValueType::kInt32, col_schema, value));
      *(reinterpret_cast<int32_t*>(dest_ptr)) = static_cast<int32_t>(value.GetInt32());
      break;
    }
    case DataType::INT16: {
      // TODO: update these casts when all data types are supported in docdb
      RETURN_NOT_OK(ExpectValueType(ValueType::kInt32, col_schema, value));
      *(reinterpret_cast<int16_t*>(dest_ptr)) = static_cast<int16_t>(value.GetInt32());
      break;
    }
    case DataType::INT8: {
      // TODO: update these casts when all data types are supported in docdb
      RETURN_NOT_OK(ExpectValueType(ValueType::kInt32, col_schema, value));
      *(reinterpret_cast<int8_t*>(dest_ptr)) = static_cast<int8_t>(value.GetInt32());
      break;
    }
    case DataType::BOOL: {
      if (value.value_type() != ValueType::kTrue &&
          value.value_type() != ValueType::kFalse) {
        return STATUS_SUBSTITUTE(Corruption,
                                 "Expected true/false, got $0", value.ToString());
      }
      *(reinterpret_cast<bool*>(dest_ptr)) = value.value_type() == ValueType::kTrue;
      break;
    }
    default:
      return STATUS_SUBSTITUTE(IllegalState,
                               "Unsupported column data type $0", data_type);
  }
        return Status::OK();
  }

// Set primary key column values (hashed or range columns) in a Kudu row. The destination row's
// schema must match that of the projection.
  CHECKED_STATUS SetKuduPrimaryKeyColumnValues(const Schema& projection,
      const size_t begin_index,
      const size_t column_count,
      const char* column_type,
      const vector<PrimitiveValue>& values,
      RowBlockRow* dst_row) {
    if (values.size() != column_count) {
      return STATUS_SUBSTITUTE(Corruption, "$0 $1 primary key columns found but $2 expected",
      values.size(), column_type, column_count);
  }
  if (begin_index + column_count > projection.num_columns()) {
    return STATUS_SUBSTITUTE(
        Corruption,
        "$0 primary key columns between positions $1 and $2 go beyond selected columns $3",
        column_type, begin_index, begin_index + column_count - 1, projection.num_columns());
  }
  for (size_t i = 0, j = begin_index; i < column_count; i++, j++) {
    RETURN_NOT_OK(PrimitiveValueToKudu(projection, j, values[i], dst_row));
  }
  return Status::OK();
}

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
    const auto column_id = schema.column_id(j);
    const auto ql_type = schema.column(j).type();
    PrimitiveValue::ToQLValuePB(values[i], ql_type, &(*table_row)[column_id].value);
  }
  return Status::OK();
}

} // namespace

Status DocRowwiseIterator::NextBlock(RowBlock* dst) {
  // Verify the basic compatibility of the schema assumed by the row block provided to us to the
  // projection schema we already have.
  DCHECK_EQ(projection_.num_key_columns(), dst->schema().num_key_columns());
  DCHECK_EQ(projection_.num_columns(), dst->schema().num_columns());

  if (!status_.ok()) {
    // An error happened in HasNext.
    return status_;
  }

  if (PREDICT_FALSE(done_)) {
    dst->Resize(0);
    return STATUS(NotFound, "end of iter");
  }

  // Ensure row is ready to be read. HasNext() must be called before reading the first row, or
  // again after the previous row has been read or skipped.
  if (!row_ready_) {
    return STATUS(InternalError, "next row has not be prepared for reading");
  }

  if (PREDICT_FALSE(dst->row_capacity() == 0)) {
    return Status::OK();
  }

  dst->Resize(1);
  dst->selection_vector()->SetAllTrue();
  RowBlockRow dst_row(dst->row(0));

  // Populate the key column values from the doc key. We require that when a projection selects
  // either hash or range columns, all hash or range columns are selected.
  if (projection_.num_hash_key_columns() > 0) {
    CHECK_EQ(projection_.num_hash_key_columns(), schema_.num_hash_key_columns())
        << "projection's hash column count does not match schema's";
    RETURN_NOT_OK(SetKuduPrimaryKeyColumnValues(
        projection_, 0, projection_.num_hash_key_columns(),
        "hash", row_key_.hashed_group(), &dst_row));
  }
  if (projection_.num_range_key_columns() > 0) {
    CHECK_EQ(projection_.num_range_key_columns(), schema_.num_range_key_columns())
        << "projection's range column count does not match schema's";
    RETURN_NOT_OK(SetKuduPrimaryKeyColumnValues(
        projection_, projection_.num_hash_key_columns(), projection_.num_range_key_columns(),
        "range", row_key_.range_group(), &dst_row));
  }

  for (size_t i = projection_.num_key_columns(); i < projection_.num_columns(); i++) {
    const SubDocument* value = row_.GetChild(PrimitiveValue(projection_.column_id(i)));
    const bool is_null = value->value_type() == ValueType::kInvalidValueType;
    const bool is_nullable = dst->column_block(i).is_nullable();
    if (!is_null) {
      RETURN_NOT_OK(PrimitiveValueToKudu(projection_, i, *value, &dst_row));
    }
    if (is_null && !is_nullable) {
      // From D1319 (mikhail) to make some tests run with QL tables:
      // We can't use dst->schema().column(i) here, because it might not provide the correct read
      // default. Instead, we get the matching column's schema from the server-side schema that
      // the iterator was created with.
      int idx = schema_.find_column_by_id(projection_.column_id(i));
      if (idx != Schema::kColumnNotFound && schema_.column(idx).has_read_default()) {
        memcpy(dst_row.cell(i).mutable_ptr(), schema_.column(idx).read_default_value(),
               schema_.column(idx).type_info()->size());
      } else {
        return STATUS_SUBSTITUTE(
            IllegalState,
            "Column #$0 in the projection ($1) is not nullable, but no data is found and no read "
                "default provided by the schema",
            i,
            schema_.column(idx).ToString());
      }
    }
    if (is_nullable) {
      dst->column_block(i).SetCellIsNull(0, is_null);
    }
  }
  row_ready_ = false;
  return Status::OK();
}

void DocRowwiseIterator::SkipRow() {
  row_ready_ = false;
}

bool DocRowwiseIterator::IsNextStaticColumn() const {
  return schema_.has_statics() && row_key_.range_group().empty();
}

Status DocRowwiseIterator::NextRow(const Schema& projection, QLTableRow* table_row) {
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
      SubDocument::ToQLValuePB(*column_value, ql_type, &(*table_row)[column_id].value);
      (*table_row)[column_id].ttl_seconds = column_value->GetTtl();
      (*table_row)[column_id].write_time = column_value->GetWriteTime();
    }
  }
  row_ready_ = false;
  return Status::OK();
}

void DocRowwiseIterator::GetIteratorStats(std::vector<IteratorStats>* stats) const {
  // A no-op implementation that adds new IteratorStats objects. This is an attempt to fix
  // linked_list-test with the QL table type.
  for (int i = 0; i < projection_.num_columns(); i++) {
    stats->emplace_back();
  }
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

// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/common/iterator.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/docdb/subdocument.h"

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
    return STATUS_SUBSTITUTE(Corruption,
        "Expected a internal value type $0 for column $1 of physical SQL type $2, got $3",
        ValueTypeToStr(expected_value_type),
        column.name(),
        column.type_info()->name(),
        ValueTypeToStr(actual_value.value_type()));
  }
}

}  // namespace

DocRowwiseIterator::DocRowwiseIterator(const Schema &projection,
                                       const Schema &schema,
                                       rocksdb::DB *db,
                                       HybridTime hybrid_time,
                                       yb::util::PendingOperationCounter* pending_op_counter)
    : projection_(projection),
      schema_(schema),
      hybrid_time_(hybrid_time),
      db_(db),
      has_upper_bound_key_(false),
      pending_op_(pending_op_counter),
      done_(false),
      row_delete_marker_time_(HybridTime::kInvalidHybridTime) {
}

DocRowwiseIterator::~DocRowwiseIterator() {
}

Status DocRowwiseIterator::Init(ScanSpec *spec) {
  // TODO(bogdan): refactor this after we completely move away from the old ScanSpec. For now, just
  // default to not using bloom filters on scans for these codepaths.
  db_iter_ = CreateRocksDBIterator(db_, false /* use_bloom_on_scan */);

  if (spec->lower_bound_key() != nullptr) {
    ROCKSDB_SEEK(db_iter_.get(), SubDocKey(KuduToDocKey(*spec->lower_bound_key()),
                                           hybrid_time_).Encode().AsSlice());
  } else {
    // The equivalent of db_iter_->SeekToFirst().
    ROCKSDB_SEEK(db_iter_.get(), "");
  }

  if (spec->exclusive_upper_bound_key() != nullptr) {
    has_upper_bound_key_ = true;
    exclusive_upper_bound_key_ = KuduToDocKey(*spec->exclusive_upper_bound_key()).Encode();
  } else {
    has_upper_bound_key_ = false;
  }

  return Status::OK();
}

Status DocRowwiseIterator::Init(const YQLScanSpec& spec) {
  // TODO(bogdan): decide if this is a good enough heuristic for using blooms for scans.
  DocKey lower_doc_key;
  DocKey upper_doc_key;
  RETURN_NOT_OK(spec.lower_bound(&lower_doc_key));
  RETURN_NOT_OK(spec.upper_bound(&upper_doc_key));
  const bool is_fixed_point_get = !lower_doc_key.empty() && upper_doc_key == lower_doc_key;
  db_iter_ = CreateRocksDBIterator(db_, is_fixed_point_get /* use_bloom_on_scan */);

  // Start scan with the lower bound doc key.
  // If there is no lower bound, start from the beginning.
  if (!lower_doc_key.empty()) {
    ROCKSDB_SEEK(db_iter_.get(), SubDocKey(lower_doc_key, hybrid_time_).Encode().AsSlice());
  } else {
    ROCKSDB_SEEK(db_iter_.get(), "");
  }

  // End scan with the upper bound key bytes.
  if (!upper_doc_key.empty()) {
    has_upper_bound_key_ = true;
    exclusive_upper_bound_key_ = SubDocKey(upper_doc_key).AdvanceOutOfDocKeyPrefix();
  } else {
    has_upper_bound_key_ = false;
  }
  return Status::OK();
}

bool DocRowwiseIterator::IsDeletedByRowDeletion(const SubDocKey& subdoc_key) const {
  if (subdoc_key.doc_key() != row_delete_marker_key_) {
    // The current delete marker key doesn't apply for this column.
    return false;
  }

  if (row_delete_marker_time_ != HybridTime::kInvalidHybridTime &&
      subdoc_key.hybrid_time() < row_delete_marker_time_) {
    // This column isn't valid since there is a row level delete marker at a higher timestamp.
    // TODO: If we have an insert and delete at the same timestamp (could be possible in
    // transactions), we currently choose the insert to win. This isn't correct since we could
    // have deletes after an insert in a transaction and in that case we want to keep the delete.
    // This behavior will be fixed in the future.
    return true;
  }
  return false;
}

Status DocRowwiseIterator::FindValidColumn(bool* column_found) const {
  // We've found a column for the row, now check if the column is valid.
  *column_found = false;
  RETURN_NOT_OK(CheckColumnValidity(subdoc_key_, top_level_value_, column_found))

  if (!(*column_found)) {
    // This column has expired or has been deleted or is not part of the projection, look for the
    // next column.
    ROCKSDB_SEEK(db_iter_.get(), subdoc_key_.AdvanceOutOfSubDoc().AsSlice());
  }

  // Otherwise, we've found a valid column and as a result the row exists.
  return Status::OK();
}

Status DocRowwiseIterator::ProcessColumnsForHasNext(bool* column_found) const {
  const PrimitiveValue& last_key = subdoc_key_.last_subkey();

  switch (last_key.value_type()) {
    case ValueType::kColumnId: {
      RETURN_NOT_OK(FindValidColumn(column_found));
      break;
    }
    case ValueType::kSystemColumnId: {
      // We have a system column id here.
      if (last_key.GetColumnId() !=
          ColumnId(static_cast<ColumnIdRep>(SystemColumnIds::kLivenessColumn))) {
        // We found an invalid system column here.
        return STATUS_SUBSTITUTE(Corruption,
                                 "Expected system column id $0, found $1",
                                 static_cast<int32_t>(SystemColumnIds::kLivenessColumn),
                                 last_key.GetColumnId());
      }
      RETURN_NOT_OK(FindValidColumn(column_found));
      break;
    }
    default:
      return STATUS_SUBSTITUTE(Corruption,
                               "Expected value type $0 or $1 in first subkey, found $2",
                               ValueTypeToStr(ValueType::kColumnId),
                               ValueTypeToStr(ValueType::kSystemColumnId),
                               ValueTypeToStr(last_key.value_type()));
  }
  return Status::OK();
}

bool DocRowwiseIterator::HasNext() const {
  if (!status_.ok()) {
    // Unfortunately, we don't have a way to return an error status here, so we save it until the
    // next time NextBlock is called. This is also the reason why we have to return true in error
    // cases.
    return true;
  }

  if (done_) return false;

  // Use empty string as initial previous key value in order to not be equal to real key.
  string prev_rocksdb_key;

  while (true) {
    if (db_iter_ == nullptr || !db_iter_->Valid() ||
        (has_upper_bound_key_ && exclusive_upper_bound_key_.CompareTo(db_iter_->key()) <= 0)) {
      done_ = true;
      return false;
    }

    status_ = subdoc_key_.FullyDecodeFrom(db_iter_->key());
    if (!status_.ok()) {
      // Defer error reporting to NextBlock.
      return true;
    }

    if (subdoc_key_.doc_key() != row_delete_marker_key_) {
      // The delete marker is no longer valid for this subdoc key.
      row_delete_marker_time_ = HybridTime::kInvalidHybridTime;
      row_delete_marker_key_.Clear();
    }

    if (prev_rocksdb_key == db_iter_->key()) {
      // Infinite loop detected, defer error reporting to NextBlock.
      status_ = STATUS_SUBSTITUTE(Corruption,
          "Infinite loop detected at $0", subdoc_key_.ToString());
      return true;
    }
    prev_rocksdb_key = db_iter_->key().ToString();

    // We expect to find a tombstone, a liveness column or a regular column which all have
    // num_subkeys <= 1. Since we don't support non-primitive column values yet, we can't have
    // more than 1 subkey.
    if (subdoc_key_.num_subkeys() > 1) {
      // Defer error reporting to NextBlock.
      status_ = STATUS_SUBSTITUTE(Corruption,
                                  "Did not expect to find any subkeys when starting row "
                                  "iteration, found $0", subdoc_key_.num_subkeys());
      return true;
    }

    if (subdoc_key_.hybrid_time().CompareTo(hybrid_time_) > 0) {
      // This document was fully overwritten after the hybrid_time we are trying to scan at. We have
      // to read the latest version of the document that existed at the specified hybrid_time
      // instead.
      subdoc_key_.set_hybrid_time(hybrid_time_);
      ROCKSDB_SEEK(db_iter_.get(), subdoc_key_.Encode().AsSlice());
      continue;
    }

    status_ = top_level_value_.Decode(db_iter_->value());
    if (!status_.ok()) {
      // Defer error reporting to NextBlock.
      return true;
    }
    const auto value_type = top_level_value_.value_type();

    if (subdoc_key_.num_subkeys() == 1) {
      // We should expect a liveness column or a regular user column here. We could have a regular
      // column since the liveness column might have been compacted away or the row was added
      // using an UPDATE statement.
      bool column_found = false;
      status_ = ProcessColumnsForHasNext(&column_found);
      if (!status_.ok()) {
        // Defer error reporting to NextBlock.
        return true;
      }

      if (!column_found) {
        // Continue looking for valid columns.
        continue;
      }
      return true;
    }

    // Otherwise, the only possibility left here is that we have a tombstone marker for the row.
    if (value_type != ValueType::kTombstone) {
      // Defer error reporting to NextBlock.
      status_ = STATUS_SUBSTITUTE(Corruption,
          "Invalid value type at the top of a document, row tombstone expected, found: $0",
          ValueTypeToStr(value_type));
      return true;
    }

    // We have a tombstone marker here, store its timestamp and continue looking for columns
    // since there could have been columns created after the delete timestamp.
    row_delete_marker_time_ = subdoc_key_.hybrid_time();
    row_delete_marker_key_ = subdoc_key_.doc_key();
    // Seek for the first system column within a row since its sorted at the top.
    KeyBytes encoded_subdoc_key = subdoc_key_.Encode(/* include_hybrid_time = */ false);
    encoded_subdoc_key.AppendValueType(ValueType::kSystemColumnId);
    ROCKSDB_SEEK(db_iter_.get(), encoded_subdoc_key.AsSlice());
  }
}

string DocRowwiseIterator::ToString() const {
  return "DocRowwiseIterator";
}

Status DocRowwiseIterator::CheckColumnValidity(const SubDocKey& subdoc_key,
                                               const Value& value,
                                               bool* is_valid) const {
  // At this point, we could be pointing to a liveness system column or a regular column.
  const ValueType value_type = subdoc_key.last_subkey().value_type();
  if (value_type != ValueType::kColumnId && value_type != ValueType::kSystemColumnId) {
    return STATUS_SUBSTITUTE(Corruption, "Expected $0 or $1, found $2",
                             ValueTypeToStr(ValueType::kColumnId),
                             ValueTypeToStr(ValueType::kSystemColumnId),
                             ValueTypeToStr(value_type));
  }

  // Check for TTL.
  bool has_expired = false;
  *is_valid = false;
  RETURN_NOT_OK(HasExpiredTTL(subdoc_key.hybrid_time(), ComputeTTL(value.ttl(), schema_),
                              hybrid_time_, &has_expired));

  if (value.value_type() != ValueType::kTombstone &&
      !has_expired && !IsDeletedByRowDeletion(subdoc_key)) {
    *is_valid = true;
  }
  return Status::OK();
}

Status DocRowwiseIterator::ProcessValues(const Value& value, const SubDocKey& subdoc_key,
                                         vector<PrimitiveValue>* values,
                                         bool *is_null) const {
  *is_null = true;
  bool is_valid = false;
  RETURN_NOT_OK(CheckColumnValidity(subdoc_key, value, &is_valid));

  if (is_valid) {
    DOCDB_DEBUG_LOG("Found a non-null value for column #$0: $1", i, value.ToString());
    values->emplace_back(value.primitive_value());
    *is_null = false;
  } else {
    DOCDB_DEBUG_LOG("Found a null value for column #$0: $1", i, value.ToString());
  }
  return Status::OK();
}

// Get the non-key column values of a YQL row and advance to the next row before return.
Status DocRowwiseIterator::GetValues(const Schema& projection, vector<SubDocument>* values) {

  values->reserve(projection.num_columns() - projection.num_key_columns());

  // This column should be valid considering we checked this in HasNext(), although double
  // check here for sanity.
  bool is_valid = false;
  RETURN_NOT_OK(CheckColumnValidity(subdoc_key_, top_level_value_, &is_valid));
  if (!is_valid) {
    return STATUS_SUBSTITUTE(Corruption, "Expected $0 with value $1 to be a valid key, but found "
        "otherwise. Contract between HasNext and GetValues is violated!", subdoc_key_.ToString(),
                             top_level_value_.ToString());
  }

  ColumnId column_id_processed(kInvalidColumnId);
  PrimitiveValue column_processed_value(ValueType::kNull);
  ValueType value_type = subdoc_key_.last_subkey().value_type();
  if (value_type == ValueType::kColumnId) {
    // If this is a regular column, save its value so that we don't have to seek for it in the
    // loop below.
    column_processed_value = top_level_value_.primitive_value();
    column_id_processed = subdoc_key_.last_subkey().GetColumnId();
  }

  // At this point, we could be pointing to a liveness system column or a regular column.
  // Remove the last subkey, to allow searches for the next column.
  subdoc_key_.RemoveLastSubKey();

  MonoDelta table_ttl = TableTTL(schema_);
  for (size_t i = projection_.num_key_columns(); i < projection.num_columns(); i++) {
    const auto& column_id = projection.column_id(i);
    if (column_id == column_id_processed) {
      // Fill in this column id since we already processed it above.
      values->emplace_back(column_processed_value);
      continue;
    }
    subdoc_key_.RemoveHybridTime();
    subdoc_key_.AppendSubKeysAndMaybeHybridTime(PrimitiveValue(column_id));
    SubDocument sub_doc;
    bool doc_found;
    RETURN_NOT_OK(GetSubDocument(db_, subdoc_key_, &sub_doc, &doc_found, hybrid_time_, table_ttl));
    if (doc_found) {
      values->emplace_back(std::move(sub_doc));
    } else {
      values->emplace_back(PrimitiveValue(ValueType::kNull));
    }
    subdoc_key_.RemoveLastSubKey();
  }

  // Advance to the next row of our document since we are done processing columns for this row.
  ROCKSDB_SEEK(db_iter_.get(), subdoc_key_.AdvanceOutOfDocKeyPrefix().AsSlice());

  return Status::OK();
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
      RETURN_NOT_OK(ExpectValueType(ValueType::kInt64, col_schema, value));
      *(reinterpret_cast<int32_t*>(dest_ptr)) = static_cast<int32_t>(value.GetInt64());
      break;
    }
    case DataType::INT8: {
      // TODO: update these casts when all data types are supported in docdb
      RETURN_NOT_OK(ExpectValueType(ValueType::kInt64, col_schema, value));
      *(reinterpret_cast<int8_t*>(dest_ptr)) = static_cast<int8_t>(value.GetInt64());
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

// Set primary key column values (hashed or range columns) in a YQL row value map.
CHECKED_STATUS SetYQLPrimaryKeyColumnValues(const Schema& schema,
                                            const size_t begin_index,
                                            const size_t column_count,
                                            const char* column_type,
                                            const vector<PrimitiveValue>& values,
                                            YQLValueMap* value_map) {
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
    const auto yql_type = schema.column(j).type();
    PrimitiveValue::ToYQLValuePB(values[i], yql_type, &(*value_map)[column_id]);
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
        "hash", subdoc_key_.doc_key().hashed_group(), &dst_row));
  }
  if (projection_.num_range_key_columns() > 0) {
    CHECK_EQ(projection_.num_range_key_columns(), schema_.num_range_key_columns())
        << "projection's range column count does not match schema's";
    RETURN_NOT_OK(SetKuduPrimaryKeyColumnValues(
        projection_, projection_.num_hash_key_columns(), projection_.num_range_key_columns(),
        "range", subdoc_key_.doc_key().range_group(), &dst_row));
  }

  // Get the non-key column values of a row.
  vector<SubDocument> values;
  RETURN_NOT_OK(GetValues(projection_, &values));
  for (size_t i = projection_.num_key_columns(); i < projection_.num_columns(); i++) {
    const auto& value = values[i - projection_.num_key_columns()];
    const bool is_null = (value.value_type() == ValueType::kNull);
    const bool is_nullable = dst->column_block(i).is_nullable();
    if (!is_null) {
      RETURN_NOT_OK(PrimitiveValueToKudu(projection_, i, value, &dst_row));
    }
    if (is_null && !is_nullable) {
      return STATUS_SUBSTITUTE(IllegalState,
                               "Column $0 is not nullable, but no data is found", i);
    }
    if (is_nullable) {
      dst->column_block(i).SetCellIsNull(0, is_null);
    }
  }

  return Status::OK();
}

Status DocRowwiseIterator::NextRow(YQLValueMap* value_map) {
  if (!status_.ok()) {
    // An error happened in HasNext.
    return status_;
  }

  if (PREDICT_FALSE(done_)) {
    return STATUS(NotFound, "end of iter");
  }

  // Populate the key column values from the doc key. The key column values in doc key were
  // written in the same order as in the table schema (see DocKeyFromYQLKey).
  RETURN_NOT_OK(SetYQLPrimaryKeyColumnValues(
      schema_, 0, schema_.num_hash_key_columns(),
      "hash", subdoc_key_.doc_key().hashed_group(), value_map));
  RETURN_NOT_OK(SetYQLPrimaryKeyColumnValues(
      schema_, schema_.num_hash_key_columns(), schema_.num_range_key_columns(),
      "range", subdoc_key_.doc_key().range_group(), value_map));

  // Get the non-key column values of a YQL row.
  vector<SubDocument> values;
  RETURN_NOT_OK(GetValues(projection_, &values));
  for (size_t i = projection_.num_key_columns(); i < projection_.num_columns(); i++) {
    const auto& column_id = projection_.column_id(i);
    const auto yql_type = projection_.column(i).type();
    SubDocument::ToYQLValuePB(values[i - projection_.num_key_columns()], yql_type,
                              &(*value_map)[column_id]);
  }
  return Status::OK();
}

void DocRowwiseIterator::GetIteratorStats(std::vector<IteratorStats>* stats) const {
  // A no-op implementation that adds new IteratorStats objects. This is an attempt to fix
  // linked_list-test with the YQL table type.
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
  DocKey doc_key;
  rocksdb::Slice slice(db_iter_->key());
  RETURN_NOT_OK(doc_key.DecodeFrom(&slice));
  *sub_doc_key = SubDocKey(doc_key, hybrid_time_);
  DVLOG(3) << "Next SubDocKey: " << sub_doc_key->ToString();
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb

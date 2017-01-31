// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/common/iterator.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/gutil/strings/substitute.h"
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
      done_(false) {
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
  // TOOD(bogdan): decide if this is a good enough heuristic for using blooms for scans.
  const bool is_fixed_point_get = spec.lower_bound() == spec.upper_bound();
  db_iter_ = CreateRocksDBIterator(db_, is_fixed_point_get /* use_bloom_on_scan */);

  // Start scan with the lower bound doc key.
  const DocKey& lower_bound = spec.lower_bound();
  ROCKSDB_SEEK(db_iter_.get(), SubDocKey(lower_bound, hybrid_time_).Encode().AsSlice());

  // End scan with the upper bound key bytes.
  has_upper_bound_key_ = true;
  exclusive_upper_bound_key_ = SubDocKey(spec.upper_bound()).AdvanceOutOfDocKeyPrefix();

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
    if (!db_iter_->Valid() ||
        has_upper_bound_key_ && exclusive_upper_bound_key_.CompareTo(db_iter_->key()) <= 0) {
      done_ = true;
      return false;
    }

    status_ = subdoc_key_.FullyDecodeFrom(db_iter_->key());
    if (!status_.ok()) {
      // Defer error reporting to NextBlock.
      return true;
    }

    if (prev_rocksdb_key == db_iter_->key()) {
      // Infinite loop detected, defer error reporting to NextBlock.
      status_ = STATUS_SUBSTITUTE(Corruption,
          "Infinite loop detected at $0", subdoc_key_.ToString());
      return true;
    }
    prev_rocksdb_key = db_iter_->key().ToString();

    if (subdoc_key_.num_subkeys() == 1) {
      // Even without optional init markers (i.e. if we require an object init marker at the top of
      // each row), we may get into the row/column section if we don't have any data at or older
      // than our scan hybrid_time. In this case, we need to skip the row and go to the next row.
      //
      // When we switch to supporting optional init markers for SQL rows, we'll need to add a check
      // to see if we've got any row/column keys within the expected hybrid_time range, or perhaps
      // perform a seek into the row/column section with the specified scan hybrid_time, skip
      // key/value pairs outside of the hybrid_time range (the lower end of that range is based on
      // the delete hybrid_time), and come up with a return value for HasNext. We might also have to
      // go the next row as part of this process.
      ROCKSDB_SEEK(db_iter_.get(), subdoc_key_.AdvanceOutOfSubDoc().AsSlice());
      continue;
    }

    if (subdoc_key_.num_subkeys() > 0) {
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

    Value top_level_value;
    status_ = top_level_value.Decode(db_iter_->value());
    if (!status_.ok()) {
      // Defer error reporting to NextBlock.
      return true;
    }
    const auto value_type = top_level_value.value_type();
    if (value_type == ValueType::kObject) {
      // For now, we hide the init marker if it expires based on table level TTL.
      // TODO: fix this once we support proper TTL semantics.
      bool has_expired = false;
      status_ = HasExpiredTTL(subdoc_key_.hybrid_time(), TableTTL(schema_), hybrid_time_,
                             &has_expired);
      if (!status_.ok()) {
        // Defer error reporting to NextBlock.
        return true;
      }

      if (has_expired) {
        // Skip the row completely and go to the next row.
        ROCKSDB_SEEK(db_iter_.get(), subdoc_key_.AdvanceOutOfSubDoc().AsSlice());
        continue;
      }

      // Success: found a valid document at the given hybrid_time.
      return true;
    }
    if (value_type != ValueType::kTombstone) {
      // Defer error reporting to NextBlock.
      status_ = STATUS_SUBSTITUTE(Corruption,
          "Invalid value type at the top of a document, object or tombstone expected: $0",
          ValueTypeToStr(value_type));
      return true;
    }
    // No document with this key exists at this hybrid_time (it has been deleted). Advance out
    // of this doc key to go to the next document key and repeat.
    ROCKSDB_SEEK(db_iter_.get(), subdoc_key_.AdvanceOutOfDocKeyPrefix().AsSlice());
  }
}

string DocRowwiseIterator::ToString() const {
  return "DocRowwiseIterator";
}

// Get the non-key column values of a YQL row and advance to the next row before return.
Status DocRowwiseIterator::GetValues(const Schema& projection, vector<PrimitiveValue>* values) {

  values->reserve(projection.num_columns() - projection.num_key_columns());

  for (size_t i = projection.num_key_columns(); i < projection.num_columns(); i++) {
    const auto& column_id = projection.column_id(i);
    subdoc_key_.RemoveHybridTime();
    subdoc_key_.AppendSubKeysAndMaybeHybridTime(
        PrimitiveValue(column_id), hybrid_time_);

    const KeyBytes key_for_column = subdoc_key_.Encode();
    ROCKSDB_SEEK(db_iter_.get(), key_for_column.AsSlice());

    bool is_null = true;
    if (db_iter_->Valid() && key_for_column.OnlyDiffersByLastHybridTimeFrom(db_iter_->key())) {
      Value value;
      RETURN_NOT_OK(value.Decode(db_iter_->value()));

      // Check for TTL.
      bool has_expired = false;
      RETURN_NOT_OK(HasExpiredTTL(db_iter_->key(), ComputeTTL(value.ttl(), schema_), hybrid_time_,
                                  &has_expired));

      if (value.primitive_value().value_type() != ValueType::kNull &&
          value.primitive_value().value_type() != ValueType::kTombstone &&
          !has_expired) {
        DOCDB_DEBUG_LOG("Found a non-null value for column #$0: $1", i, value.ToString());
        values->emplace_back(value.primitive_value());
        is_null = false;
      } else {
        DOCDB_DEBUG_LOG("Found a null value for column #$0: $1", i, value.ToString());
      }
    }
    if (is_null) {
      values->emplace_back(PrimitiveValue(ValueType::kNull));
    }

    subdoc_key_.RemoveLastSubKey();
  }

  // Advance to the next column (the next field of the top-level document in our SQL to DocDB
  // mapping).
  ROCKSDB_SEEK(db_iter_.get(), subdoc_key_.AdvanceOutOfSubDoc().AsSlice());

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
    const auto data_type = schema.column(j).type_info()->type();
    const auto& elem = value_map->emplace(column_id, YQLValue(data_type));
    values[i].ToYQLValue(&elem.first->second);
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
  vector<PrimitiveValue> values;
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

Status DocRowwiseIterator::NextRow(const YQLScanSpec& spec, YQLValueMap* value_map) {
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
  vector<PrimitiveValue> values;
  RETURN_NOT_OK(GetValues(projection_, &values));
  for (size_t i = projection_.num_key_columns(); i < projection_.num_columns(); i++) {
    const auto& column_id = projection_.column_id(i);
    const auto data_type = projection_.column(i).type_info()->type();
    const auto& elem = value_map->emplace(column_id, YQLValue(data_type));
    values[i - projection_.num_key_columns()].ToYQLValue(&elem.first->second);
  }

  return Status::OK();
}

Status DocRowwiseIterator::NextBlock(const YQLScanSpec& spec, YQLRowBlock* rowblock) {
  YQLValueMap value_map;
  RETURN_NOT_OK(NextRow(spec, &value_map));

  // Match the row with the where condition before adding to the row block.
  bool match = false;
  RETURN_NOT_OK(spec.Match(value_map, &match));
  if (match) {
    auto& row = rowblock->Extend();
    for (size_t i = 0; i < row.schema().num_columns(); i++) {
      const auto column_id = row.schema().column_id(i);
      const auto it = value_map.find(column_id);
      CHECK(it != value_map.end()) << "Projected column missing: " << column_id;
      row.set_column(i, it->second);
    }
  }

  // Done if we have hit the row count limit. Since we are read one row in each NextBlock() call
  // above, it shouldn't be possible for the row count to jump pass the limit suddenly. But just
  // to play safe in case we read more than 1 row above and didn't
  if (rowblock->row_count() == spec.row_count_limit()) {
    done_ = true;
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

}  // namespace docdb
}  // namespace yb

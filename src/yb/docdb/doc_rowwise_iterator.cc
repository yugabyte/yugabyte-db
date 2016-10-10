// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/common/iterator.h"
#include "yb/docdb/doc_key.h"
#include "yb/gutil/strings/substitute.h"

using std::string;

using strings::Substitute;
using yb::FormatRocksDBSliceAsStr;

namespace yb {
namespace docdb {

namespace {

Status ExpectValueType(ValueType expected_value_type,
                       const ColumnSchema& column,
                       const PrimitiveValue& actual_value) {
  if (expected_value_type == actual_value.value_type()) {
    return Status::OK();
  } else {
    return STATUS(Corruption, Substitute(
        "Expected a internal value type $0 for column $1 of physical SQL type $2, got $3",
        ValueTypeToStr(expected_value_type),
        column.name(),
        column.type_info()->name(),
        ValueTypeToStr(actual_value.value_type())));
  }
}

}  // namespace

DocRowwiseIterator::DocRowwiseIterator(const Schema &projection,
                                       const Schema &schema,
                                       rocksdb::DB *db,
                                       Timestamp timestamp,
                                       yb::util::PendingOperationCounter* pending_op_counter)
    : projection_(projection),
      schema_(schema),
      timestamp_(timestamp),
      db_(db),
      has_upper_bound_key_(false),
      pending_op_(pending_op_counter),
      done_(false) {
}

DocRowwiseIterator::~DocRowwiseIterator() {
}

Status DocRowwiseIterator::Init(ScanSpec *spec) {
  rocksdb::ReadOptions read_options;
  db_iter_.reset(db_->NewIterator(read_options));

  if (spec->lower_bound_key() != nullptr) {
    db_iter_->Seek(
        SubDocKey(KuduToDocKey(*spec->lower_bound_key()), timestamp_).Encode().AsSlice());
  } else {
    db_iter_->SeekToFirst();
  }

  if (spec->exclusive_upper_bound_key() != nullptr) {
    has_upper_bound_key_ = true;
    exclusive_upper_bound_key_ = KuduToDocKey(*spec->exclusive_upper_bound_key()).Encode();
  } else {
    has_upper_bound_key_ = false;
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

  while (true) {
    if (!db_iter_->Valid() ||
        has_upper_bound_key_ && exclusive_upper_bound_key_.CompareTo(db_iter_->key()) <= 0) {
      done_ = true;
      return false;
    }

    status_ = subdoc_key_.DecodeFrom(db_iter_->key());
    if (!status_.ok()) {
      // Defer error reporting to NextBlock.
      return true;
    }

    if (subdoc_key_.num_subkeys() > 0) {
      // Defer error reporting to NextBlock.
      status_ = STATUS(Corruption, "A top-level document key must not have any subkeys");
      return true;
    }

    if (subdoc_key_.doc_gen_ts().CompareTo(timestamp_) > 0) {
      // This document was fully overwritten after the timestamp we are trying to scan at. We have
      // to read the latest version of the document that existed at the specified timestamp instead.
      subdoc_key_.set_doc_gen_ts(timestamp_);
      db_iter_->Seek(subdoc_key_.Encode().AsSlice());
      continue;
    }

    PrimitiveValue top_level_value;
    status_ = top_level_value.DecodeFromValue(db_iter_->value());
    if (!status_.ok()) {
      // Defer error reporting to NextBlock.
      return true;
    }
    auto value_type = top_level_value.value_type();
    if (value_type == ValueType::kObject) {
      // Success: found a valid document at the given timestamp.
      return true;
    }
    if (value_type != ValueType::kTombstone) {
      // Defer error reporting to NextBlock.
      status_ = STATUS(Corruption, Substitute(
          "Invalid value type at the top of a document, object or tombstone expected: $0",
          ValueTypeToStr(value_type)));
      return true;
    }
    // No document with this key exists at this timestamp (it has been deleted). Go to the next
    // document key and repeat.
    db_iter_->Seek(subdoc_key_.doc_key().Encode().Increment().AsSlice());
  }
}

string DocRowwiseIterator::ToString() const {
  return "DocRowwiseIterator";
}

Status DocRowwiseIterator::PrimitiveValueToKudu(
    int column_index,
    const PrimitiveValue& value,
    RowBlockRow* dest_row) {
  const ColumnSchema& col_schema = projection_.column(column_index);
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
    case DataType::BOOL: {
      if (value.value_type() != ValueType::kTrue &&
          value.value_type() != ValueType::kFalse) {
        return STATUS(Corruption,
                      Substitute("Expected true/false, got $0", value.ToString()));
      }
      *(reinterpret_cast<bool*>(dest_ptr)) = value.value_type() == ValueType::kTrue;
      break;
    }
    default:
      return STATUS(IllegalState,
                    Substitute("Unsupported column data type $0", data_type));
  }
  return Status::OK();
}

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

  // Currently we are assuming that we are not using hashed keys, and all keys are stored in the
  // "range group" of the DocKey.
  const auto& range_group = subdoc_key_.doc_key().range_group();
  if (range_group.size() < projection_.num_key_columns()) {
    return STATUS(Corruption,
                  Substitute("Document key has $0 range keys, but at least $1 are expected.",
                             range_group.size(), projection_.num_key_columns()));
  }

  // Key columns come from the key.
  for (size_t i = 0; i < projection_.num_key_columns(); i++) {
    RETURN_NOT_OK(PrimitiveValueToKudu(i, range_group[i], &dst_row));
  }

  for (size_t i = projection_.num_key_columns(); i < projection_.num_columns(); i++) {
    const auto& column_id = projection_.column_id(i);
    subdoc_key_.AppendSubKeysAndTimestamps(
        PrimitiveValue(static_cast<int32_t>(column_id)), timestamp_);

    KeyBytes key_for_column = subdoc_key_.Encode();
    db_iter_->Seek(key_for_column.AsSlice());

    bool is_null = true;
    const bool is_nullable = dst->column_block(i).is_nullable();
    if (db_iter_->Valid() &&
        key_for_column.OnlyDiffersByLastTimestampFrom(db_iter_->key())) {
      // TODO: we are doing a lot of unnecessary buffer copies here.
      PrimitiveValue value;
      RETURN_NOT_OK(value.DecodeFromValue(db_iter_->value()));
      if (value.value_type() != ValueType::kNull &&
          value.value_type() != ValueType::kTombstone) {
        RETURN_NOT_OK(PrimitiveValueToKudu(i, value, &dst_row));
        is_null = false;
      }
    }

    if (is_null && !is_nullable) {
      return STATUS(IllegalState,
                    Substitute("Column $0 is not nullable, but no data is found", i));
    }
    if (is_nullable) {
      dst->column_block(i).SetCellIsNull(0, is_null);
    }

    subdoc_key_.RemoveLastSubKeyAndTimestamp();
  }

  // Seek to the next row (document). We model this by adding the minimum possible timestamp
  // as the document generation timestamp, and further incrementing the key so that even if the
  // minimum timestamp is present, we'll still navigate to the next document key.
  // TODO: reduce unnecessary buffer copies.
  db_iter_->Seek(SubDocKey(subdoc_key_.doc_key(), Timestamp::kMin).Encode().Increment().AsSlice());

  return Status::OK();
}

void DocRowwiseIterator::GetIteratorStats(std::vector<IteratorStats>* stats) const {
  // A no-op implementation that adds new IteratorStats objects. This is an attempt to fix
  // linked_list-test with the YSQL table type.
  for (int i = 0; i < projection_.num_columns(); ++i) {
    stats->emplace_back();
  }
}

}  // namespace docdb
}  // namespace yb

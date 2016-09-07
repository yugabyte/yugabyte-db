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
    return Status::Corruption(Substitute(
        "Expected a internal value type $0 for column $1 of physical SQL type $2, got $3",
        ValueTypeToStr(expected_value_type),
        column.name(),
        column.type_info()->name(),
        ValueTypeToStr(actual_value.value_type())));
  }
}

}

DocRowwiseIterator::DocRowwiseIterator(const Schema &projection,
                                       const Schema &schema,
                                       rocksdb::DB *db,
                                       Timestamp timestamp)
    : projection_(projection),
      schema_(schema),
      timestamp_(timestamp),
      db_(db),
      has_upper_bound_key_(false),
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
      status_ = Status::Corruption("A top-level document key must not have any subkeys");
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
      status_ = Status::Corruption(Substitute(
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
    return Status::NotFound("end of iter");
  }
  if (PREDICT_FALSE(dst->row_capacity() == 0)) {
    return Status::OK();
  }

  dst->Resize(1);
  dst->selection_vector()->SetAllTrue();
  RowBlockRow dst_row(dst->row(0));

  for (size_t i = 0; i < projection_.num_columns(); i++) {
    const auto& column_id = projection_.column_id(i);
    subdoc_key_.AppendSubKeysAndTimestamps(
        PrimitiveValue(static_cast<int32_t>(column_id)), timestamp_);

    KeyBytes key_for_column = subdoc_key_.Encode();
    db_iter_->Seek(key_for_column.AsSlice());

    if (!db_iter_->Valid() ||
        !key_for_column.OnlyDiffersByLastTimestampFrom(db_iter_->key())) {
      dst->column_block(i).SetCellIsNull(0, true);
    } else {
      dst->column_block(i).SetCellIsNull(0, false);

      // TODO: we are doing a lot of unnecessary buffer copies here.
      PrimitiveValue value;
      RETURN_NOT_OK(value.DecodeFromValue(db_iter_->value()));

      DataType column_data_type = projection_.column(i).type_info()->physical_type();

      // We'll put a Slice or a primitive value here.
      void* dest_ptr = dst_row.cell(i).mutable_ptr();

      switch (column_data_type) {
        case DataType::BINARY: {
          Slice cell_copy;
          RETURN_NOT_OK(ExpectValueType(ValueType::kString, projection_.column(i), value));
          if (PREDICT_FALSE(!dst->arena()->RelocateSlice(value.GetStringAsSlice(), &cell_copy))) {
            return Status::IOError("out of memory");
          }
          // Kudu represents variable-size values as Slices pointing to memory allocated from an
          // associated Arena.
          *(reinterpret_cast<Slice*>(dest_ptr)) = cell_copy;
          break;
        }
        case DataType::INT64: {
          RETURN_NOT_OK(ExpectValueType(ValueType::kInt64, projection_.column(i), value));
          // TODO: double-check that we can just assign the 64-bit integer here.
          *(reinterpret_cast<int64_t*>(dst_row.cell(i).mutable_ptr())) = value.GetInt64();
          break;
        }
        default:
          return Status::IllegalState(
              Substitute("Unsupported column data type $0", column_data_type));
      }
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
  // Not implemented yet. We don't print warnings or error out here because this method is actually
  // being called.
}

}
}

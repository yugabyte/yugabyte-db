// Copyright (c) YugaByte, Inc.

#include <yb/gutil/stringprintf.h>
#include "yb/rocksutil/write_batch_formatter.h"
#include "yb/util/bytes_formatter.h"

using std::endl;
using rocksdb::Status;
using rocksdb::Slice;
using rocksdb::SequenceNumber;
using yb::util::FormatBytesAsStr;
using yb::util::QuotesType;

namespace yb {

rocksdb::Status WriteBatchFormatter::PutCF(
    uint32_t column_family_id,
    const rocksdb::Slice& key,
    const rocksdb::Slice& value) {
  StartOutputLine(__FUNCTION__);
  OutputField(key);
  OutputField(value);
  FinishOutputLine();
  return rocksdb::Status::OK();
}

rocksdb::Status WriteBatchFormatter::DeleteCF(
    uint32_t column_family_id,
    const rocksdb::Slice& key) {
  StartOutputLine(__FUNCTION__);
  OutputField(key);
  FinishOutputLine();
  return rocksdb::Status::OK();
}

rocksdb::Status WriteBatchFormatter::SingleDeleteCF(
    uint32_t column_family_id,
    const rocksdb::Slice& key) {
  StartOutputLine(__FUNCTION__);
  OutputField(key);
  FinishOutputLine();
  return rocksdb::Status::OK();
}

rocksdb::Status WriteBatchFormatter::MergeCF(
    uint32_t column_family_id,
    const rocksdb::Slice& key,
    const rocksdb::Slice& value) {
  StartOutputLine(__FUNCTION__);
  OutputField(key);
  OutputField(value);
  FinishOutputLine();
  return rocksdb::Status::OK();
}

Status WriteBatchFormatter::UserOpId(const OpId& op_id) {
  StartOutputLine(__FUNCTION__);
  out_ << op_id;
  FinishOutputLine();
  return Status::OK();
}

void WriteBatchFormatter::StartOutputLine(const char* name) {
  ++update_index_;
  out_ << update_index_ << ". ";
  out_ << name << "(";
  need_separator_ = false;
}

void WriteBatchFormatter::OutputField(const rocksdb::Slice& value) {
  if (need_separator_) {
    out_ << ", ";
  }
  need_separator_ = true,
  out_ << FormatBytesAsStr(value.cdata(), value.size(), QuotesType::kSingleQuotes);
}

void WriteBatchFormatter::FinishOutputLine() {
  out_ << ")" << endl;
}

}  // namespace yb

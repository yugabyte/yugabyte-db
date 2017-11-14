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
  need_separator_ = true;
  if (output_format_ == OutputFormat::kEscaped) {
    out_ << FormatBytesAsStr(value.cdata(), value.size(), QuotesType::kSingleQuotes);
  } else {
    out_ << value.ToDebugHexString();
  }
}

void WriteBatchFormatter::FinishOutputLine() {
  out_ << ")" << endl;
}

}  // namespace yb

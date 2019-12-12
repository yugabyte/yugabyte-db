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
#include "yb/rocksdb/metadata.h"
#include "yb/util/bytes_formatter.h"

using std::endl;
using rocksdb::Status;
using rocksdb::SequenceNumber;

using yb::FormatBytesAsStr;
using yb::QuotesType;
using yb::BinaryOutputFormat;

namespace yb {

rocksdb::Status WriteBatchFormatter::PutCF(
    uint32_t column_family_id,
    const Slice& key,
    const Slice& value) {
  StartOutputLine(__FUNCTION__);
  OutputKey(key);
  OutputValue(value);
  FinishOutputLine();
  return Status::OK();
}

rocksdb::Status WriteBatchFormatter::DeleteCF(
    uint32_t column_family_id,
    const Slice& key) {
  StartOutputLine(__FUNCTION__);
  OutputKey(key);
  FinishOutputLine();
  return Status::OK();
}

rocksdb::Status WriteBatchFormatter::SingleDeleteCF(
    uint32_t column_family_id,
    const Slice& key) {
  StartOutputLine(__FUNCTION__);
  OutputKey(key);
  FinishOutputLine();
  return Status::OK();
}

rocksdb::Status WriteBatchFormatter::MergeCF(
    uint32_t column_family_id,
    const Slice& key,
    const Slice& value) {
  StartOutputLine(__FUNCTION__);
  OutputKey(key);
  OutputValue(value);
  FinishOutputLine();
  return Status::OK();
}

Status WriteBatchFormatter::Frontiers(const rocksdb::UserFrontiers& range) {
  StartOutputLine(__FUNCTION__, /* is_kv_op */ false);
  out_ << range.ToString();
  FinishOutputLine();
  return Status::OK();
}

std::string WriteBatchFormatter::FormatKey(const Slice& key) {
  return FormatSliceAsStr(key, binary_output_format_, QuotesType::kSingleQuotes);
}

std::string WriteBatchFormatter::FormatValue(const Slice& value) {
  return FormatSliceAsStr(value, binary_output_format_, QuotesType::kSingleQuotes);
}

void WriteBatchFormatter::StartOutputLine(const char* name, bool is_kv_op) {
  if (is_kv_op) {
    ++update_index_;
    out_ << update_index_ << ". ";
  }
  out_ << name;
  if (is_kv_op) {
    parentheses_ = true;
    out_ << "(";
  } else {
    // Frontiers.
    out_ << ": ";
  }
  need_separator_ = false;
}

void WriteBatchFormatter::AddSeparatorIfNeeded() {
  if (need_separator_) {
    out_ << ", ";
  }
  need_separator_ = true;
}

void WriteBatchFormatter::OutputKey(const Slice& key) {
  AddSeparatorIfNeeded();
  out_ << FormatKey(key);
}

void WriteBatchFormatter::OutputValue(const Slice& value) {
  AddSeparatorIfNeeded();
  out_ << FormatValue(value);
}

void WriteBatchFormatter::FinishOutputLine() {
  if (parentheses_) {
    out_ << ")";
  }
  out_ << endl;
}


}  // namespace yb

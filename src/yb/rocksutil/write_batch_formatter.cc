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
  AddSeparator();
  OutputValue(key, value);
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
  AddSeparator();
  OutputValue(key, value);
  FinishOutputLine();
  return Status::OK();
}

Status WriteBatchFormatter::Frontiers(const rocksdb::UserFrontiers& range) {
  StartOutputLine(__FUNCTION__, /* is_kv= */ false);
  out_ << range.ToString() << endl;
  return Status::OK();
}

std::string WriteBatchFormatter::FormatKey(const Slice& key) {
  return FormatSliceAsStr(key, binary_output_format_, QuotesType::kSingleQuotes);
}

std::string WriteBatchFormatter::FormatValue(const Slice& key, const Slice& value) {
  // We are ignoring the key here, but in subclasses we can use it to decide how to decode value.
  return FormatSliceAsStr(value, binary_output_format_, QuotesType::kSingleQuotes);
}

void WriteBatchFormatter::StartOutputLine(const char* function_name, bool is_kv) {
  out_ << line_prefix_;
  if (is_kv) {
    ++kv_index_;
    out_ << kv_index_ << ". ";
  }
  out_ << function_name;
  switch (output_format_) {
    case WriteBatchOutputFormat::kParentheses: out_ << "("; break;
    case WriteBatchOutputFormat::kArrow: out_ << ": "; break;
  }
}

void WriteBatchFormatter::AddSeparator() {
  switch (output_format_) {
    case WriteBatchOutputFormat::kParentheses: out_ << ", "; break;
    case WriteBatchOutputFormat::kArrow: out_ << " => "; break;
  }
}

void WriteBatchFormatter::OutputKey(const Slice& key) {
  out_ << FormatKey(key);
}

void WriteBatchFormatter::OutputValue(const Slice& key, const Slice& value) {
  out_ << FormatValue(key, value);
}

void WriteBatchFormatter::FinishOutputLine() {
  if (output_format_ == WriteBatchOutputFormat::kParentheses) {
    out_ << ")";
  }
  out_ << endl;
}

}  // namespace yb

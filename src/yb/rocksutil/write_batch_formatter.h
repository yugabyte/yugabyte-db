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

#ifndef YB_ROCKSUTIL_WRITE_BATCH_FORMATTER_H
#define YB_ROCKSUTIL_WRITE_BATCH_FORMATTER_H

#include <sstream>

#include "yb/rocksdb/status.h"
#include "yb/rocksdb/types.h"
#include "yb/rocksdb/write_batch.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"
#include "yb/util/result.h"

namespace yb {

YB_DEFINE_ENUM(WriteBatchFieldKind, (kKey)(kValue));

// Produces a human-readable representation of the given RocksDB WriteBatch, e.g.:
// <pre>
// 1. PutCF('key1', 'value1')
// 2. PutCF('key2', 'value2')
// </pre>
class WriteBatchFormatter : public rocksdb::WriteBatch::Handler {
 public:
  explicit WriteBatchFormatter(
      BinaryOutputFormat binary_output_format = BinaryOutputFormat::kEscaped)
      : binary_output_format_(binary_output_format) {}

  virtual CHECKED_STATUS PutCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key,
      const rocksdb::Slice& value) override;

  virtual CHECKED_STATUS DeleteCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key) override;

  virtual CHECKED_STATUS SingleDeleteCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key) override;

  virtual CHECKED_STATUS MergeCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key,
      const rocksdb::Slice& value) override;

  CHECKED_STATUS Frontiers(const rocksdb::UserFrontiers& range) override;

  std::string str() { return out_.str(); }

 protected:
  virtual std::string FormatKey(const Slice& key);
  virtual std::string FormatValue(const Slice& value);

 private:
  void StartOutputLine(const char* name, bool is_kv_op = true);
  void AddSeparatorIfNeeded();
  void OutputKey(const Slice& key);
  void OutputValue(const Slice& value);
  void FinishOutputLine();

  BinaryOutputFormat binary_output_format_;
  bool need_separator_ = false;
  std::stringstream out_;
  int update_index_ = 0;
  bool parentheses_ = false;
};

} // namespace yb

#endif // YB_ROCKSUTIL_WRITE_BATCH_FORMATTER_H

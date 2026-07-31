// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <stdint.h>
#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <sstream>
#include <string>

#include "yb/rocksdb/write_batch.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"
#include "yb/util/slice.h"
#include "yb/util/slice_parts.h"

namespace yb {

YB_DEFINE_ENUM(WriteBatchFieldKind, (kKey)(kValue));
YB_DEFINE_ENUM(WriteBatchOutputFormat, (kParentheses)(kArrow));

// Produces a human-readable representation of the given RocksDB WriteBatch, e.g.:
// <pre>
// 1. PutCF('key1', 'value1')
// 2. PutCF('key2', 'value2')
// </pre>
class WriteBatchFormatter : public rocksdb::WriteBatch::Handler {
 public:
  explicit WriteBatchFormatter(
      BinaryOutputFormat binary_output_format = BinaryOutputFormat::kEscaped,
      WriteBatchOutputFormat output_format = WriteBatchOutputFormat::kParentheses,
      std::string line_prefix = std::string())
      : binary_output_format_(binary_output_format),
        output_format_(output_format),
        line_prefix_(line_prefix) {}

  virtual Status PutCF(
      uint32_t column_family_id,
      const rocksdb::SliceParts& key,
      const rocksdb::SliceParts& value) override;

  virtual Status DeleteCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key) override;

  virtual Status SingleDeleteCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key) override;

  virtual Status MergeCF(
      uint32_t column_family_id,
      const rocksdb::Slice& key,
      const rocksdb::Slice& value) override;

  Status Frontiers(const yb::storage::UserFrontiers& range) override;

  std::string str() { return out_.str(); }

  void SetLinePrefix(const std::string& line_prefix) {
    line_prefix_ = line_prefix;
  }

  int Count() { return kv_index_; }

 protected:
  virtual std::string FormatKey(const Slice& key);

  // When formatting a value, it is sometimes necessary to know the key as well.
  virtual std::string FormatValue(const Slice& key, const Slice& value);

 private:
  void StartOutputLine(const char* function_name, bool is_kv = true);
  void AddSeparator();
  void OutputKey(const Slice& key);
  void OutputValue(const Slice& key, const Slice& value);
  void FinishOutputLine();

  BinaryOutputFormat binary_output_format_;
  WriteBatchOutputFormat output_format_;
  std::string line_prefix_;

  std::stringstream out_;
  int kv_index_ = 0;
};

} // namespace yb

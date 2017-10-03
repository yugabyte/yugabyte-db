// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef KUDU_TPCH_LINE_ITEM_TSV_IMPORTER_H
#define KUDU_TPCH_LINE_ITEM_TSV_IMPORTER_H

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include "kudu/benchmarks/tpch/tpch-schemas.h"
#include "kudu/common/partial_row.h"
#include "kudu/gutil/strings/numbers.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/gutil/strings/stringpiece.h"
#include "kudu/util/status.h"

namespace kudu {

static const char* const kPipeSeparator = "|";

// Utility class used to parse the lineitem tsv file
class LineItemTsvImporter {
 public:
  explicit LineItemTsvImporter(const string &path) : in_(path.c_str()),
    updated_(false) {
    CHECK(in_.is_open()) << "not able to open input file: " << path;
  }

  bool HasNextLine() {
    if (!updated_) {
      done_ = !getline(in_, line_);
      updated_ = true;
    }
    return !done_;
  }

  // Fills the row builder with a single line item from the file.
  // It returns 0 if it's done or the order number if it got a line
  int GetNextLine(KuduPartialRow* row) {
    if (!HasNextLine()) return 0;
    columns_.clear();

    // grab all the columns_ individually
    // Note that columns_ refers, and does not copy, the data in line_
    columns_ = strings::Split(line_, kPipeSeparator);

    // The row copies all indirect data from columns_. This must be done
    // because callers expect to retrieve lines repeatedly before flushing
    // the accumulated rows in a batch.
    int i = 0;
    int order_number = ConvertToInt64AndPopulate(columns_[i++], row, tpch::kOrderKeyColIdx);
    ConvertToIntAndPopulate(columns_[i++], row, tpch::kPartKeyColIdx);
    ConvertToIntAndPopulate(columns_[i++], row, tpch::kSuppKeyColIdx);
    ConvertToIntAndPopulate(columns_[i++], row, tpch::kLineNumberColIdx);
    ConvertToIntAndPopulate(columns_[i++], row, tpch::kQuantityColIdx);
    ConvertToDoubleAndPopulate(columns_[i++], row, tpch::kExtendedPriceColIdx);
    ConvertToDoubleAndPopulate(columns_[i++], row, tpch::kDiscountColIdx);
    ConvertToDoubleAndPopulate(columns_[i++], row, tpch::kTaxColIdx);
    CHECK_OK(row->SetStringCopy(tpch::kReturnFlagColIdx, columns_[i++]));
    CHECK_OK(row->SetStringCopy(tpch::kLineStatusColIdx, columns_[i++]));
    CHECK_OK(row->SetStringCopy(tpch::kShipDateColIdx, columns_[i++]));
    CHECK_OK(row->SetStringCopy(tpch::kCommitDateColIdx, columns_[i++]));
    CHECK_OK(row->SetStringCopy(tpch::kReceiptDateColIdx, columns_[i++]));
    CHECK_OK(row->SetStringCopy(tpch::kShipInstructColIdx, columns_[i++]));
    CHECK_OK(row->SetStringCopy(tpch::kShipModeColIdx, columns_[i++]));
    CHECK_OK(row->SetStringCopy(tpch::kCommentColIdx, columns_[i++]));

    updated_ = false;

    return order_number;
  }

 private:
  int ConvertToInt64AndPopulate(const StringPiece &chars, KuduPartialRow* row,
                                int col_idx) {
    // TODO: extra copy here, since we don't have a way to parse StringPiece
    // into ints.
    chars.CopyToString(&tmp_);
    int64_t number;
    bool ok_parse = safe_strto64(tmp_.c_str(), &number);
    CHECK(ok_parse) << "Bad integer in column " << col_idx
                    << ": '" << tmp_ << "'";
    CHECK_OK(row->SetInt64(col_idx, number));
    return number;
  }

  int ConvertToIntAndPopulate(const StringPiece &chars, KuduPartialRow* row,
                              int col_idx) {
    // TODO: extra copy here, since we don't have a way to parse StringPiece
    // into ints.
    chars.CopyToString(&tmp_);
    int number;
    bool ok_parse = SimpleAtoi(tmp_.c_str(), &number);
    CHECK(ok_parse) << "Bad integer in column " << col_idx
                    << ": '" << tmp_ << "'";
    CHECK_OK(row->SetInt32(col_idx, number));
    return number;
  }

  void ConvertToDoubleAndPopulate(const StringPiece &chars, KuduPartialRow* row,
                                  int col_idx) {
    // TODO: extra copy here, since we don't have a way to parse StringPiece
    // into ints.
    chars.CopyToString(&tmp_);
    char *error = NULL;
    errno = 0;
    const char *cstr = tmp_.c_str();
    double number = strtod(cstr, &error);
    CHECK(errno == 0 &&  // overflow/underflow happened
          error != cstr) << "Bad double in column " << col_idx
                         << ": '" << tmp_ << "': errno=" << errno;
    CHECK_OK(row->SetDouble(col_idx, number));
  }
  std::ifstream in_;
  vector<StringPiece> columns_;
  string line_, tmp_;
  bool updated_, done_;
};
} // namespace kudu
#endif

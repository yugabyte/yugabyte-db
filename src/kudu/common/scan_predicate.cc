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

#include "kudu/common/scan_predicate.h"

#include <string>

#include "kudu/common/rowblock.h"
#include "kudu/common/types.h"
#include "kudu/util/bitmap.h"

namespace kudu {

using std::string;

ValueRange::ValueRange(const TypeInfo* type,
                       const void* lower_bound,
                       const void* upper_bound)
  : type_info_(type),
    lower_bound_(lower_bound),
    upper_bound_(upper_bound) {
  CHECK(has_lower_bound() || has_upper_bound())
    << "range predicate has no bounds";
}

bool ValueRange::IsEquality() const {
  if (has_lower_bound() && has_upper_bound()) {
    return type_info_->Compare(upper_bound(), lower_bound()) == 0;
  }
  return false;
}

bool ValueRange::ContainsCell(const void* cell) const {
  if (has_lower_bound() && type_info_->Compare(cell, lower_bound()) < 0) {
    return false;
  }
  if (has_upper_bound() && type_info_->Compare(cell, upper_bound()) > 0) {
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////

ColumnRangePredicate::ColumnRangePredicate(ColumnSchema col,
                                           const void* lower_bound,
                                           const void* upper_bound)
    : col_(std::move(col)),
      range_(col_.type_info(), lower_bound, upper_bound) {}

void ColumnRangePredicate::Evaluate(RowBlock* block, SelectionVector* vec) const {
  int col_idx = block->schema().find_column(col_.name());
  CHECK_GE(col_idx, 0) << "bad col: " << col_.ToString();

  ColumnBlock cblock(block->column_block(col_idx, block->nrows()));

  // TODO: this is all rather slow, could probably push down all the way
  // to the TypeInfo so we only make one virtual call, or use codegen.
  // Not concerned for now -- plan of record is to eventually embed Impala
  // expression evaluation somewhere here, so this is just a stub.
  if (cblock.is_nullable()) {
    for (size_t i = 0; i < block->nrows(); i++) {
      if (!vec->IsRowSelected(i)) continue;
      const void *cell = cblock.nullable_cell_ptr(i);
      if (cell == nullptr || !range_.ContainsCell(cell)) {
        BitmapClear(vec->mutable_bitmap(), i);
      }
    }
  } else {
    for (size_t i = 0; i < block->nrows(); i++) {
      if (!vec->IsRowSelected(i)) continue;
      const void *cell = cblock.cell_ptr(i);
      if (!range_.ContainsCell(cell)) {
        BitmapClear(vec->mutable_bitmap(), i);
      }
    }
  }
}

string ColumnRangePredicate::ToString() const {
  if (range_.has_lower_bound() && range_.has_upper_bound()) {
    return StringPrintf("(`%s` BETWEEN %s AND %s)", col_.name().c_str(),
                        col_.Stringify(range_.lower_bound()).c_str(),
                        col_.Stringify(range_.upper_bound()).c_str());
  } else if (range_.has_lower_bound()) {
    return StringPrintf("(`%s` >= %s)", col_.name().c_str(),
                        col_.Stringify(range_.lower_bound()).c_str());
  } else if (range_.has_upper_bound()) {
    return StringPrintf("(`%s` <= %s)", col_.name().c_str(),
                        col_.Stringify(range_.upper_bound()).c_str());
  } else {
    LOG(FATAL) << "Cannot reach here";
    return string("Does not reach here");
  }
}

} // namespace kudu

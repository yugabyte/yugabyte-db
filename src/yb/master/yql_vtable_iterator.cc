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

#include "yb/master/yql_vtable_iterator.h"
#include <iterator>

#include "yb/qlexpr/ql_expr.h"
#include "yb/qlexpr/ql_rowblock.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"

#include "yb/util/result.h"

namespace yb {
namespace master {

YQLVTableIterator::YQLVTableIterator(
    std::shared_ptr<qlexpr::QLRowBlock> vtable,
    const google::protobuf::RepeatedPtrField<QLExpressionPB>& hashed_column_values)
    : vtable_(std::move(vtable)), hashed_column_values_(hashed_column_values) {
  Advance(false /* increment */);
}

Result<bool> YQLVTableIterator::PgFetchNext(dockv::PgTableRow* table_row) {
  return STATUS_FORMAT(NotSupported, "$0 on system table", __func__);
}

Result<bool> YQLVTableIterator::DoFetchNext(
    qlexpr::QLTableRow* table_row,
    const dockv::ReaderProjection* projection,
    qlexpr::QLTableRow* static_row,
    const dockv::ReaderProjection* static_projection) {
  if (vtable_index_ >= vtable_->row_count()) {
    return false;
  }

  // TODO: return columns in projection only.
  auto& row = vtable_->row(vtable_index_);
  for (size_t i = 0; i < row.schema().num_columns(); i++) {
    table_row->AllocColumn(row.schema().column_id(i), down_cast<const QLValue&>(row.column(i)));
  }
  Advance(true /* increment */);
  return true;
}

std::string YQLVTableIterator::ToString() const {
  return "YQLVTableIterator";
}

// Advances iterator to next valid row, filtering columns using hashed_column_values_.
void YQLVTableIterator::Advance(bool increment) {
  if (increment) {
    ++vtable_index_;
  }
  int num_hashed_columns = hashed_column_values_.size();
  if (num_hashed_columns == 0) {
    return;
  }
  while (vtable_index_ < vtable_->row_count()) {
    auto& row = vtable_->row(vtable_index_);
    bool bad = false;
    for (int idx = 0; idx != num_hashed_columns; ++idx) {
      if (hashed_column_values_[idx].value() != row.column(idx)) {
        bad = true;
        break;
      }
    }
    if (!bad) {
      break;
    }
    ++vtable_index_;
  }
}

YQLVTableIterator::~YQLVTableIterator() {
}

Result<HybridTime> YQLVTableIterator::RestartReadHt() {
  return HybridTime::kInvalid;
}

}  // namespace master
}  // namespace yb

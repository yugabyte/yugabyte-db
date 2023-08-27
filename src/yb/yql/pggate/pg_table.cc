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

#include "yb/yql/pggate/pg_table.h"

#include "yb/util/result.h"

#include "yb/yql/pggate/pg_tabledesc.h"

namespace yb::pggate {
namespace {

[[nodiscard]] PgColumn& GetColumnByIndex(std::vector<PgColumn>* columns, size_t index) {
  CHECK_LT(index + 1, columns->size());
  return (*columns)[index];
}

} // namespace

PgTable::PgTable(const PgTableDescPtr& desc)
    : desc_(desc), columns_(std::make_shared<std::vector<PgColumn>>()) {
  if (!desc_) {
    return;
  }
  size_t num_columns = desc_->num_columns();
  columns_->reserve(num_columns + 1);
  for (size_t i = 0; i != num_columns; ++i) {
    columns_->emplace_back(desc->schema(), i);
  }
  columns_->emplace_back(desc_->schema(), num_columns);
}

Result<PgColumn&> PgTable::ColumnForAttr(int attr_num) {
  return (*columns_)[VERIFY_RESULT(desc_->FindColumn(attr_num))];
}

PgColumn& PgTable::ColumnForIndex(size_t index) {
  return GetColumnByIndex(columns_.get(), index);
}

const PgColumn& PgTable::ColumnForIndex(size_t index) const {
  return GetColumnByIndex(columns_.get(), index);
}

}  // namespace yb::pggate

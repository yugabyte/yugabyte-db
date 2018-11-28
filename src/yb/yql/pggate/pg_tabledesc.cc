//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_tabledesc.h"

namespace yb {
namespace pggate {

PgTableDesc::PgTableDesc(std::shared_ptr<client::YBTable> pg_table) : table_(pg_table) {
  const auto& schema = pg_table->schema();
  const int num_columns = schema.num_columns();
  columns_.resize(num_columns);
  for (int idx = 0; idx < num_columns; idx++) {
    // Find the column descriptor.
    const auto& col = schema.Column(idx);

    // TODO(neil) Considering index columns by attr_num instead of ID.
    ColumnDesc *desc = columns_[idx].desc();
    desc->Init(idx,
               schema.ColumnId(idx),
               col.name(),
               idx < schema.num_hash_key_columns(),
               idx < schema.num_key_columns(),
               col.order() /* attr_num */,
               col.type(),
               client::YBColumnSchema::ToInternalDataType(col.type()));
  }

  // Create virtual columns.
  column_yb_ctid_.Init(PgSystemAttrNum::kYBTupleId);
}

Result<PgColumn *> PgTableDesc::FindColumn(int attr_num) {
  // Find virtual columns.
  if (attr_num == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
    return &column_yb_ctid_;
  }

  // Find physical column.
  for (auto& col : columns_) {
    if (col.attr_num() == attr_num) {
      return &col;
    }
  }

  return STATUS_SUBSTITUTE(InvalidArgument, "Invalid column number $0", attr_num);
}

CHECKED_STATUS PgTableDesc::GetColumnInfo(int16_t attr_number,
                                          bool *is_primary,
                                          bool *is_hash) const {
  for (int i = 0; i < num_key_columns(); i++) {
    if (columns_[i].attr_num() == attr_number) {
      *is_primary = true;
      *is_hash = i < num_hash_key_columns();
      return Status::OK();
    }
  }
  *is_primary = false;
  *is_hash = false;
  return Status::OK();
}

}  // namespace pggate
}  // namespace yb

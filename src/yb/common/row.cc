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

#include "yb/common/row.h"

namespace yb {

const TypeInfo* SimpleConstCell::typeinfo() const {
  return col_schema_->type_info();
}

size_t SimpleConstCell::size() const {
  return col_schema_->type_info()->size;
}

bool SimpleConstCell::is_nullable() const {
  return col_schema_->is_nullable();
}

RowProjector::RowProjector(const Schema* base_schema, const Schema* projection)
  : base_schema_(base_schema), projection_(projection),
    is_identity_(base_schema->Equals(*projection)) {
}

// Initialize the projection mapping with the specified base_schema and projection
Status RowProjector::Init() {
  return projection_->GetProjectionMapping(*base_schema_, this);
}

Status RowProjector::Reset(const Schema* base_schema, const Schema* projection) {
  base_schema_ = base_schema;
  projection_ = projection;
  base_cols_mapping_.clear();
  adapter_cols_mapping_.clear();
  is_identity_ = base_schema->Equals(*projection);
  return Init();
}

Status RowProjector::ProjectExtraColumn(size_t proj_col_idx) {
  return STATUS(InvalidArgument,
    "The column '" + projection_->column(proj_col_idx).name() +
    "' does not exist in the projection, and it does not have a nullable type");
}

}  // namespace yb

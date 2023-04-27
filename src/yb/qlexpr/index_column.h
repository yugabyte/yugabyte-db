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

#pragma once

#include "yb/common/column_id.h"
#include "yb/common/common.pb.h"

namespace yb::qlexpr {

// Index column mapping.
struct IndexColumn {
  ColumnId column_id;         // Column id in the index table.
  std::string column_name;    // Column name in the index table - colexpr.MangledName().
  ColumnId indexed_column_id; // Corresponding column id in indexed table.
  QLExpressionPB colexpr;     // Index expression.

  explicit IndexColumn(const IndexInfoPB::IndexColumnPB& pb);
  IndexColumn() {}

  void ToPB(IndexInfoPB::IndexColumnPB* pb) const;

  std::string ToString() const;
};

}  // namespace yb::qlexpr

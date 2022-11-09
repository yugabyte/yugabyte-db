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
//
// Logic to identify if a YCQL expr => another YCQL expr (i.e., logical implication).
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/cql/ql/ptree/column_arg.h"

#include "yb/common/common_fwd.h"

namespace yb {
namespace ql {

Result<bool> WhereClauseImpliesPred(const MCList<ColumnOp> &col_ops,
                                    const QLExpressionPB& predicate, int *predicate_len,
                                    MCUnorderedMap<int32, uint16> *column_ref_cnts);

Result<bool> OpInExpr(const QLExpressionPB& predicate, const ColumnOp& col_op);

}  // namespace ql
}  // namespace yb

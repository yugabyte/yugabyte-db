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

#include "yb/yql/cql/ql/ptree/column_arg.h"

#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_bcall.h"

using std::string;

namespace yb {
namespace ql {

string JsonColumnOp::IndexExprToColumnName() const {
  string index_column_name = desc_->MangledName();
  for (const PTExpr::SharedPtr &arg : args_->node_list()) {
    index_column_name += arg->MangledName();
  }
  return index_column_name;
}

void FuncOp::Init(const PTExprPtr& value_expr,
                  const PTExprPtr& func_expr,
                  yb::QLOperator yb_op) {
  value_expr_ = value_expr;
  func_expr_ = std::dynamic_pointer_cast<PTBcall>(func_expr);
  yb_op_ = yb_op;
}

const char* QLOperatorAsString(QLOperator ql_op) {
  switch (ql_op) {
    case QL_OP_AND:
      return "AND";
    case QL_OP_EQUAL:
      return "=";
    case QL_OP_LESS_THAN:
      return "<";
    case QL_OP_LESS_THAN_EQUAL:
      return "<=";
    case QL_OP_GREATER_THAN:
      return ">";
    case QL_OP_GREATER_THAN_EQUAL:
      return ">=";
    case QL_OP_IN:
      return "IN";
    case QL_OP_NOT_IN:
      return "NOT IN";
    case QL_OP_NOT_EQUAL:
      return "!=";
    default:
      return "";
  }
}

void ColumnOp::OutputTo(std::ostream* out) const {
  auto& s = *out;
  s << "(" << desc()->name() << " " << QLOperatorAsString(yb_op());

  if (expr()->expr_op() != ExprOperator::kBindVar && expr()->ql_type_id() == DataType::STRING) {
    s << " '" << expr()->QLName() << "')";
  } else {
    s << " " << expr()->QLName() << ")";
  }
}

}  // namespace ql
}  // namespace yb

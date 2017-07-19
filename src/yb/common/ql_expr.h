//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines the ResultSet that YQL database returns to a query request.
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_QL_EXPR_H_
#define YB_COMMON_QL_EXPR_H_

#include "yb/common/ql_value.h"
#include "yb/common/ql_rowblock.h"
#include "yb/common/schema.h"

namespace yb {

// TODO(neil)
// - This should be maping directly from "int32_t" to QLValue.
//   using ValueMap = std::unordered_map<int32_t, const QLValuePB>;
// - We should use shared_ptr for this map as we might multi-threading the execution process.

// DocDB is using this map, so its code has to be updated before we can change this.
// Slowing down our execution by calling constructor each time is not desired.
using ValueMap = std::unordered_map<int32_t, const QLValuePB>;

class QLExprExecutor {
 public:
  // Constructor.
  // TODO(neil) Investigate to see if constructor should save some parameters as members since
  // we pass the same parameter over & over again when calling function recursively.
  QLExprExecutor() { }
  virtual ~QLExprExecutor() { }

  // Evaluate the given QLExpressionPB.
  CHECKED_STATUS EvalExpr(const QLExpressionPB& ql_expr,
                          const QLTableRow& column_map,
                          QLValueWithPB *result);

  // Evaluate call to regular builtin operator.
  virtual CHECKED_STATUS EvalBFCall(const QLBCallPB& ql_expr,
                                    const QLTableRow& column_map,
                                    QLValueWithPB *result);

  // Evaluate call to tablet-server builtin operator.
  virtual CHECKED_STATUS EvalTSCall(const QLBCallPB& ql_expr,
                                    const QLTableRow& column_map,
                                    QLValueWithPB *result);

  // Evaluate subscripting operator for indexing a collection such as column[key].
  virtual CHECKED_STATUS EvalSubscriptedColumn(const QLSubscriptedColPB& ql_expr,
                                               const QLTableRow& column_map,
                                               QLValueWithPB *result);

  // Evaluate a boolean condition for the given row.
  virtual CHECKED_STATUS EvalCondition(const QLConditionPB& condition,
                                       const QLTableRow& column_map,
                                       bool* result);
  virtual CHECKED_STATUS EvalCondition(const QLConditionPB& condition,
                                       const QLTableRow& column_map,
                                       QLValueWithPB *result);
};

} // namespace yb

#endif // YB_COMMON_QL_EXPR_H_

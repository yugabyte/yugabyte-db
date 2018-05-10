//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines and executes expression-related operations in DocDB.
//--------------------------------------------------------------------------------------------------

#ifndef YB_DOCDB_DOC_EXPR_H_
#define YB_DOCDB_DOC_EXPR_H_

#include "yb/common/ql_value.h"
#include "yb/common/ql_expr.h"
#include "yb/common/schema.h"

namespace yb {
namespace docdb {

class DocExprExecutor : public QLExprExecutor {
 public:
  // Public types.
  typedef std::shared_ptr<DocExprExecutor> SharedPtr;
  typedef std::shared_ptr<const DocExprExecutor> SharedPtrConst;

  // Constructor.
  // TODO(neil) Investigate to see if constructor should take 'table_row' and bind_map.
  DocExprExecutor() { }
  virtual ~DocExprExecutor() { }

  // Evaluate call to tablet-server builtin operator.
  virtual CHECKED_STATUS EvalTSCall(const QLBCallPB& ql_expr,
                                    const QLTableRow& table_row,
                                    QLValue *result) override;

  // Evaluate aggregate functions for each row.
  CHECKED_STATUS EvalCount(QLValue *aggr_count);
  CHECKED_STATUS EvalSum(const QLValue& val, QLValue *aggr_sum);
  CHECKED_STATUS EvalMax(const QLValue& val, QLValue *aggr_max);
  CHECKED_STATUS EvalMin(const QLValue& val, QLValue *aggr_min);
  CHECKED_STATUS EvalAvg(const QLValue& val, QLValue *aggr_avg);

 protected:
  vector<QLValue> aggr_result_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_DOC_EXPR_H_

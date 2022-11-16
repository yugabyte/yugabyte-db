//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines and executes expression-related operations in DocDB.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/common/ql_expr.h"

namespace yb {
namespace docdb {

class DocExprExecutor : public QLExprExecutor {
 public:
  // Public types.
  typedef std::shared_ptr<DocExprExecutor> SharedPtr;
  typedef std::shared_ptr<const DocExprExecutor> SharedPtrConst;

  // Constructor.
  // TODO(neil) Investigate to see if constructor should take 'table_row' and bind_map.
  DocExprExecutor();
  virtual ~DocExprExecutor();

  // Evaluate column reference.
  Status EvalColumnRef(ColumnIdRep col_id,
                       const QLTableRow* table_row,
                       QLExprResultWriter result_writer) override;

  // Evaluate call to tablet-server builtin operator.
  Status EvalTSCall(const QLBCallPB& ql_expr,
                    const QLTableRow& table_row,
                    QLValuePB *result,
                    const Schema *schema = nullptr) override;

  Status EvalTSCall(const PgsqlBCallPB& ql_expr,
                    const QLTableRow& table_row,
                    QLValuePB *result,
                    const Schema *schema) override;

  Status EvalTSCall(const LWPgsqlBCallPB& ql_expr,
                    const QLTableRow& table_row,
                    LWQLValuePB *result,
                    const Schema *schema) override;

 protected:
  // Evaluate aggregate functions for each row.
  template <class Val>
  Status EvalCount(Val *aggr_count);

  template <class Val>
  Status EvalSum(const Val& val, Val *aggr_sum);

  template <class Expr, class Val, class Extractor>
  Status EvalSumInt(
      const Expr& expr, const QLTableRow& table_row, Val *aggr_sum, const Extractor& extractor);

  template <class Expr, class Val, class Extractor, class Setter>
  Status EvalSumReal(
      const Expr& expr, const QLTableRow& table_row, Val *aggr_sum,
      const Extractor& extractor, const Setter& setter);

  template <class Val>
  Status EvalMax(const Val& val, Val *aggr_max);

  template <class Val>
  Status EvalMin(const Val& val, Val *aggr_min);

  template <class Val>
  Status EvalAvg(const Val& val, Val *aggr_avg);

  Status EvalParametricToJson(const QLExpressionPB& operand,
                              const QLTableRow& table_row,
                              QLValuePB *result,
                              const Schema *schema);

  template <class Expr, class Val>
  Status DoEvalTSCall(const Expr& ql_expr,
                      const QLTableRow& table_row,
                      Val *result,
                      const Schema *schema);

  virtual Status GetTupleId(QLValuePB *result) const;
  std::vector<QLExprResult> aggr_result_;
};

} // namespace docdb
} // namespace yb

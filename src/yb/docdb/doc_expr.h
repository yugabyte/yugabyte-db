//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines and executes expression-related operations in DocDB.
//--------------------------------------------------------------------------------------------------

#ifndef YB_DOCDB_DOC_EXPR_H_
#define YB_DOCDB_DOC_EXPR_H_

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
  CHECKED_STATUS EvalColumnRef(ColumnIdRep col_id,
                               const QLTableRow* table_row,
                               QLExprResultWriter result_writer) override;

  // Evaluate call to tablet-server builtin operator.
  CHECKED_STATUS EvalTSCall(const QLBCallPB& ql_expr,
                            const QLTableRow& table_row,
                            QLValue *result,
                            const Schema *schema = nullptr) override;

  CHECKED_STATUS EvalTSCall(const PgsqlBCallPB& ql_expr,
                            const QLTableRow& table_row,
                            QLValue *result,
                            const Schema *schema) override;

  // Evaluate aggregate functions for each row.
  CHECKED_STATUS EvalCount(QLValue *aggr_count);
  CHECKED_STATUS EvalSum(const QLValuePB& val, QLValue *aggr_sum);
  template <class Extractor>
  CHECKED_STATUS EvalSumInt(
      const PgsqlExpressionPB& expr, const QLTableRow& table_row, QLValue *aggr_sum,
      const Extractor& extractor);
  template <class Extractor, class Setter>
  CHECKED_STATUS EvalSumReal(
      const PgsqlExpressionPB& expr, const QLTableRow& table_row, QLValue *aggr_sum,
      const Extractor& extractor, const Setter& setter);
  CHECKED_STATUS EvalMax(const QLValuePB& val, QLValue *aggr_max);
  CHECKED_STATUS EvalMin(const QLValuePB& val, QLValue *aggr_min);
  CHECKED_STATUS EvalAvg(const QLValuePB& val, QLValue *aggr_avg);

  CHECKED_STATUS EvalParametricToJson(const QLExpressionPB& operand,
                                      const QLTableRow& table_row,
                                      QLValue *result,
                                      const Schema *schema);

 protected:
  virtual CHECKED_STATUS GetTupleId(QLValue *result) const;
  std::vector<QLExprResult> aggr_result_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_DOC_EXPR_H_

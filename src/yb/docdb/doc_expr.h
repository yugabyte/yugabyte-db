//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines and executes expression-related operations in DocDB.
//--------------------------------------------------------------------------------------------------

#ifndef YB_DOCDB_DOC_EXPR_H_
#define YB_DOCDB_DOC_EXPR_H_

#include "yb/common/ql_expr.h"
#include "yb/common/schema.h"
#include "yb/docdb/key_bytes.h"

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
  virtual CHECKED_STATUS EvalColumnRef(ColumnIdRep col_id,
                                       const QLTableRow::SharedPtrConst& table_row,
                                       QLValue *result) override;

  // Evaluate call to tablet-server builtin operator.
  virtual CHECKED_STATUS EvalTSCall(const QLBCallPB& ql_expr,
                                    const QLTableRow& table_row,
                                    QLValue *result,
                                    const Schema *schema = nullptr) override;

  virtual CHECKED_STATUS EvalTSCall(const PgsqlBCallPB& ql_expr,
                                    const QLTableRow::SharedPtrConst& table_row,
                                    QLValue *result) override;

  // Evaluate aggregate functions for each row.
  CHECKED_STATUS EvalCount(QLValue *aggr_count);
  CHECKED_STATUS EvalSum(const QLValue& val, QLValue *aggr_sum);
  CHECKED_STATUS EvalSumInt8(const QLValue& val, QLValue *aggr_sum);
  CHECKED_STATUS EvalSumInt16(const QLValue& val, QLValue *aggr_sum);
  CHECKED_STATUS EvalSumInt32(const QLValue& val, QLValue *aggr_sum);
  CHECKED_STATUS EvalSumInt64(const QLValue& val, QLValue *aggr_sum);
  CHECKED_STATUS EvalSumFloat(const QLValue& val, QLValue *aggr_sum);
  CHECKED_STATUS EvalSumDouble(const QLValue& val, QLValue *aggr_sum);
  CHECKED_STATUS EvalMax(const QLValue& val, QLValue *aggr_max);
  CHECKED_STATUS EvalMin(const QLValue& val, QLValue *aggr_min);
  CHECKED_STATUS EvalAvg(const QLValue& val, QLValue *aggr_avg);

  CHECKED_STATUS EvalParametricToJson(const QLExpressionPB& operand,
                                      const QLTableRow& table_row,
                                      QLValue *result,
                                      const Schema *schema);

 protected:
  virtual CHECKED_STATUS GetTupleId(QLValue *result) const;
  vector<QLValue> aggr_result_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_DOC_EXPR_H_

//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines and executes expression-related operations in DocDB.
//--------------------------------------------------------------------------------------------------

#ifndef YB_DOCDB_DOC_EXPR_H_
#define YB_DOCDB_DOC_EXPR_H_

#include "yb/common/ql_bfunc.h"
#include "yb/common/ql_value.h"
#include "yb/common/ql_expr.h"
#include "yb/common/schema.h"

namespace yb {
namespace docdb {

class DocExprExecutor : public QLExprExecutor {
 public:
  // Constructor.
  // TODO(neil) Investigate to see if constructor should take column_map and bind_map.
  DocExprExecutor() { }
  virtual ~DocExprExecutor() { }

  // Evaluate call to tablet-server builtin operator.
  virtual CHECKED_STATUS EvalTSCall(const QLBCallPB& ql_expr,
                                    const QLTableRow& column_map,
                                    QLValueWithPB *result) override;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_DOC_EXPR_H_

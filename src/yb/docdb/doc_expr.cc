//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/docdb/doc_expr.h"
#include "yb/docdb/subdocument.h"

namespace yb {
namespace docdb {

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS DocExprExecutor::EvalTSCall(const QLBCallPB& tscall,
                                           const QLTableRow& table_row,
                                           QLValueWithPB *result) {
  bfql::TSOpcode tsopcode = static_cast<bfql::TSOpcode>(tscall.opcode());
  switch (tsopcode) {
    case bfql::TSOpcode::kNoOp:
      break;

    case bfql::TSOpcode::kTtl: {
      DCHECK_EQ(tscall.operands().size(), 1) << "WriteTime takes only one argument, a column";
      const QLExpressionPB& column = tscall.operands(0);
      const auto column_id = ColumnId(column.column_id());
      const auto it = table_row.find(column_id);
      CHECK(it != table_row.end());
      if (it->second.ttl_seconds != -1) {
        result->set_int64_value(it->second.ttl_seconds);
      } else {
        result->SetNull();
      }
      return Status::OK();
    }

    case bfql::TSOpcode::kWriteTime: {
      DCHECK_EQ(tscall.operands().size(), 1) << "WriteTime takes only one argument, a column";
      const QLExpressionPB& column = tscall.operands(0);
      const auto column_id = ColumnId(column.column_id());
      const auto it = table_row.find(column_id);
      CHECK(it != table_row.end());
      result->set_int64_value(it->second.write_time);
      return Status::OK();
    }

    case bfql::TSOpcode::kCount: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kSum: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kAvg: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kMin: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kMax:
      // TODO(neil) Call DocDB to execute aggregate functions.
      // These functions operate across many rows, so some state variables must be kept beyond the
      // scope of one row. The variable "table_row" is not enough for these operators.
      LOG(ERROR) << "Failed to execute aggregate function";
  }

  result->SetNull();
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

}  // namespace docdb
}  // namespace yb

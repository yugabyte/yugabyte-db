//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines the ResultSet that YQL database returns to a query request.
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_QL_EXPR_H_
#define YB_COMMON_QL_EXPR_H_

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/ql_bfunc.h"

namespace yb {

// TODO(neil)
// - This should be maping directly from "int32_t" to QLValue.
//   using ValueMap = std::unordered_map<int32_t, const QLValuePB>;
// - We should use shared_ptr for this map as we might multi-threading the execution process.

// DocDB is using this map, so its code has to be updated before we can change this.
// Slowing down our execution by calling constructor each time is not desired.

// Map for easy lookup of column values of a row by the column id. This map is used in tserver
// for saving the column values of a selected row to evaluate the WHERE and IF clauses. Since
// we use the clauses in protobuf to evaluate, we will maintain the column values in QLValuePB
// also to avoid conversion to and from QLValue.
struct QLTableColumn {
  QLValuePB value;
  int64_t ttl_seconds;
  int64_t write_time;

  std::string ToString() const {
    return Format("{ value: $0 ttl_seconds: $1 write_time: $2 }", value, ttl_seconds, write_time);
  }
};

class QLTableRow {
 public:
  // Public types.
  typedef std::shared_ptr<QLTableRow> SharedPtr;
  typedef std::shared_ptr<const QLTableRow> SharedPtrConst;

  // Check if row is empty (no column).
  bool IsEmpty() const {
    return col_map_.empty();
  }

  // Get column count.
  size_t ColumnCount() const {
    return col_map_.size();
  }

  // Clear the row.
  void Clear() { col_map_.clear(); }

  // Compare column value between two rows.
  bool MatchColumn(ColumnIdRep col_id, const QLTableRow& source) const;
  bool MatchColumn(const ColumnId& col, const QLTableRow& source) const {
    return MatchColumn(col.rep(), source);
  }

  // Allocate column in a map to cache its value, ttl, and writetime.
  QLTableColumn& AllocColumn(ColumnIdRep col_id);
  QLTableColumn& AllocColumn(const ColumnId& col) { return AllocColumn(col.rep()); }

  QLTableColumn& AllocColumn(ColumnIdRep col_id, const QLValue& ql_value);
  QLTableColumn& AllocColumn(const ColumnId& col, const QLValue& ql_value) {
    return AllocColumn(col.rep(), ql_value);
  }

  // Copy column-value from 'source' to the 'col_id' entry in the cached column-map.
  CHECKED_STATUS CopyColumn(ColumnIdRep col_id, const QLTableRow& source);
  CHECKED_STATUS CopyColumn(const ColumnId& col, const QLTableRow& source) {
    return CopyColumn(col.rep(), source);
  }

  // Get a column TTL.
  CHECKED_STATUS GetTTL(ColumnIdRep col_id, int64_t *ttl_seconds) const;

  // Get a column WriteTime.
  CHECKED_STATUS GetWriteTime(ColumnIdRep col_id, int64_t *write_time) const;

  // Copy the column value of the given ID to output parameter "column".
  CHECKED_STATUS GetValue(ColumnIdRep col_id, QLValue *column) const;
  CHECKED_STATUS GetValue(const ColumnId& col, QLValue *column) const {
    return GetValue(col.rep(), column);
  }

  // Get the column value in PB format.
  CHECKED_STATUS ReadColumn(ColumnIdRep col_id, QLValue *col_value) const;
  CHECKED_STATUS ReadSubscriptedColumn(const QLSubscriptedColPB& subcol,
                                       const QLValue& index,
                                       QLValue *col_value) const;

  // For testing only (no status check).
  const QLTableColumn& TestValue(ColumnIdRep col_id) const {
    return col_map_.at(col_id);
  }
  const QLTableColumn& TestValue(const ColumnId& col) const {
    return col_map_.at(col.rep());
  }

  std::string ToString() const {
    return yb::ToString(col_map_);
  }

  std::string ToString(const Schema& schema) const;

 private:
  std::unordered_map<ColumnIdRep, QLTableColumn> col_map_;
};

class QLExprExecutor {
 public:
  // Public types.
  typedef std::shared_ptr<QLExprExecutor> SharedPtr;
  typedef std::shared_ptr<const QLExprExecutor> SharedPtrConst;

  // Constructor.
  // TODO(neil) Investigate to see if constructor should save some parameters as members since
  // we pass the same parameter over & over again when calling function recursively.
  QLExprExecutor() { }
  virtual ~QLExprExecutor() { }

  // Get TServer opcode.
  yb::bfql::TSOpcode GetTSWriteInstruction(const QLExpressionPB& ql_expr) const;

  // Evaluate the given QLExpressionPB.
  CHECKED_STATUS EvalExpr(const QLExpressionPB& ql_expr,
                          const QLTableRow& table_row,
                          QLValue *result);

  // Read evaluated value from an expression. This is only useful for aggregate function.
  CHECKED_STATUS ReadExprValue(const QLExpressionPB& ql_expr,
                               const QLTableRow& table_row,
                               QLValue *result);

  // Evaluate call to regular builtin operator.
  virtual CHECKED_STATUS EvalBFCall(const QLBCallPB& ql_expr,
                                    const QLTableRow& table_row,
                                    QLValue *result);

  // Evaluate call to tablet-server builtin operator.
  virtual CHECKED_STATUS EvalTSCall(const QLBCallPB& ql_expr,
                                    const QLTableRow& table_row,
                                    QLValue *result);

  virtual CHECKED_STATUS ReadTSCallValue(const QLBCallPB& ql_expr,
                                         const QLTableRow& table_row,
                                         QLValue *result);

  // Evaluate a boolean condition for the given row.
  virtual CHECKED_STATUS EvalCondition(const QLConditionPB& condition,
                                       const QLTableRow& table_row,
                                       bool* result);
  virtual CHECKED_STATUS EvalCondition(const QLConditionPB& condition,
                                       const QLTableRow& table_row,
                                       QLValue *result);
};

} // namespace yb

#endif // YB_COMMON_QL_EXPR_H_

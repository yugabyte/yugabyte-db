//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines the ResultSet that YQL database returns to a query request.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <boost/container/small_vector.hpp>

#include <boost/optional/optional.hpp>

#include "yb/bfql/tserver_opcodes.h"
#include "yb/bfpg/tserver_opcodes.h"

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"
#include "yb/common/ql_value.h"
#include "yb/common/value.messages.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/gutil/casts.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/status.h"
#include "yb/util/status_format.h"

namespace yb::qlexpr {

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
  static constexpr int64_t kUninitializedWriteTime = std::numeric_limits<int64_t>::min();

  QLValuePB value;
  int64_t ttl_seconds = 0;
  int64_t write_time = kUninitializedWriteTime;

  std::string ToString() const;
};

template <class Val>
class ExprResultWriter;

template <class Val>
class ExprResult;

template <>
class ExprResult<QLValuePB> {
 public:
  ExprResult() = default;
  explicit ExprResult(QLValuePB* template_value) {}
  explicit ExprResult(ExprResult<QLValuePB>* template_result) {}

  const QLValuePB& Value();


  void MoveTo(QLValuePB* out);

  QLValuePB& ForceNewValue();

  ExprResultWriter<QLValuePB> Writer();

 private:
  friend class ExprResultWriter<QLValuePB>;

  QLValuePB value_;
  const QLValuePB* existing_value_ = nullptr;
};

template <>
class ExprResult<LWQLValuePB> {
 public:
  explicit ExprResult(ThreadSafeArena* arena) : arena_(arena) {}
  explicit ExprResult(LWQLValuePB* template_value) : arena_(&template_value->arena()) {}
  explicit ExprResult(ExprResult<LWQLValuePB>* template_result)
      : arena_(template_result->arena_) {}

  const LWQLValuePB& Value();

  void MoveTo(LWQLValuePB* out);

  LWQLValuePB& ForceNewValue();

  ExprResultWriter<LWQLValuePB> Writer();

 private:
  friend class ExprResultWriter<LWQLValuePB>;

  ThreadSafeArena* arena_;
  LWQLValuePB* value_ = nullptr;
};

template <class Val>
class ExprResultWriter {
 public:
  explicit ExprResultWriter(ExprResult<Val>* result);

  void SetNull();

  void SetExisting(const Val* existing_value);

  Val& NewValue();

 private:
  ExprResult<Val>* result_;
};

using QLExprResult = ExprResult<QLValuePB>;
using LWExprResult = ExprResult<LWQLValuePB>;

using QLExprResultWriter = ExprResultWriter<QLValuePB>;
using LWExprResultWriter = ExprResultWriter<LWQLValuePB>;

class QLTableRow {
 public:
  // Public types.
  typedef std::shared_ptr<QLTableRow> SharedPtr;
  typedef std::shared_ptr<const QLTableRow> SharedPtrConst;

  static const QLTableRow& empty_row();

  // Check if row is empty (no column).
  bool IsEmpty() const { return num_assigned_ == 0; }
  bool Exists() const { return !IsEmpty(); }

  // Get column count.
  size_t ColumnCount() const;

  // Clear the row.
  void Clear();

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
  QLTableColumn& AllocColumn(ColumnIdRep col_id, const QLValuePB& ql_value);
  QLTableColumn& AllocColumn(const ColumnId& col, const QLValuePB& ql_value) {
    return AllocColumn(col.rep(), ql_value);
  }

  QLTableColumn& AllocColumn(ColumnIdRep col_id, QLValuePB&& ql_value);
  QLTableColumn& AllocColumn(const ColumnId& col, QLValuePB&& ql_value) {
    return AllocColumn(col.rep(), std::move(ql_value));
  }

  // Copy column-value from 'source' to the 'col_id' entry in the cached column-map.
  void CopyColumn(ColumnIdRep col_id, const QLTableRow& source);
  void CopyColumn(const ColumnId& col, const QLTableRow& source) {
    return CopyColumn(col.rep(), source);
  }

  // Get a column TTL.
  Status GetTTL(ColumnIdRep col_id, int64_t *ttl_seconds) const;

  // Get a column WriteTime.
  Status GetWriteTime(ColumnIdRep col_id, int64_t *write_time) const;

  // Copy the column value of the given ID to output parameter "column".
  Status GetValue(ColumnIdRep col_id, QLValue *column) const;
  Status GetValue(const ColumnId& col, QLValue *column) const;
  boost::optional<const QLValuePB&> GetValue(ColumnIdRep col_id) const;
  boost::optional<const QLValuePB&> GetValue(const ColumnId& col) const {
    return GetValue(col.rep());
  }

  // Predicate if given column is specified in the row.
  // NOTE: This returns true if column is specified even when its value is NULL.
  bool IsColumnSpecified(ColumnIdRep col_id) const;

  // Clear the column value.
  void MarkTombstoned(ColumnIdRep col_id);
  void MarkTombstoned(const ColumnId& col) {
    return MarkTombstoned(col.rep());
  }

  // Get the column value in PB format.
  Status ReadColumn(ColumnIdRep col_id, QLExprResultWriter result_writer) const;
  Status ReadColumn(ColumnIdRep col_id, LWExprResultWriter result_writer) const;
  const QLValuePB* GetColumn(ColumnIdRep col_id) const;
  Status ReadSubscriptedColumn(const QLSubscriptedColPB& subcol,
                               const QLValuePB& index,
                               QLExprResultWriter result_writer) const;

  // For testing only (no status check).
  const QLTableColumn& TestValue(ColumnIdRep col_id) const {
    return *FindColumn(col_id);
  }
  const QLTableColumn& TestValue(const ColumnId& col) const {
    return *FindColumn(col.rep());
  }

  std::string ToString() const;
  std::string ToString(const Schema& schema) const;

 private:
  // Return kInvalidIndex when column index is unknown.
  size_t ColumnIndex(ColumnIdRep col_id) const;
  const QLTableColumn* FindColumn(ColumnIdRep col_id) const;
  Result<const QLTableColumn&> Column(ColumnIdRep col_id) const;
  // Appends new entry to values_ and assigned_ fields.
  QLTableColumn& AppendColumn();

  template <class Writer>
  Status DoReadColumn(ColumnIdRep col_id, Writer result_writer) const;

  // Map from column id to index in values_ and assigned_ vectors.
  // For columns from [kFirstColumnId; kFirstColumnId + kPreallocatedSize) we don't use
  // this field and map them directly.
  // I.e. column with id kFirstColumnId will have index 0 etc.
  // We are using unsigned int as map value and std::numeric_limits<size_t>::max() as invalid
  // column.
  // This way, the compiler would understand that this invalid value could never be stored in the
  // map and optimize away the comparison with it when inlining the ColumnIndex function call.
  std::unordered_map<ColumnIdRep, unsigned int> column_id_to_index_;

  static constexpr size_t kPreallocatedSize = 8;
  static constexpr ColumnIdRep kFirstNonPreallocatedColumnId =
      kFirstColumnIdRep + static_cast<ColumnIdRep>(kPreallocatedSize);

  // The two following vectors will be of the same size.
  // We use separate fields to achieve the following features:
  // 1) Fast way to cleanup row, just by setting assigned to false with one call.
  // 2) Avoid destroying values_, so they would be able to reuse allocated storage during row reuse.
  boost::container::small_vector<QLTableColumn, kPreallocatedSize> values_;
  boost::container::small_vector<bool, kPreallocatedSize> assigned_;
  size_t num_assigned_ = 0;
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

  //------------------------------------------------------------------------------------------------
  // CQL Support.

  // Evaluate the given QLExpressionPB.
  Status EvalExpr(const QLExpressionPB& ql_expr,
                  const QLTableRow& table_row,
                  QLExprResultWriter result_writer,
                  const Schema *schema = nullptr);

  // Evaluate the given QLExpressionPB (if needed) and replace its content with the result.
  Status EvalExpr(QLExpressionPB* ql_expr,
                  const QLTableRow& table_row,
                  const Schema *schema = nullptr);

  // Read evaluated value from an expression. This is only useful for aggregate function.
  Status ReadExprValue(const QLExpressionPB& ql_expr,
                       const QLTableRow& table_row,
                       QLExprResultWriter result_writer);

  template <class Row, class Writer>
  Status EvalColumnRef(ColumnIdRep col_id,
                       const Row* table_row,
                       Writer result_writer);

  // Evaluate call to tablet-server builtin operator.
  virtual Status EvalTSCall(const QLBCallPB& ql_expr,
                            const QLTableRow& table_row,
                            QLValuePB *result,
                            const Schema *schema = nullptr);

  virtual Status ReadTSCallValue(const QLBCallPB& ql_expr,
                                 const QLTableRow& table_row,
                                 QLExprResultWriter result_writer);

  // Evaluate a boolean condition for the given row.
  Status EvalCondition(const QLConditionPB& condition,
                       const QLTableRow& table_row,
                       bool* result);
  Status EvalCondition(const QLConditionPB& condition,
                       const QLTableRow& table_row,
                       QLValuePB *result);

  //------------------------------------------------------------------------------------------------
  // PGSQL Support.

  // Evaluate the given QLExpressionPB.
  Status EvalExpr(const PgsqlExpressionPB& ql_expr,
                  const dockv::PgTableRow* table_row,
                  QLExprResultWriter result_writer,
                  const Schema *schema = nullptr);

  Status EvalExpr(const PgsqlExpressionPB& ql_expr,
                  const dockv::PgTableRow& table_row,
                  QLExprResultWriter result_writer,
                  const Schema *schema = nullptr) {
    return EvalExpr(ql_expr, &table_row, result_writer, schema);
  }

  // Read evaluated value from an expression. This is only useful for aggregate function.
  Status ReadExprValue(const PgsqlExpressionPB& ql_expr,
                       const dockv::PgTableRow& table_row,
                       QLExprResultWriter result_writer);

  // Evaluate call to tablet-server builtin operator.
  virtual Status EvalTSCall(const PgsqlBCallPB& ql_expr,
                            const dockv::PgTableRow& table_row,
                            QLValuePB *result,
                            const Schema *schema = nullptr);

  virtual Status ReadTSCallValue(const PgsqlBCallPB& ql_expr,
                                 const dockv::PgTableRow& table_row,
                                 QLExprResultWriter result_writer);

  // Evaluate a boolean condition for the given row.
  Status EvalCondition(const PgsqlConditionPB& condition,
                       const dockv::PgTableRow& table_row,
                       bool* result);

  template <class PB, class Row, class Value>
  Status EvalCondition(
      const PB& condition, const Row& table_row, Value* result);

  virtual Status GetSpecialColumn(ColumnIdRep column_id, QLValuePB* out) {
    return Status::OK();
  }

 private:
  template <class PB, class Row, class Writer>
  Status DoEvalExpr(
      const PB& ql_expr, const Row* table_row, Writer result_writer, const Schema* schema);

  // Evaluate call to regular builtin operator.
  template <class OpCode, class Expr, class Row>
  Result<QLValuePB> EvalBFCall(const Expr& bfcall, const Row& table_row);
};

template <class It, class Row>
Status EvalOperandsHelper(
    QLExprExecutor* executor, It it, const Row& table_row) {
  return Status::OK();
}

template <class It, class Row, class Writer, class... Args>
Status EvalOperandsHelper(
    QLExprExecutor* executor, It it, const Row& table_row,
    Writer writer, Args&&... args) {
  RETURN_NOT_OK(executor->EvalExpr(*it, table_row, writer));
  return EvalOperandsHelper(executor, ++it, table_row, std::forward<Args>(args)...);
}

template <class Operands, class Row, class... Args>
Status EvalOperands(
    QLExprExecutor* executor, const Operands& operands, const Row& table_row,
    Args&&... args) {
  if (operands.size() != sizeof...(Args)) {
    return STATUS_FORMAT(InvalidArgument, "Wrong number of arguments, $0 expected but $1 found",
                         sizeof...(Args), operands.size());
  }

  return EvalOperandsHelper(executor, operands.begin(), table_row, std::forward<Args>(args)...);
}

// Get TServer opcode.
yb::bfql::TSOpcode GetTSWriteInstruction(const QLExpressionPB& ql_expr);
bfpg::TSOpcode GetTSWriteInstruction(const PgsqlExpressionPB& ql_expr);

} // namespace yb::qlexpr

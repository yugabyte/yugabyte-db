// Copyright (c) Yugabyte, Inc.
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

#include "yb/docdb/doc_pg_expr.h"

#include <map>
#include <optional>
#include <utility>

#include <boost/container/small_vector.hpp>

#include "yb/common/schema.h"
#include "yb/common/pgsql_protocol.pb.h"

#include "yb/docdb/docdb_pgapi.h"

#include "yb/dockv/reader_projection.h"

#include "ybgate/ybgate_api.h"

#include "yb/util/logging.h"

#include "yb/yql/pggate/pg_value.h"

namespace yb::docdb {
namespace {

template<class Func, class... Args>
Status YbgFuncWrapper(Func func, Args&&... args) {
  PG_RETURN_NOT_OK(func(std::forward<Args>(args)...));
  return Status::OK();
}

inline Status DeleteMemoryContext() {
  return YbgFuncWrapper(&YbgDeleteMemoryContext);
}

inline Status CreateMemoryContext(
    YbgMemoryContext parent, const char* name, YbgMemoryContext* memctx) {

  return YbgFuncWrapper(&YbgCreateMemoryContext, parent, name, memctx);
}

inline Status ResetMemoryContext() {
  return YbgFuncWrapper(&YbgResetMemoryContext);
}

class MemoryContextGuard {
 public:
  explicit MemoryContextGuard(YbgMemoryContext ctx_to_restore)
      : ctx_to_restore_(ctx_to_restore) {
  }

  ~MemoryContextGuard() {
    YbgSetCurrentMemoryContext(ctx_to_restore_);
  }

 private:
  YbgMemoryContext ctx_to_restore_;

  DISALLOW_COPY_AND_ASSIGN(MemoryContextGuard);
};

class ColumnIdxResolver {
 public:
  ColumnIdxResolver(
      std::reference_wrapper<const Schema> schema,
      std::reference_wrapper<const dockv::ReaderProjection> projection)
      : schema_(schema), projection_(projection) {}

  Result<size_t> GetColumnIdx(ColumnId id) const {
    auto result = projection_.ColumnIdxById(id);
    RSTATUS_DCHECK_NE(
      result, dockv::ReaderProjection::kNotFoundIndex, InternalError, "Invalid projection");
    return result;
  }

  Result<size_t> GetColumnIdx(int32_t attno) const {
    const auto& columns = schema_.columns();
    auto it = std::find_if(
        columns.begin(), columns.end(),
        [attno](const auto& column) { return attno == column.order(); });
    RSTATUS_DCHECK(it != columns.end(), InternalError, Format("Column not found: $0", attno));
    return GetColumnIdx(schema_.column_id(std::distance(columns.begin(), it)));
  }

 private:
  const Schema& schema_;
  const dockv::ReaderProjection& projection_;

  DISALLOW_COPY_AND_ASSIGN(ColumnIdxResolver);
};

// Deserialized Postgres expression paired with type information to convert results to DocDB format
using DocPgEvalExprData = std::pair<YbgPreparedExpr, DocPgVarRef>;

class TSCallExecutor {
 public:
  explicit TSCallExecutor(std::reference_wrapper<const ColumnIdxResolver> resolver)
      : resolver_(resolver) {
    // Memory context to store things that are needed for executor lifetime, like column references
    // or deserialized expressions.
    CHECK_OK(CreateMemoryContext(nullptr, "DocPg Expression Context", &mem_ctx_));
  }

  ~TSCallExecutor() {
    // If row_ctx_ was created it is deleted with mem_ctx_, as mem_ctx_ is the parent
    YbgSetCurrentMemoryContext(mem_ctx_);
    CHECK_OK(DeleteMemoryContext());
  }

  Status AddColumnRef(const PgsqlColRefPB& column_ref) {
    ColumnId col_id(column_ref.column_id());
    // Column references without Postgres type info are not used for expression evaluation
    // they may still be used for something else
    if (!column_ref.has_typid()) {
      VLOG(1) << "Column reference " << col_id << " has no type information, skipping";
      return Status::OK();
    }
    VLOG(1) << "Column lookup " << col_id;
    // Prepare DocPgVarRef object and store it in the var_map_ using the attribute number as a key.
    // The DocPgVarRef object encapsulates info needed to extract DocDB from a row and convert it
    // to Postgres format.
    return DocPgAddVarRef(VERIFY_RESULT(resolver_.GetColumnIdx(col_id)),
                          column_ref.attno(),
                          column_ref.typid(),
                          column_ref.has_typmod() ? column_ref.typmod() : -1,
                          column_ref.has_collid() ? column_ref.collid() : 0,
                          &var_map_);
  }

  Status AddWhere(const PgsqlBCallPB& tscall) {
    where_clause_.push_back(VERIFY_RESULT(PrepareExprCall(tscall)));
    VLOG(1) << "A condition has been added";
    return Status::OK();
  }

  Status AddTarget(const PgsqlBCallPB& tscall) {
    DocPgVarRef expr_type;
    auto* prepared_expr = VERIFY_RESULT(PrepareExprCall(tscall, &expr_type));
    targets_.emplace_back(prepared_expr, expr_type);
    VLOG(1) << "A target expression has been added";
    return Status::OK();
  }

  Result<bool> Exec(
      const dockv::PgTableRow& row, std::vector<qlexpr::QLExprResult>* results) {
    // early exit if there are no operations to process
    if(where_clause_.empty() && targets_.empty()) {
      return true;
    }

    auto mem_context_reset_required = true;
    if (!row_ctx_) {
      // The first row, prepare memory context for per row allocations
      RETURN_NOT_OK(CreateMemoryContext(mem_ctx_, "DocPg Row Context", &row_ctx_));
      mem_context_reset_required = false;
    }
    MemoryContextGuard mem_guard(YbgSetCurrentMemoryContext(row_ctx_));
    if (mem_context_reset_required) {
      // Clean up memory allocations that may be still around after previous row was processed
      RETURN_NOT_OK(ResetMemoryContext());
    }

    RETURN_NOT_OK(PreparePgRowData(row));
    if (!VERIFY_RESULT(EvalWhereExprCalls())) {
      return false;
    }
    if (results && !targets_.empty()) {
      RETURN_NOT_OK(EvalTargetExprCalls(results));
    }
    return true;
  }

 private:
  Result<YbgPreparedExpr> PrepareExprCall(
      const PgsqlBCallPB& tscall, DocPgVarRef* expr_type = nullptr) {
    // Presence of row_ctx_ indicates that execution was started we do not allow to modify
    // the executor dynamically.
    RSTATUS_DCHECK(!row_ctx_, InternalError, "Can not add expression, execution has started");
    // Retrieve string representing the expression
    const auto& expr_str = tscall.operands(0).value().string_value();
    // Make sure expression is in the right memory context
    MemoryContextGuard mem_guard(YbgSetCurrentMemoryContext(mem_ctx_));
    // Perform deserialization and get result data type info
    YbgPreparedExpr prepared_expr = nullptr;
    RETURN_NOT_OK(DocPgPrepareExpr(expr_str, &prepared_expr, expr_type));
    if (tscall.operands_size() > 1) {
      // Pre-pushdown nodes e.g. v2.12 may create and send serialized PG expression when executing
      // statements like UPDATE table SET col = col + 1 WHERE pk = 1; during upgrade.
      // Those expressions have their column references bundled as operands (attno, typid, typmod)
      // triplets, one per reference.
      // That is sufficient information to execute such request. We need to discover the column id,
      // it is not super efficient, but good enough for a rare compatibility case.
      const auto num_params = (tscall.operands_size() - 1) / 3;
      LOG(INFO) << "Found old style expression with " << num_params << " bundled parameter(s)";
      for (int i = 0; i < num_params; ++i) {
        const auto attno = tscall.operands(3 * i + 1).value().int32_value();
        const auto typid = tscall.operands(3 * i + 2).value().int32_value();
        const auto typmod = tscall.operands(3 * i + 3).value().int32_value();
        RETURN_NOT_OK(DocPgAddVarRef(VERIFY_RESULT(resolver_.GetColumnIdx(attno)),
                      attno, typid, typmod, 0 /*collid*/, &var_map_));
      }
    }
    return prepared_expr;
  }

  Status PreparePgRowData(const dockv::PgTableRow& row) {
    if (var_map_.empty()) {
      return Status::OK();
    }
    RETURN_NOT_OK(EnsureExprContext());
    // Transfer referenced row values to the expr_ctx_ container
    return DocPgPrepareExprCtx(row, var_map_, expr_ctx_);
  }

  Status EnsureExprContext() {
    if (expr_ctx_) {
      return Status::OK();
    }
    // While contents of the expression container is updated per row, the container itself
    // should persist. So make sure that mem_ctx_ is curent during the creation.
    MemoryContextGuard mem_guard(YbgSetCurrentMemoryContext(mem_ctx_));
    return DocPgCreateExprCtx(var_map_, &expr_ctx_);
  }

  Result<bool> EvalWhereExprCalls() {
    for (const auto& expr : where_clause_) {
      auto [datum, is_null] = VERIFY_RESULT(DocPgEvalExpr(expr, expr_ctx_));
      // Stop iteration if expression does not yield true
      if (is_null || !datum) {
        return false;
      }
    }
    return true;
  }

  Status EvalTargetExprCalls(std::vector<qlexpr::QLExprResult>* results) {
    results->reserve(targets_.size());
    for (const auto& target : targets_) {
      auto [datum, is_null] = VERIFY_RESULT(DocPgEvalExpr(target.first, expr_ctx_));
      // Convert Postgres result to DocDB
      RETURN_NOT_OK(yb::pggate::PgValueToPB(
          target.second.var_type, datum, is_null, &results->emplace_back().Writer().NewValue()));
    }
    return Status::OK();
  }

  const ColumnIdxResolver& resolver_;

  // Memory context for permanent allocations. Exists for executor's lifetime.
  YbgMemoryContext mem_ctx_ = nullptr;
  // Memory context for per row allocations. Reset with every new row.
  YbgMemoryContext row_ctx_ = nullptr;
  // Container for Postgres-format data retrieved from the DocDB row.
  // Provides fast access to is_nulls and datums by index(attribute number).
  YbgExprContext expr_ctx_ = nullptr;
  // Where clause expressions
  boost::container::small_vector<YbgPreparedExpr, 8> where_clause_;
  // Target expressions with their type info
  boost::container::small_vector<DocPgEvalExprData, 8> targets_;
  // Storage for column references.
  // Key is the attribute number, value is basically DocDB column id and type info.
  std::map<int, const DocPgVarRef> var_map_;

  DISALLOW_COPY_AND_ASSIGN(TSCallExecutor);
};

class ConditionFilter {
 public:
  void Add(const PgsqlConditionPB& condition) {
    conditions_.push_back(&condition);
  }

  Result<bool> IsMatch(const dockv::PgTableRow& row) {
    auto match = false;
    for (const auto* condition : conditions_) {
      RETURN_NOT_OK(executor_.EvalCondition(*condition, row, &match));
      if (!match) {
        return false;
      }
    }
    return true;
  }

 private:
  qlexpr::QLExprExecutor executor_;
  boost::container::small_vector<const PgsqlConditionPB*, 8> conditions_;
};

} // namespace

class DocPgExprExecutor::State {
 public:
  State(
      std::reference_wrapper<const Schema> schema,
      std::reference_wrapper<const dockv::ReaderProjection> projection)
      : resolver_(schema, projection) {}

  Status AddColumnRef(const PgsqlColRefPB& column_ref) {
    return tscall_executor().AddColumnRef(column_ref);
  }

  Status AddWhere(const PgsqlExpressionPB& expr) {
    if (expr.has_condition()) {
      condition_filter().Add(expr.condition());
      return Status::OK();
    }
    return tscall_executor().AddWhere(VERIFY_RESULT_REF(GetTSCall(expr)));
  }

  Status AddTarget(const PgsqlExpressionPB& expr) {
    return tscall_executor().AddTarget(VERIFY_RESULT_REF(GetTSCall(expr)));
  }

  Result<bool> Exec(
      const dockv::PgTableRow& row, std::vector<qlexpr::QLExprResult>* results) {
    return (!condition_filter_ || VERIFY_RESULT(condition_filter_->IsMatch(row))) &&
           (!tscall_executor_ || VERIFY_RESULT(tscall_executor_->Exec(row, results)));
  }

  bool IsColumnRefsRequired() const {
    return tscall_executor_.has_value();
  }

 private:
  Result<const PgsqlBCallPB&> GetTSCall(const PgsqlExpressionPB& expr) {
    RSTATUS_DCHECK(expr.has_tscall(),
                   InternalError,
                   Format("Unsupported expression type $0", expr.expr_case()));
    auto& tscall = expr.tscall();
    RSTATUS_DCHECK_EQ(
        tscall.opcode(), to_underlying(bfpg::TSOpcode::kPgEvalExprCall),
        InternalError, "Serialized Postgres expression is expected");
    return tscall;
  }

  TSCallExecutor& tscall_executor() {
    if (!tscall_executor_) {
      tscall_executor_.emplace(resolver_);
    }
    return *tscall_executor_;
  }

  ConditionFilter& condition_filter() {
    if (!condition_filter_) {
      condition_filter_.emplace();
    }
    return *condition_filter_;
  }

  const ColumnIdxResolver resolver_;
  std::optional<TSCallExecutor> tscall_executor_;
  std::optional<ConditionFilter> condition_filter_;
};

DocPgExprExecutor::DocPgExprExecutor(std::unique_ptr<State> state)
    : state_(std::move(state)) {
}

DocPgExprExecutor::~DocPgExprExecutor() = default;

Result<bool> DocPgExprExecutor::Exec(
    const dockv::PgTableRow& row, std::vector<qlexpr::QLExprResult>* results) {
  return state_->Exec(row, results);
}

DocPgExprExecutorBuilder::DocPgExprExecutorBuilder(
    std::reference_wrapper<const Schema> schema,
    std::reference_wrapper<const dockv::ReaderProjection> projection)
    : state_(new DocPgExprExecutor::State(schema, projection)) {
}

DocPgExprExecutor::DocPgExprExecutor(DocPgExprExecutor&&) = default;
DocPgExprExecutor& DocPgExprExecutor::operator=(DocPgExprExecutor&&) = default;
DocPgExprExecutorBuilder::~DocPgExprExecutorBuilder() = default;

Status DocPgExprExecutorBuilder::AddWhere(std::reference_wrapper<const PgsqlExpressionPB> expr) {
  return state_->AddWhere(expr);
}

Status DocPgExprExecutorBuilder::AddTarget(const PgsqlExpressionPB& expr) {
  return state_->AddTarget(expr);
}

bool DocPgExprExecutorBuilder::IsColumnRefsRequired() const {
  return state_->IsColumnRefsRequired();
}

Status DocPgExprExecutorBuilder::AddColumnRef(const PgsqlColRefPB& column_ref) {
  return state_->AddColumnRef(column_ref);
}

DocPgExprExecutor DocPgExprExecutorBuilder::DoBuild() {
  decltype(state_) state;
  state.swap(state_);
  return DocPgExprExecutor(std::move(state));
}

}  // namespace yb::docdb

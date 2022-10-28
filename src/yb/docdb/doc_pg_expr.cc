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

#include <list>

#include "yb/docdb/doc_pg_expr.h"
#include "yb/docdb/docdb_pgapi.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/yql/pggate/pg_value.h"

using yb::pggate::PgValueToPB;

namespace yb {
namespace docdb {

//--------------------------------------------------------------------------------------------------

// Deserialized Postgres expression paired with type information to convert results to DocDB format
typedef std::pair<YbgPreparedExpr, DocPgVarRef> DocPgEvalExprData;

class DocPgExprExecutor::Private {
 public:
  Private() {
    // Memory context to store things that are needed for executor lifetime, like column references
    // or deserialized expressions.
    YbgCreateMemoryContext(nullptr, "DocPg Expression Context", &mem_ctx_);
  }

  ~Private() {
    // If row_ctx_ was created it is deleted with mem_ctx_, as mem_ctx_ is the parent
    YbgSetCurrentMemoryContext(mem_ctx_, nullptr);
    YbgDeleteMemoryContext();
  }

  // Process a column reference
  Status AddColumnRef(const PgsqlColRefPB& column_ref,
                              const Schema *schema) {
    DCHECK(expr_ctx_ == nullptr);
    // Get DocDB column identifier
    ColumnId col_id = ColumnId(column_ref.column_id());
    // Column references without Postgres type info are not used for expression evaluation
    // they may still be used for something else
    if (!column_ref.has_typid()) {
      VLOG(1) << "Column reference " << col_id << " has no type information, skipping";
      return Status::OK();
    }
    // Find column in the schema
    VLOG(1) << "Column lookup " << col_id;
    auto column = schema->column_by_id(col_id);
    SCHECK(column.ok(), InternalError, "Invalid Schema");
    SCHECK_EQ(column->order(), column_ref.attno(), InternalError, "Invalid Schema");
    // Prepare DocPgVarRef object and store it in the var_map_ using the attribute number as a key.
    // The DocPgVarRef object encapsulates info needed to extract DocDB from a row and convert it
    // to Postgres format.
    return DocPgAddVarRef(col_id,
                          column_ref.attno(),
                          column_ref.typid(),
                          column_ref.has_typmod() ? column_ref.typmod() : -1,
                          column_ref.has_collid() ? column_ref.collid() : 0,
                          &var_map_);
  }

  // Process a where clause expression
  Status PreparePgWhereExpr(const PgsqlExpressionPB& ql_expr,
                                    const Schema *schema) {
    YbgPreparedExpr expr;
    // Deserialize Postgres expression. Expression type is known to be boolean
    RETURN_NOT_OK(prepare_pg_expr_call(ql_expr, schema, &expr, nullptr));
    // Store the Postgres expression in the list
    where_clause_.push_back(expr);
    VLOG(1) << "A condition has been added";
    return Status::OK();
  }

  // Process a target expression
  Status PreparePgTargetExpr(const PgsqlExpressionPB& ql_expr,
                                     const Schema *schema) {
    YbgPreparedExpr expr;
    DocPgVarRef expr_type;
    // Deserialize Postgres expression. Get type information to convert evaluation results to
    // DocDB format
    RETURN_NOT_OK(prepare_pg_expr_call(ql_expr, schema, &expr, &expr_type));
    // Store the Postgres expression in the list
    targets_.emplace_back(expr, expr_type);
    VLOG(1) << "A target expression has been added";
    return Status::OK();
  }

  // Deserialize a Postgres expression and optionally determine its result data type info
  Status prepare_pg_expr_call(const PgsqlExpressionPB& ql_expr,
                                      const Schema *schema,
                                      YbgPreparedExpr *expr,
                                      DocPgVarRef *expr_type) {
    YbgMemoryContext old;
    // Presence of row_ctx_ indicates that execution was started we do not allow to modify
    // the executor dynamically.
    SCHECK(!row_ctx_, InternalError, "Can not add expression, execution has started");
    SCHECK_EQ(
        ql_expr.expr_case(), PgsqlExpressionPB::ExprCase::kTscall, InternalError,
        "Unexpected expression code");
    const PgsqlBCallPB& tscall = ql_expr.tscall();
    SCHECK(
        static_cast<bfpg::TSOpcode>(tscall.opcode()) == bfpg::TSOpcode::kPgEvalExprCall,
        InternalError, "Serialized Postgres expression is expected");
    SCHECK_EQ(tscall.operands_size(), 1, InternalError, "Invalid serialized Postgres expression");
    // Retrieve string representing the expression
    const std::string& expr_str = tscall.operands(0).value().string_value();
    // Make sure expression is in the right memory context
    YbgSetCurrentMemoryContext(mem_ctx_, &old);
    // Perform deserialization and get result data type info
    const Status s = DocPgPrepareExpr(expr_str, expr, expr_type);
    // Restore previous memory context
    YbgSetCurrentMemoryContext(old, nullptr);
    return s;
  }

  // Retrieve expressions from the row according to the added column references
  Status PreparePgRowData(const QLTableRow& table_row) {
    Status s = Status::OK();
    // If there are no column references the expression context will not be used
    if (!var_map_.empty()) {
      s = ensure_expr_context();
      // Transfer referenced row values to the expr_ctx_ container
      if (s.ok()) {
        s = DocPgPrepareExprCtx(table_row, var_map_, expr_ctx_);
      }
    }

    return s;
  }

  // Create the expression context if does not exist
  Status ensure_expr_context() {
    if (expr_ctx_ == nullptr) {
      YbgMemoryContext old;
      // While contents of the expression container is updated per row, the container itself
      // should persist. So make sure that mem_ctx_ is curent during the creation.
      YbgSetCurrentMemoryContext(mem_ctx_, &old);
      RETURN_NOT_OK(DocPgCreateExprCtx(var_map_, &expr_ctx_));
      YbgSetCurrentMemoryContext(old, nullptr);
    }
    return Status::OK();
  }

  // Evaluate where clause expressions
  Status EvalWhereExprCalls(bool *result) {
    // If where_clause_ is empty or all the expressions yield true, the result will remain true
    *result = true;

    uint64_t datum;
    bool is_null;
    for (auto expr : where_clause_) {
      // Evaluate expression
      RETURN_NOT_OK(DocPgEvalExpr(expr, expr_ctx_, &datum, &is_null));
      // Stop iteration and return false if expression does not yield true
      if (is_null || !datum) {
        *result = false;
        break;
      }
    }
    return Status::OK();
  }

  // Evaluate target expressions and write results into provided vector elements
  Status EvalTargetExprCalls(std::vector<QLExprResult>* results) {
    // Shortcut if there is nothing to evaluate
    if (targets_.empty()) {
      return Status::OK();
    }
    SCHECK_GE(
        results->size(), targets_.size(), InternalError,
        "Provided results storage is insufficient");

    // Output element's index
    int i = 0;
    for (const DocPgEvalExprData& target : targets_) {
      // Container for the DocDB result
      QLExprResult &result = (*results)[i++];
      // Containers for Postgres result
      uint64_t datum;
      bool is_null;
      // Evaluate the expression
      RETURN_NOT_OK(DocPgEvalExpr(target.first, expr_ctx_, &datum, &is_null));
      // Convert Postgres result to DocDB
      RETURN_NOT_OK(
          PgValueToPB(target.second.var_type, datum, is_null, &result.Writer().NewValue()));
    }
    return Status::OK();
  }

  Status Exec(const QLTableRow& table_row,
              std::vector<QLExprResult>* results,
              bool* match) {
    *match = true;

    // early exit if there are no operations to process
    if(var_map_.empty() && where_clause_.empty() && targets_.empty()) {
      return Status::OK();
    }

    // Set the correct memory context
    YbgMemoryContext old;
    if (row_ctx_ == nullptr) {
      // The first row, prepare memory context for per row allocations
      YbgCreateMemoryContext(mem_ctx_, "DocPg Row Context", &row_ctx_);
      YbgSetCurrentMemoryContext(row_ctx_, &old);
    } else {
      // Clean up memory allocations that may be still around after previous row was processed
      YbgSetCurrentMemoryContext(row_ctx_, &old);
      YbgResetMemoryContext();
    }

    Status status = PreparePgRowData(table_row);
    if (status.ok())
      status = EvalWhereExprCalls(match);

    if (status.ok() && *match)
      status = EvalTargetExprCalls(results);

    // Restore previous memory context
    YbgSetCurrentMemoryContext(old, nullptr);

    return status;
  }

 private:
  // Memory context for permanent allocations. Exists for executor's lifetime.
  YbgMemoryContext mem_ctx_ = nullptr;
  // Memory context for per row allocations. Reset with every new row.
  YbgMemoryContext row_ctx_ = nullptr;
  // Container for Postgres-format data retrieved from the DocDB row.
  // Provides fast access to is_nulls and datums by index(attribute number).
  YbgExprContext expr_ctx_ = nullptr;
  // List of where clause expressions
  std::list<YbgPreparedExpr> where_clause_;
  // List of target expressions with their type info
  std::list<DocPgEvalExprData> targets_;
  // Storage for column references. Key is the attribute number, value is basically DocDB column id
  // and type info.
  // There are couple minor benefits of using ordered map here. First, we tolerate duplicate column
  // references, second is that we iterate over columns in their schema order, hopefully this speeds
  // up access to data.
  std::map<int, const DocPgVarRef> var_map_;
};

void DocPgExprExecutor::private_deleter::operator()(DocPgExprExecutor::Private* ptr) const {
  // DocPgExprExecutor::Private is a complete class in this module, so it can be simply deleted
  delete ptr;
}

//--------------------------------------------------------------------------------------------------

Status DocPgExprExecutor::AddColumnRef(const PgsqlColRefPB& column_ref) {
  if (private_.get() == nullptr) {
    private_.reset(new Private());
  }
  return private_->AddColumnRef(column_ref, schema_);
}

Status DocPgExprExecutor::AddWhereExpression(const PgsqlExpressionPB& ql_expr) {
  if (private_.get() == nullptr) {
    private_.reset(new Private());
  }
  return private_->PreparePgWhereExpr(ql_expr, schema_);
}

Status DocPgExprExecutor::AddTargetExpression(const PgsqlExpressionPB& ql_expr) {
  if (private_.get() == nullptr) {
    private_.reset(new Private());
  }
  return private_->PreparePgTargetExpr(ql_expr, schema_);
}

Status DocPgExprExecutor::Exec(
    const QLTableRow& table_row, std::vector<QLExprResult>* results, bool* match) {
  return !private_.get() ? Status::OK() : private_->Exec(table_row, results, match);
}

}  // namespace docdb
}  // namespace yb

// Copyright (c) YugaByte, Inc.
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

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "yb/common/pgsql_protocol.fwd.h"
#include "yb/dockv/dockv_fwd.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
class Schema;

namespace docdb {

// DocPgExprExecutor is optimized for repeatable execution of serialized postgres expressions.
//
// Evaluations are supposed to happen in context of single relation scan, and expected
// DocPgExprExecutor object's lifespan is the duration of that scan. It is also can be used to
// evaluate expressions for a single row operation, such as insert or update.
// Constructor takes relation's schema and its projection as parameters. They are needed to look
// up column references. If column reference provides the column id, it can be found in the
// projection, otherwise column id should be looked up by the attno in the schema. Column id is
// expected in most cases, however some legacy structures provide the attno only.
//
// After object has been constructed, the expressions and column references should be added, in
// any order. Column references, where clause and target expressions are treated differently and
// added using different methods. All parts are optional, however if any expression refers a column
// (has a Var expression node in the tree), it must be explicitly added.
//
// Then Exec method should be called to evaluate expressions per relation row. After Exec was called
// for the first time, column references or expressions can no longer be added to the executor.
class DocPgExprExecutor {
  class State;

 public:
  DocPgExprExecutor(DocPgExprExecutor&&);
  DocPgExprExecutor& operator=(DocPgExprExecutor&&);
  ~DocPgExprExecutor();

  // Evaluate the expression in the context of single row.
  // Method extracts values from the row according to the column references added to the executor.
  // Extracted values are converted to Postgres format (datum and is_null pairs). The case if no
  // columns are referenced is possible, but not very practical, it means that all expressions are
  // constants.
  // Then where clause expressions are evaluated, if any, in the order they are added. If a where
  // clause expression is evaluated to false, execution stops and match is returned as false
  // (results is not changed in this case).
  // Then target expressions are evaluated in the order they were added. Execution results are
  // converted to DocDB values and written into the next element of the results vector. This is a
  // caller's responsibility to track target expressions added to the executor.
  Result<bool> Exec(
      const dockv::PgTableRow& row, std::vector<qlexpr::QLExprResult>* results = nullptr);

 private:
  explicit DocPgExprExecutor(std::unique_ptr<State> state);

  friend class DocPgExprExecutorBuilder;

  std::unique_ptr<State> state_;
};

class DocPgExprExecutorBuilder {
 public:
  DocPgExprExecutorBuilder(
      std::reference_wrapper<const Schema> schema,
      std::reference_wrapper<const dockv::ReaderProjection> projection);
  ~DocPgExprExecutorBuilder();

  // Add a where clause expression to the executor.
  // Where clause expression must return boolean value. Where clause expressions are implicitly
  // AND'ed. If any is evaluated to false, no further evaluation happens and row is considered
  // filtered out. Empty where clause means no rows filtered out.
  Status AddWhere(std::reference_wrapper<const PgsqlExpressionPB> expr);

  // Add a target expression to the executor.
  // Expression is deserialized and stored in the list of the target expressions. Function prepares
  // to convert evaluation results to DocDB format based on expression's Postgres data type.
  // Each expression produces single value to be returned to client (like in SELECT), stored to
  // DocDB table (like in UPDATE), or both (like in UPDATE with RETURNING clause). Target
  // expressions are evaluated unless where clause was evaluated to false.
  Status AddTarget(const PgsqlExpressionPB& expr);

  template<class ColumnRefs>
  Result<DocPgExprExecutor> Build(const ColumnRefs& refs) {
    if (IsColumnRefsRequired()) {
      for (const auto& column_ref : refs) {
        RETURN_NOT_OK(AddColumnRef(column_ref));
      }
    }
    return DoBuild();
  }

 private:
  bool IsColumnRefsRequired() const;
  Status AddColumnRef(const PgsqlColRefPB& column_ref);
  DocPgExprExecutor DoBuild();

  std::unique_ptr<DocPgExprExecutor::State> state_;
};

} // namespace docdb
} // namespace yb

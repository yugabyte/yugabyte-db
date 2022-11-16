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

#include "yb/common/ql_expr.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/schema.h"
#include "yb/util/status.h"

namespace yb {
namespace docdb {

// DocPgExprExecutor is optimized for repeatable execution of serialized postgres expressions.
//
// Evaluations are supposed to happen in context of single relation scan, and expected
// DocPgExprExecutor object's lifespan is the duration of that scan. It is also can be used to
// evaluate expressions for a single row operation, such as insert or update.
// Constructor takes relation's schema as a parameter, it is needed to extract column values
// referenced by the expressions and convert to Postgres format they expect.
//
// After object has been constructed, the expressions and column references should be added, in
// any order. Column references, where clause and target expressions are treated differently and
// added using different methods. All parts are optional, however if any expression refers a column
// (has a Var expression node in the tree), it must be explicitly added.
//
// Then Exec method should be called to evaluate expressions per relation row. After Exec was called
// for the first time, column references or expressions can no longer be added to the executor.
class DocPgExprExecutor {
 public:
  // Create new executor to evaluate expressions for a relation with specified schema.
  explicit DocPgExprExecutor(const Schema *schema) : schema_(schema) {}

  // Destroy the executor and release all allocated resources.
  virtual ~DocPgExprExecutor() {}

  // Add column reference to the executor.
  // Column id, attribute number, type information is extracted from the protobuf element and
  // stored in a data structure allowing to quickly locate the value in the row, convert it from
  // DocDB data format to Postgres and then locate it by attribute number when evaluating
  // expressions.
  Status AddColumnRef(const PgsqlColRefPB& column_ref);

  // Add a where clause expression to the executor.
  // Expression is deserialized and stored in the list of where clause expressions.
  // Where clause expression must return boolean value. Where clause expressions are implicitly
  // AND'ed. If any is evaluated to false, no further evaluation happens and row is considered
  // filtered out. Empty where clause means no rows filtered out.
  Status AddWhereExpression(const PgsqlExpressionPB& ql_expr);

  // Add a target expression to the executor.
  // Expression is deserialized and stored in the list of the target expressions. Function prepares
  // to convert evaluation results to DocDB format based on expression's Postgres data type.
  // Each expression produces single value to be returned to client (like in SELECT), stored to
  // DocDB table (like in UPDATE), or both (like in UPDATE with RETURNING clause). Target
  // expressions are evaluated unless where clause was evaluated to false.
  Status AddTargetExpression(const PgsqlExpressionPB& ql_expr);

  // Evaluate the expression in the context of single row.
  // The results vector is expected to have at least as many entries as the number of target
  // expressions added to the executor. If no target expressions were added it is OK to pass null.
  // The match parameter is mandatory. It returns true if the where clause is empty or evaluated to
  // true, false otherwise. If function returns false it does not modify the results.
  // Method extracts values from the row according to the column references added to the executor.
  // Extracted values are converted to Postgres format (datum and is_null pairs). The case if no
  // columns are referenced is possible, but not very practical, it means that all expressions are
  // constants.
  // Then where clause expressions are evaluated, if any, in the order they are added. If a where
  // clause expression is evaluated to false, execution stops and match is returned as false.
  // Then target expressions are evaluated in the order they are added. Execution results are
  // converted to DocDB values and written into the next element of the results vector. This is a
  // caller's responsibility to track target expressions added to the executor, provide sufficiently
  // long results vector and match the results.
  Status Exec(const QLTableRow& table_row,
              std::vector<QLExprResult>* results,
              bool* match);

 private:
  // The relation schema
  const Schema *schema_;
  // Private object hides implementation details, heavily based on the ybgate.
  // We do not want ybgate data types and functions to appear in this header file, and therefore
  // included everywhere where we may want to use the executor.
  class Private;
  // The Private object's size is not known here, so explicit deleter is needed.
  // The implementation in the .cc file has all the information compiler needs.
  struct private_deleter { void operator()(Private*) const; };
  std::unique_ptr<Private, private_deleter> private_;
};

} // namespace docdb
} // namespace yb

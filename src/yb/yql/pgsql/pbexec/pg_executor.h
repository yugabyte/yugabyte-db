//--------------------------------------------------------------------------------------------------
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
//
// Entry point for the execution process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PBEXEC_PG_EXECUTOR_H_
#define YB_YQL_PGSQL_PBEXEC_PG_EXECUTOR_H_

#include "yb/common/ql_expr.h"
#include "yb/common/ql_rowblock.h"
#include "yb/common/partial_row.h"

#include "yb/yql/pgsql/pbexec/pg_exec_context.h"

#include "yb/yql/pgsql/ptree/pg_tdatabase.h"
#include "yb/yql/pgsql/ptree/pg_tcreate_schema.h"
#include "yb/yql/pgsql/ptree/pg_tcreate_table.h"
#include "yb/yql/pgsql/ptree/pg_tdrop.h"

namespace yb {
namespace pgsql {

class PgExecutor {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<PgExecutor> SharedPtr;
  typedef std::shared_ptr<const PgExecutor> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  PgExecutor();
  virtual ~PgExecutor();

  CHECKED_STATUS Run(const PgExecContext::SharedPtr& exec_context);

  //------------------------------------------------------------------------------------------------
  // API for DDL statements.
  //------------------------------------------------------------------------------------------------
  CHECKED_STATUS ExecDdl(const TreeNode *tstmt);

  // This function would be called if the execution for a statment is not yet supported.
  CHECKED_STATUS ExecDdlStmt(const TreeNode *tstmt);

  // Creates a database.
  CHECKED_STATUS ExecDdlStmt(const PgTCreateDatabase *tstmt);

  // Drops a database.
  CHECKED_STATUS ExecDdlStmt(const PgTDropDatabase *tstmt);

  // Creates a schema.
  CHECKED_STATUS ExecDdlStmt(const PgTCreateSchema *tstmt);

  // Creates a table (including index table for CREATE INDEX).
  CHECKED_STATUS ExecDdlStmt(const PgTCreateTable *tstmt);

  // Drops an object in the connected database.
  CHECKED_STATUS ExecDdlStmt(const PgTDropStmt *tstmt);

  //------------------------------------------------------------------------------------------------
  // API for DML statements - Write and Read Operators.
  //------------------------------------------------------------------------------------------------
  CHECKED_STATUS ExecWriteOp(const std::shared_ptr<client::YBPgsqlWriteOp>& write_op);

  CHECKED_STATUS ExecReadOp(const std::shared_ptr<client::YBPgsqlReadOp>& read_op);

#if 0
  //------------------------------------------------------------------------------------------------
  // Result processing.
  // Aggregate all result sets from all tablet servers to form the requested resultset.
  CHECKED_STATUS AggregateResultSets();
  CHECKED_STATUS EvalCount(const std::shared_ptr<QLRowBlock>& row_block,
                           int column_index,
                           QLValue *ql_value);
  CHECKED_STATUS EvalMax(const std::shared_ptr<QLRowBlock>& row_block,
                         int column_index,
                         QLValue *ql_value);
  CHECKED_STATUS EvalMin(const std::shared_ptr<QLRowBlock>& row_block,
                         int column_index,
                         QLValue *ql_value);
  CHECKED_STATUS EvalSum(const std::shared_ptr<QLRowBlock>& row_block,
                         int column_index,
                         DataType data_type,
                         QLValue *ql_value);

  // Reset execution state.
  void Reset();
#endif

 private:
  //------------------------------------------------------------------------------------------------
  // Execution context of the last statement being executed.
  PgExecContext::SharedPtr exec_context_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PBEXEC_PG_EXECUTOR_H_

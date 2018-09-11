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
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_PG_DML_H_
#define YB_YQL_PGGATE_PG_DML_H_

#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/pg_doc_op.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// DML
//--------------------------------------------------------------------------------------------------

class PgDml : public PgStatement {
 public:
  virtual ~PgDml();

  // Append a target in SELECT or RETURNING.
  CHECKED_STATUS AppendTarget(PgExpr *target);

  // Find the column associated with the given "attr_num".
  CHECKED_STATUS FindColumn(int attr_num, PgColumn **col);

  // Prepare column for both ends.
  // - Prepare protobuf to communicate with DocDB.
  // - Prepare PgExpr to send data back to Postgres layer.
  CHECKED_STATUS PrepareColumnForRead(int attr_num, PgsqlExpressionPB *proto, const PgColumn **col);

  // Bind a column with an expression.
  CHECKED_STATUS BindColumn(int attnum, PgExpr *attr_value);

  // True if values for all partition columns are provided.
  bool PartitionIsProvided();

  // This function is not yet working and might not be needed.
  virtual CHECKED_STATUS ClearBinds();

  // Fetch a row and advance cursor to the next row.
  CHECKED_STATUS Fetch(uint64_t *values, bool *isnulls, bool *has_data);
  CHECKED_STATUS WritePgTuple(PgTuple *pg_tuple);

 protected:
  // Method members.
  // Constructor.
  PgDml(PgSession::ScopedRefPtr pg_session,
        const char *database_name,
        const char *schema_name,
        const char *table_name,
        StmtOp stmt_op);

  // Load table.
  CHECKED_STATUS LoadTable(bool for_write);

  // Allocate column protobuf.
  virtual PgsqlExpressionPB *AllocColumnBindPB(PgColumn *col) = 0;

  // Allocate column protobuf.
  virtual PgsqlExpressionPB *AllocTargetPB() = 0;

  // Update bind values.
  CHECKED_STATUS UpdateBindPBs();

  // -----------------------------------------------------------------------------------------------
  // Data members that define the DML statement.
  //
  // TODO(neil) All related information to table descriptor should be cached in the global API
  // object or some global data structures.
  client::YBTableName table_name_;
  PgTableDesc::ScopedRefPtr table_desc_;

  // Postgres targets of statements. These are either selected or returned expressions.
  std::vector<PgExpr*> targets_;

  // -----------------------------------------------------------------------------------------------
  // Data members for generated protobuf.
  // NOTE:
  // - Where clause processing data is not supported yet.
  // - Some protobuf structure are also setup in PgColumn class.

  // Column references.
  PgsqlColumnRefsPB *column_refs_ = nullptr;

  // Column associated values (expressions) to be used by DML statements.
  // - When expression are constructed, we bind them with their associated protobuf.
  // - These expressions might not yet have values for place_holders or literals.
  // - During execution, the place_holder values are updated, and the statement protobuf need to
  //   be updated accordingly.
  std::unordered_map<PgsqlExpressionPB*, PgExpr*> expr_binds_;

  // DML Operator.
  PgDocOp::SharedPtr doc_op_;

  //------------------------------------------------------------------------------------------------
  // A batch of rows in result set.
  string row_batch_;

  // Data members for navigating the output / result-set from either seleted or returned targets.
  // Cursor.
  Slice cursor_;

  // Total number of rows that have been found.
  int64_t accumulated_row_count_ = 0;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DML_H_

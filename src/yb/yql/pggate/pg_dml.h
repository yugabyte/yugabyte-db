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

#include "yb/docdb/primitive_value.h"
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
  CHECKED_STATUS PrepareColumnForRead(int attr_num, PgsqlExpressionPB *target_pb,
                                      const PgColumn **col);
  CHECKED_STATUS PrepareColumnForWrite(PgColumn *pg_col, PgsqlExpressionPB *assign_pb);

  // Bind a column with an expression.
  CHECKED_STATUS BindColumn(int attnum, PgExpr *attr_value);
  CHECKED_STATUS BindColumnCondEq(int attr_num, PgExpr *attr_value);
  CHECKED_STATUS BindColumnCondBetween(int attr_num, PgExpr *attr_value, PgExpr *attr_value_end);
  CHECKED_STATUS BindColumnCondIn(int attnum, int n_attr_values, PgExpr **attr_values);

  // Assign an expression to a column.
  CHECKED_STATUS AssignColumn(int attnum, PgExpr *attr_value);

  // This function is not yet working and might not be needed.
  virtual CHECKED_STATUS ClearBinds();

  // Fetch a row and advance cursor to the next row.
  CHECKED_STATUS Fetch(int32_t natts,
                       uint64_t *values,
                       bool *isnulls,
                       PgSysColumns *syscols,
                       bool *has_data);
  CHECKED_STATUS WritePgTuple(PgTuple *pg_tuple);

  // Build tuple id (ybctid) of the given Postgres tuple.
  Result<std::string> BuildYBTupleId(const PgAttrValueDescriptor *attrs, int32_t nattrs);

  virtual void SetCatalogCacheVersion(uint64_t catalog_cache_version) = 0;

  bool has_aggregate_targets();

 protected:
  // Method members.
  // Constructor.
  PgDml(PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id);

  // Load table.
  CHECKED_STATUS LoadTable();

  // Allocate protobuf for a SELECTed expression.
  virtual PgsqlExpressionPB *AllocTargetPB() = 0;

  // Allocate protobuf for expression whose value is bounded to a column.
  virtual PgsqlExpressionPB *AllocColumnBindPB(PgColumn *col) = 0;

  // Allocate protobuf for expression whose value is assigned to a column (SET clause).
  virtual PgsqlExpressionPB *AllocColumnAssignPB(PgColumn *col) = 0;

  // Update bind values.
  CHECKED_STATUS UpdateBindPBs();

  // Update set values.
  CHECKED_STATUS UpdateAssignPBs();

  // Indicate in the protobuf what columns must be read before the statement is processed.
  static void SetColumnRefIds(PgTableDesc::ScopedRefPtr table_desc, PgsqlColumnRefsPB *column_refs);

  // -----------------------------------------------------------------------------------------------
  // Data members that define the DML statement.
  //
  // TODO(neil) All related information to table descriptor should be cached in the global API
  // object or some global data structures.
  const PgObjectId table_id_;
  PgTableDesc::ScopedRefPtr table_desc_;

  // Postgres targets of statements. These are either selected or returned expressions.
  std::vector<PgExpr*> targets_;

  // -----------------------------------------------------------------------------------------------
  // Data members for generated protobuf.
  // NOTE:
  // - Where clause processing data is not supported yet.
  // - Some protobuf structure are also set up in PgColumn class.

  // Column associated values (expressions) to be used by DML statements.
  // - When expression are constructed, we bind them with their associated protobuf.
  // - These expressions might not yet have values for place_holders or literals.
  // - During execution, the place_holder values are updated, and the statement protobuf need to
  //   be updated accordingly.
  //
  // * Bind values are used to identify the selected rows to be operated on.
  // * Set values are used to hold columns' new values in the selected rows.
  bool ybctid_bind_ = false;
  std::unordered_map<PgsqlExpressionPB*, PgExpr*> expr_binds_;
  std::unordered_map<PgsqlExpressionPB*, PgExpr*> expr_assigns_;

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

  //------------------------------------------------------------------------------------------------
  // Hashed and range values/components used to compute the tuple id.
  //
  // These members are populated by the AddYBTupleIdColumn function and the tuple id is retrieved
  // using the GetYBTupleId function.
  //
  // These members are not used internally by the statement and are simply a utility for computing
  // the tuple id (ybctid).
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DML_H_

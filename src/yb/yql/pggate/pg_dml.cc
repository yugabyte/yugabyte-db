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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_dml.h"
#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/client/yb_op.h"

namespace yb {
namespace pggate {

using namespace std::literals;  // NOLINT

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;
using client::YBTable;
using client::YBTableName;
using client::YBTableType;
using client::YBPgsqlWriteOp;

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

//--------------------------------------------------------------------------------------------------
// PgDml
//--------------------------------------------------------------------------------------------------
PgDml::PgDml(PgSession::ScopedRefPtr pg_session,
             const char *database_name,
             const char *schema_name,
             const char *table_name,
             StmtOp stmt_op)
    : PgStatement(std::move(pg_session), stmt_op),
      table_name_(database_name, table_name) {
}

PgDml::~PgDml() {
}

Status PgDml::LoadTable(bool for_write) {
  auto result = pg_session_->LoadTable(table_name_, for_write);
  RETURN_NOT_OK(result);
  table_desc_ = *result;
  return Status::OK();
}

Status PgDml::ClearBinds() {
  return STATUS(NotSupported, "Clearing binds for prepared statement is not yet implemented");
}

Status PgDml::FindColumn(int attr_num, PgColumn **col) {
  auto result = table_desc_->FindColumn(attr_num);
  RETURN_NOT_OK(result);
  *col = *result;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgDml::AppendTarget(PgExpr *target) {
  // Append to targets_.
  targets_.push_back(target);

  // Allocate associated protobuf.
  PgsqlExpressionPB *expr_pb = AllocTargetPB();

  // Prepare expression. Except for constants and place_holders, all other expressions can be
  // evaluate just one time during prepare.
  RETURN_NOT_OK(target->Prepare(this, expr_pb));

  // Link the given expression "attr_value" with the allocated protobuf. Note that except for
  // constants and place_holders, all other expressions can be setup just one time during prepare.
  // Example:
  // - Bind values for a target of SELECT
  //   SELECT AVG(col + ?) FROM a_table;
  expr_binds_[expr_pb] = target;
  return Status::OK();
}

Status PgDml::PrepareColumnForRead(int attr_num, PgsqlExpressionPB *target_pb,
                                   const PgColumn **col) {
  *col = nullptr;
  PgColumn *pg_col;
  RETURN_NOT_OK(FindColumn(attr_num, &pg_col));
  *col = pg_col;

  // Prepare protobuf to send to DocDB.
  target_pb->set_column_id(pg_col->id());

  // Make sure that ProtoBuf has only one entry per column ID instead of one per reference.
  //   SELECT col, col, col FROM a_table;
  if (!pg_col->read_requested()) {
    pg_col->set_read_requested(true);
    column_refs_->add_ids(pg_col->id());
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgDml::BindColumn(int attr_num, PgExpr *attr_value) {
  // Find column.
  PgColumn *col;
  RETURN_NOT_OK(FindColumn(attr_num, &col));

  // Check datatype.
  CHECK_EQ(col->internal_type(), attr_value->internal_type());

  // Alloc the protobuf.
  PgsqlExpressionPB *bind_pb = col->bind_pb();
  if (bind_pb == nullptr) {
    bind_pb = AllocColumnBindPB(col);
    bind_pb = col->bind_pb();
  } else {
    if (expr_binds_.find(bind_pb) != expr_binds_.end()) {
      return STATUS_SUBSTITUTE(InvalidArgument,
                               "Column $0 is already bound to another value", attr_num);
    }
  }

  // Link the expression and protobuf. During execution, expr will write result to the pb.
  RETURN_NOT_OK(attr_value->Prepare(this, bind_pb));

  // Link the given expression "attr_value" with the allocated protobuf. Note that except for
  // constants and place_holders, all other expressions can be setup just one time during prepare.
  // Examples:
  // - Bind values for primary columns in where clause.
  //     WHERE hash = ?
  // - Bind values for a column in INSERT statement.
  //     INSERT INTO a_table(hash, key, col) VALUES(?, ?, ?)
  expr_binds_[bind_pb] = attr_value;
  return Status::OK();
}

bool PgDml::PartitionIsProvided() {
  bool has_partition_columns = false;
  bool miss_partition_columns = false;
  for (PgColumn &col : table_desc_->columns()) {
    if (col.desc()->is_partition()) {
      if (expr_binds_.find(col.bind_pb()) == expr_binds_.end()) {
        miss_partition_columns = true;
      } else {
        has_partition_columns = true;
      }
    }
  }

  DCHECK(!has_partition_columns || !miss_partition_columns) << "Partition is not fully specified";
  return has_partition_columns;
}

//--------------------------------------------------------------------------------------------------

Status PgDml::UpdateBindPBs() {
  // Process the column binds for two cases.
  // For performance reasons, we might evaluate these expressions together with bind values in YB.
  for (const auto &entry : expr_binds_) {
    PgsqlExpressionPB *expr_pb = entry.first;
    PgExpr *attr_value = entry.second;
    RETURN_NOT_OK(attr_value->Eval(this, expr_pb));
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgDml::Fetch(uint64_t *values, bool *isnulls, bool *has_data) {

  // Each isnulls and values correspond (in order) to columns from the table schema.
  // Initialize to nulls for any columns not present in result.
  memset(isnulls, true, table_desc_->num_columns() * sizeof(bool));

  // Load data from cache in doc_op_ to cursor_ if it is not pointing to any data.
  if (cursor_.empty()) {
    int64_t row_count = 0;
    // Keep reading untill we either reach the end or get some rows.
    while (row_count == 0) {
      if (doc_op_->EndOfResult()) {
        // To be compatible with Postgres code, memset output array with 0.
        *has_data = false;
        return Status::OK();
      }

      // Read from cache.
      RETURN_NOT_OK(doc_op_->GetResult(&row_batch_));
      RETURN_NOT_OK(PgDocData::LoadCache(row_batch_, &row_count, &cursor_));
    }

    accumulated_row_count_ += row_count;
  }

  // Read the tuple from cached buffer and write it to postgres buffer.
  *has_data = true;
  PgTuple pg_tuple(values, isnulls);
  RETURN_NOT_OK(WritePgTuple(&pg_tuple));

  return Status::OK();
}

Status PgDml::WritePgTuple(PgTuple *pg_tuple) {
  for (const PgExpr *target : targets_) {
    if (target->op() != PgColumnRef::Opcode::PG_EXPR_COLREF) {
      return STATUS(InternalError, "Unexpected expression, only column refs supported here");
    }
    const auto *col_ref = static_cast<const PgColumnRef *>(target);
    PgWireDataHeader header = PgDocData::ReadDataHeader(&cursor_);
    CHECK(target->TranslateData) << "Data format translation is not provided";
    target->TranslateData(&cursor_, header, pg_tuple, col_ref->attr_num() - 1);
  }

  return Status::OK();
}

}  // namespace pggate
}  // namespace yb

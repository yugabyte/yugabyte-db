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

#include "yb/yql/pggate/pg_select.h"
#include "yb/client/yb_op.h"

namespace yb {
namespace pggate {

using std::make_shared;

//--------------------------------------------------------------------------------------------------
// PgSelect
//--------------------------------------------------------------------------------------------------

PgSelect::PgSelect(PgSession::ScopedRefPtr pg_session,
                   const char *database_name,
                   const char *schema_name,
                   const char *table_name)
    : PgDml(std::move(pg_session), database_name, schema_name, table_name, StmtOp::STMT_SELECT) {
}

PgSelect::~PgSelect() {
}

Status PgSelect::Prepare() {
  RETURN_NOT_OK(LoadTable(false /* for_write */));

  // Allocate SELECT request.
  read_op_.reset(table_->NewPgsqlSelect());
  read_req_ = read_op_->mutable_request();
  PrepareBinds();

  return Status::OK();
}

void PgSelect::PrepareBinds() {
  column_refs_ = read_req_->mutable_column_refs();

  // When reading, only values of partition column may present.
  for (ColumnDesc col : col_descs_) {
    if (col.is_partition()) {
      primary_exprs_[col.id()] = read_req_->add_partition_column_values();
    }
  }
}

//--------------------------------------------------------------------------------------------------
// DML support.
// TODO(neil) WHERE clause is not yet supported. Revisit this function when it is.

PgsqlExpressionPB *PgSelect::AllocColumnExprPB(int attr_num) {
  for (ColumnDesc col : col_descs_) {
    if (col.attr_num() == attr_num) {
      // Return the reserved space for the column in the partition key.
      if (col.is_partition()) {
        DCHECK(primary_exprs_.find(col.id()) != primary_exprs_.end());
        return primary_exprs_[col.id()];
      }
      break;
    }
  }

  LOG(FATAL) << "Only valid partition attr_num is allowed (" << attr_num << ") ";
  return nullptr;
}

//--------------------------------------------------------------------------------------------------
// RESULT SET SUPPORT.
// For now, selected expressions are just a list of column names (ref).
//   SELECT column_l, column_m, column_n FROM ...

PgsqlExpressionPB *PgSelect::AllocSelectedExprPB() {
  return read_req_->add_selected_exprs();
}

Status PgSelect::AddSelectedColumnPB(int attr_num) {
  PgsqlExpressionPB *sel_expr = AllocSelectedExprPB();
  for (ColumnDesc col : col_descs_) {
    if (col.attr_num() == attr_num) {
      sel_expr->set_column_id(col.id());
      column_refs_->add_ids(col.id());
      return Status::OK();
    }
  }

  return STATUS_FORMAT(InvalidArgument, "Invalid attr_num ($0) is selected", attr_num);
}

Status PgSelect::BindExprInt2(int attr_num, int16_t *attr_value) {
  // Tell DocDB we are selecting column that are associated with 'attr_num'.
  // TODO(neil) when we extend selected column into selected expression, update this code.
  RETURN_NOT_OK(AddSelectedColumnPB(attr_num));

  // Set the bind to write data into Postgres buffer.
  pg_binds_.push_back(make_shared<PgBindInt16>(attr_value));
  return Status::OK();
}

Status PgSelect::BindExprInt4(int attr_num, int32_t *attr_value) {
  // Tell DocDB we are selecting column that are associated with 'attr_num'.
  // TODO(neil) when we extend selected column into selected expression, update this code.
  RETURN_NOT_OK(AddSelectedColumnPB(attr_num));

  // Set the bind to write data into Postgres buffer.
  pg_binds_.push_back(make_shared<PgBindInt32>(attr_value));
  return Status::OK();
}

Status PgSelect::BindExprInt8(int attr_num, int64_t *attr_value) {
  // Tell DocDB we are selecting column that are associated with 'attr_num'.
  // TODO(neil) when we extend selected column into selected expression, update this code.
  RETURN_NOT_OK(AddSelectedColumnPB(attr_num));

  // Set the bind to write data into Postgres buffer.
  pg_binds_.push_back(make_shared<PgBindInt64>(attr_value));
  return Status::OK();
}

Status PgSelect::BindExprFloat4(int attr_num, float *attr_value) {
  // Tell DocDB we are selecting column that are associated with 'attr_num'.
  // TODO(neil) when we extend selected column into selected expression, update this code.
  RETURN_NOT_OK(AddSelectedColumnPB(attr_num));

  // Set the bind to write data into Postgres buffer.
  pg_binds_.push_back(make_shared<PgBindFloat>(attr_value));
  return Status::OK();
}

Status PgSelect::BindExprFloat8(int attr_num, double *attr_value) {
  // Tell DocDB we are selecting column that are associated with 'attr_num'.
  // TODO(neil) when we extend selected column into selected expression, update this code.
  RETURN_NOT_OK(AddSelectedColumnPB(attr_num));

  // Set the bind to write data into Postgres buffer.
  pg_binds_.push_back(make_shared<PgBindDouble>(attr_value));
  return Status::OK();
}

Status PgSelect::BindExprText(int attr_num, char *attr_value, int64_t *attr_bytes) {
  // Tell DocDB we are selecting column that are associated with 'attr_num'.
  // TODO(neil) when we extend selected column into selected expression, update this code.
  RETURN_NOT_OK(AddSelectedColumnPB(attr_num));

  // Set the bind to write data into Postgres buffer.
  pg_binds_.push_back(make_shared<PgBindText>(attr_value, attr_bytes, true /* allow_truncate */));
  return Status::OK();
}

Status PgSelect::BindExprSerializedData(int attr_num, char *attr_value, int64_t *attr_bytes) {
  return STATUS(NotSupported, "Setting serialized values is not yet supported");
}

Status PgSelect::Exec() {
  // TODO(neil) The following code is a simple read and cache. It operates once and done.
  // - This will be extended to do scanning and caching chunk by chunk.
  // - "result_set_" field need to be locked and release. Available rows are fetched from the
  //   beginning while the arriving rows are append at the end.
  RETURN_NOT_OK(pg_session_->Apply(read_op_));

  // Append the data and wait for the fetch request.
  result_set_.push_back(read_op_->rows_data());
  if (cursor_.empty()) {
    RETURN_NOT_OK(PgNetReader::ReadNewTuples(result_set_.front(), &total_row_count_, &cursor_));
  }

  return Status::OK();
}

Status PgSelect::Fetch(int64_t *row_count) {
  // Deserialize next row.
  if (cursor_.empty()) {
    if (result_set_.size() <= 1) {
      result_set_.clear();
      *row_count = 0;
      return Status::OK();
    } else {
      result_set_.pop_front();
      RETURN_NOT_OK(PgNetReader::ReadNewTuples(result_set_.front(), &total_row_count_, &cursor_));
    }
  }

  *row_count = 1;
  return PgNetReader::ReadTuple(pg_binds_, &cursor_);
}

}  // namespace pggate
}  // namespace yb

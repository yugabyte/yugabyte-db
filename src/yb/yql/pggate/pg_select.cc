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
#include "yb/yql/pggate/util/pg_doc_data.h"
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

  // Allocate READ/SELECT operation.
  auto doc_op = make_shared<PgDocReadOp>(pg_session_, table_desc_->NewPgsqlSelect());
  read_req_ = doc_op->read_op()->mutable_request();
  PrepareColumns();

  // Preparation complete.
  doc_op_ = doc_op;
  return Status::OK();
}

void PgSelect::PrepareColumns() {
  // Setting protobuf.
  column_refs_ = read_req_->mutable_column_refs();

  // When reading, only values of partition columns are special-cased in protobuf.
  // Because Kudu API requires that partition columns must be listed in their created-order, the
  // slots for partition column bind expressions are allocated here in correct order.
  for (PgColumn &col : table_desc_->columns()) {
    col.AllocPrimaryBindPB(read_req_);
  }
}

//--------------------------------------------------------------------------------------------------
// DML support.
// TODO(neil) WHERE clause is not yet supported. Revisit this function when it is.

PgsqlExpressionPB *PgSelect::AllocColumnBindPB(PgColumn *col) {
  return col->AllocBindPB(read_req_);
}

PgsqlExpressionPB *PgSelect::AllocTargetPB() {
  return read_req_->add_targets();
}

//--------------------------------------------------------------------------------------------------
// RESULT SET SUPPORT.
// For now, selected expressions are just a list of column names (ref).
//   SELECT column_l, column_m, column_n FROM ...

Status PgSelect::DeleteEmptyPrimaryBinds() {
  bool miss_range_columns = false;
  bool has_range_columns = false;

  bool miss_partition_columns = false;
  bool has_partition_columns = false;

  for (PgColumn &col : table_desc_->columns()) {
    if (col.desc()->is_partition()) {
      if (expr_binds_.find(col.bind_pb()) == expr_binds_.end()) {
        miss_partition_columns = true;
      } else {
        has_partition_columns = true;
      }
    } else if (col.desc()->is_primary()) {
      if (expr_binds_.find(col.bind_pb()) == expr_binds_.end()) {
        miss_range_columns = true;
      } else {
        has_range_columns = true;
      }
    }
  }

  if (miss_partition_columns) {
    VLOG(1) << "Full scan is needed";
    read_req_->clear_partition_column_values();
    read_req_->clear_range_column_values();
  } else if (miss_range_columns) {
    VLOG(1) << "Single tablet scan is needed";
    read_req_->clear_range_column_values();
  }

  // Set the primary key indicator in protobuf.
  if (has_partition_columns && miss_partition_columns) {
    return STATUS(InvalidArgument, "Partition key must be fully specified");
  }
  if (has_range_columns && miss_range_columns) {
    return STATUS(InvalidArgument, "Range key must be fully specified");
  }
  return Status::OK();
}

Status PgSelect::Exec() {
  // Delete key columns that are not bound to any values.
  RETURN_NOT_OK(DeleteEmptyPrimaryBinds());

  // Update bind values for constants and placeholders.
  RETURN_NOT_OK(UpdateBindPBs());

  // Execute select statement asynchronously.
  return doc_op_->Execute();
}

}  // namespace pggate
}  // namespace yb

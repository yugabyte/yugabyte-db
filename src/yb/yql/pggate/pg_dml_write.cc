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

#include "yb/yql/pggate/pg_dml_write.h"
#include "yb/client/yb_op.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
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
// PgDmlWrite
//--------------------------------------------------------------------------------------------------

PgDmlWrite::PgDmlWrite(PgSession::SharedPtr pg_session,
                       const char *database_name,
                       const char *schema_name,
                       const char *table_name,
                       StmtOp stmt_op)
  : PgDml(pg_session, database_name, schema_name, table_name, stmt_op) {
}

PgDmlWrite::~PgDmlWrite() {
}

Status PgDmlWrite::Prepare() {
  RETURN_NOT_OK(LoadTable(true /* for_write */));

  // Allocate either INSERT, UPDATE, or DELETE request.
  AllocWriteRequest();
  PrepareBinds();
  return Status::OK();
}

void PgDmlWrite::PrepareBinds() {
  column_refs_ = write_req_->mutable_column_refs();

  // Allocate primary column expressions.
  for (ColumnDesc col : col_descs_) {
    if (col.is_partition()) {
      primary_exprs_[col.id()] = write_req_->add_partition_column_values();
    } else if (col.is_primary()) {
      primary_exprs_[col.id()] = write_req_->add_range_column_values();
    }
  }
}

Status PgDmlWrite::Exec() {
  return pg_session_->Apply(write_op_);
}

PgsqlExpressionPB *PgDmlWrite::AllocColumnExprPB(int attr_num) {
  for (ColumnDesc col : col_descs_) {
    if (col.attr_num() == attr_num) {
      if (col.is_partition() || col.is_primary()) {
        // Return the reserved space for the value / expression.
        DCHECK(primary_exprs_.find(col.id()) != primary_exprs_.end());
        return primary_exprs_[col.id()];
      }

      PgsqlColumnValuePB* col_pb = write_req_->add_column_values();
      col_pb->set_column_id(col.id());
      return col_pb->mutable_expr();
    }
  }

  LOG(FATAL) << "Invalid attr_num (" << attr_num << ") was passed to PGGate";
  return nullptr;
}

}  // namespace pggate
}  // namespace yb

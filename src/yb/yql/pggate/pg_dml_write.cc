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

PgDmlWrite::PgDmlWrite(PgSession::ScopedRefPtr pg_session,
                       const char *database_name,
                       const char *schema_name,
                       const char *table_name,
                       StmtOp stmt_op)
  : PgDml(std::move(pg_session), database_name, schema_name, table_name, stmt_op) {
}

PgDmlWrite::~PgDmlWrite() {
}

Status PgDmlWrite::Prepare() {
  RETURN_NOT_OK(LoadTable(true /* for_write */));

  // Allocate either INSERT, UPDATE, or DELETE request.
  AllocWriteRequest();
  PrepareColumns();
  return Status::OK();
}

void PgDmlWrite::PrepareColumns() {
  // Setting protobuf.
  column_refs_ = write_req_->mutable_column_refs();

  // Because Kudu API requires that primary columns must be listed in their created-order, the slots
  // for primary column bind expressions are allocated here in correct order.
  for (PgColumn &col : table_desc_->columns()) {
    col.AllocPrimaryBindPB(write_req_);
  }
}

Status PgDmlWrite::Exec() {
  // First update protobuf with new bind values.
  RETURN_NOT_OK(UpdateBindPBs());

  // Execute the statement.
  RETURN_NOT_OK(doc_op_->Execute());

  // Execute the statement.
  return doc_op_->GetResult(&row_batch_);
}

PgsqlExpressionPB *PgDmlWrite::AllocColumnBindPB(PgColumn *col) {
  return col->AllocBindPB(write_req_);
}

PgsqlExpressionPB *PgDmlWrite::AllocTargetPB() {
  return write_req_->add_targets();
}

}  // namespace pggate
}  // namespace yb

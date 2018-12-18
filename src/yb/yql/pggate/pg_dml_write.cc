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

Status PgDmlWrite::DeleteEmptyPrimaryBinds() {
  // Iterate primary-key columns and remove the binds without values.
  bool missing_primary_key = false;

  // Either ybctid or primary key must be present.
  if (ybctid_bind_.empty()) {
    // Remove empty binds from partition list.
    auto partition_iter = write_req_->mutable_partition_column_values()->begin();
    while (partition_iter != write_req_->mutable_partition_column_values()->end()) {
      if (expr_binds_.find(&*partition_iter) == expr_binds_.end()) {
        missing_primary_key = true;
        partition_iter = write_req_->mutable_partition_column_values()->erase(partition_iter);
      } else {
        partition_iter++;
      }
    }

    // Remove empty binds from range list.
    auto range_iter = write_req_->mutable_range_column_values()->begin();
    while (range_iter != write_req_->mutable_range_column_values()->end()) {
      if (expr_binds_.find(&*range_iter) == expr_binds_.end()) {
        missing_primary_key = true;
        range_iter = write_req_->mutable_range_column_values()->erase(range_iter);
      } else {
        range_iter++;
      }
    }
  } else {
    uint16_t hash_value;
    RETURN_NOT_OK(PgExpr::ReadHashValue(ybctid_bind_.data(), ybctid_bind_.size(), &hash_value));
    write_req_->set_hash_code(hash_value);
    write_req_->clear_partition_column_values();
    write_req_->clear_range_column_values();
  }

  if (missing_primary_key) {
    return STATUS(InvalidArgument, "Primary key must be fully specified for modifying table");
  }

  return Status::OK();
}

Status PgDmlWrite::Exec() {
  // Delete allocated binds that are not associated with a value.
  // YBClient interface enforce us to allocate binds for primary key columns in their indexing
  // order, so we have to allocate these binds before associating them with values. When the values
  // are not assigned, these allocated binds must be deleted.
  RETURN_NOT_OK(DeleteEmptyPrimaryBinds());

  // First update protobuf with new bind values.
  RETURN_NOT_OK(UpdateBindPBs());

  if (write_req_->has_ybctid_column_value()) {
    PgsqlExpressionPB *exprpb = write_req_->mutable_ybctid_column_value();
    CHECK(exprpb->has_value() && exprpb->value().has_binary_value())
      << "YBCTID must be of BINARY datatype";
  }

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

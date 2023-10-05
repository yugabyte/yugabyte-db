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

#include "yb/gutil/casts.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

using client::YBSession;
using client::YBMetaDataCache;
using client::YBTable;
using client::YBTableName;
using client::YBPgsqlWriteOp;

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

//--------------------------------------------------------------------------------------------------
// PgDmlWrite
//--------------------------------------------------------------------------------------------------

PgDmlWrite::PgDmlWrite(PgSession::ScopedRefPtr pg_session,
                       const PgObjectId& table_id,
                       bool is_region_local,
                       YBCPgTransactionSetting transaction_setting)
    : PgDml(std::move(pg_session), table_id, is_region_local),
      transaction_setting_(transaction_setting) {
}

PgDmlWrite::~PgDmlWrite() {
}

Status PgDmlWrite::Prepare() {
  // Setup descriptors for target and bind columns.
  target_ = bind_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(table_id_)));

  // Allocate either INSERT, UPDATE, DELETE, or TRUNCATE_COLOCATED request.
  AllocWriteRequest();
  PrepareColumns();
  return Status::OK();
}

void PgDmlWrite::PrepareColumns() {
  // Because DocDB API requires that primary columns must be listed in their created-order,
  // the slots for primary column bind expressions are allocated here in correct order.
  for (auto& col : target_.columns()) {
    col.AllocPrimaryBindPB(write_req_.get());
  }
}

Status PgDmlWrite::DeleteEmptyPrimaryBinds() {
  // Iterate primary-key columns and remove the binds without values.
  bool missing_primary_key = false;

  // Either ybctid or primary key must be present.
  if (!ybctid_bind_) {
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
    write_req_->mutable_partition_column_values()->clear();
    write_req_->mutable_range_column_values()->clear();
  }

  // Check for missing key.  This is okay when binding the whole table (for colocated truncate).
  if (missing_primary_key && !bind_table_) {
    return STATUS(InvalidArgument, "Primary key must be fully specified for modifying table");
  }

  return Status::OK();
}

Status PgDmlWrite::Exec(bool force_non_bufferable) {

  // Delete allocated binds that are not associated with a value.
  // YBClient interface enforce us to allocate binds for primary key columns in their indexing
  // order, so we have to allocate these binds before associating them with values. When the values
  // are not assigned, these allocated binds must be deleted.
  RETURN_NOT_OK(DeleteEmptyPrimaryBinds());

  // First update protobuf with new bind values.
  RETURN_NOT_OK(UpdateBindPBs());
  RETURN_NOT_OK(UpdateAssignPBs());

  if (write_req_->has_ybctid_column_value()) {
    auto* exprpb = write_req_->mutable_ybctid_column_value();
    CHECK(exprpb->has_value() && exprpb->value().has_binary_value())
      << "YBCTID must be of BINARY datatype";
  }

  // Initialize doc operator.
  RETURN_NOT_OK(doc_op_->ExecuteInit(nullptr));

  // Set column references in protobuf.
  ColRefsToPB();
  // Compatibility: set column ids as expected by legacy nodes
  ColumnRefsToPB(write_req_->mutable_column_refs());

  // Execute the statement. If the request has been sent, get the result and handle any rows
  // returned.
  if (VERIFY_RESULT(doc_op_->Execute(
          force_non_bufferable ||
          (transaction_setting_ == YB_SINGLE_SHARD_TRANSACTION))) == RequestSent::kTrue) {
    RETURN_NOT_OK(doc_op_->GetResult(&rowsets_));

    // Save the number of rows affected by the op.
    rows_affected_count_ = VERIFY_RESULT(doc_op_->GetRowsAffectedCount());
  }

  return Status::OK();
}

Status PgDmlWrite::SetWriteTime(const HybridTime& write_time) {
  SCHECK(doc_op_.get() != nullptr, RuntimeError, "expected doc_op_ to be initialized");
  down_cast<PgDocWriteOp*>(doc_op_.get())->SetWriteTime(write_time);
  return Status::OK();
}

void PgDmlWrite::AllocWriteRequest() {
  auto write_op = ArenaMakeShared<PgsqlWriteOp>(
      arena_ptr(), &arena(),
      /* need_transaction */
      (transaction_setting_ == YBCPgTransactionSetting::YB_TRANSACTIONAL),
      is_region_local_);

  write_req_ = std::shared_ptr<LWPgsqlWriteRequestPB>(write_op, &write_op->write_request());
  write_req_->set_stmt_type(stmt_type());
  write_req_->set_client(YQL_CLIENT_PGSQL);
  write_req_->dup_table_id(table_id_.GetYbTableId());
  write_req_->set_schema_version(target_->schema_version());
  write_req_->set_stmt_id(reinterpret_cast<uint64_t>(write_req_.get()));

  doc_op_ = std::make_shared<PgDocWriteOp>(pg_session_, &target_, std::move(write_op));
}

LWPgsqlExpressionPB *PgDmlWrite::AllocColumnBindPB(PgColumn *col) {
  return col->AllocBindPB(write_req_.get());
}

LWPgsqlExpressionPB *PgDmlWrite::AllocColumnAssignPB(PgColumn *col) {
  return col->AllocAssignPB(write_req_.get());
}

LWPgsqlExpressionPB *PgDmlWrite::AllocTargetPB() {
  return write_req_->add_targets();
}

LWPgsqlExpressionPB *PgDmlWrite::AllocQualPB() {
  LOG(FATAL) << "Pure virtual function is being called";
  return nullptr;
}

LWPgsqlColRefPB *PgDmlWrite::AllocColRefPB() {
  return write_req_->add_col_refs();
}

void PgDmlWrite::ClearColRefPBs() {
  write_req_->mutable_col_refs()->clear();
}

}  // namespace pggate
}  // namespace yb

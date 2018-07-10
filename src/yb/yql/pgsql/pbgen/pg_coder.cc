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

#include "yb/yql/pgsql/pbgen/pg_coder.h"

#include "yb/util/logging.h"
#include "yb/client/client.h"
#include "yb/client/callbacks.h"
#include "yb/client/yb_op.h"
#include "yb/util/decimal.h"

namespace yb {
namespace pgsql {

using std::string;
using std::shared_ptr;

using client::YBColumnSpec;
using client::YBSchema;
using client::YBTable;
using client::YBTableAlterer;
using client::YBTableType;
using client::YBTableName;
using client::YBPgsqlWriteOp;
using client::YBPgsqlReadOp;
using strings::Substitute;

//--------------------------------------------------------------------------------------------------

PgCoder::PgCoder() {
}

PgCoder::~PgCoder() {
}

void PgCoder::Reset() {
  pg_proto_ = nullptr;
}

CHECKED_STATUS PgCoder::Generate(const PgCompileContext::SharedPtr& compile_context,
                                 PgProto::SharedPtr *pg_proto) {
  *pg_proto = nullptr;

  // Keep the context and code in the PgCoder.
  compile_context_ = compile_context;
  pg_proto_ = std::make_shared<PgProto>(compile_context->stmt());

  // Process it.
  RETURN_NOT_OK(TRootToPB(compile_context_->tree_root()));

  *pg_proto = pg_proto_;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgCoder::TRootToPB(const TreeNode *root_node) {
  if (root_node == nullptr) {
    return Status::OK();
  }

  switch (root_node->opcode()) {
    case TreeNodeOpcode::kPgTCreateDatabase: FALLTHROUGH_INTENDED;
    case TreeNodeOpcode::kPgTDropDatabase: FALLTHROUGH_INTENDED;
    case TreeNodeOpcode::kPgTCreateTable: FALLTHROUGH_INTENDED;
    case TreeNodeOpcode::kPgTCreateSchema: FALLTHROUGH_INTENDED;
    case TreeNodeOpcode::kPgTDropStmt:
      pg_proto_->SetDdlStmt(compile_context_->parse_tree(), root_node);
      return Status::OK();

    case TreeNodeOpcode::kPgTInsertStmt:
      return TStmtToPB(static_cast<const PgTInsertStmt *>(root_node));

    case TreeNodeOpcode::kPgTDeleteStmt:
      return TStmtToPB(static_cast<const PgTDeleteStmt *>(root_node));

    case TreeNodeOpcode::kPgTUpdateStmt:
      return TStmtToPB(static_cast<const PgTUpdateStmt *>(root_node));

    case TreeNodeOpcode::kPgTSelectStmt:
      return TStmtToPB(static_cast<const PgTSelectStmt *>(root_node));

    default:
      return compile_context_->Error(root_node, ErrorCode::FEATURE_NOT_SUPPORTED);
  }
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgCoder::TStmtToPB(const PgTInsertStmt *tstmt) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tstmt->table();
  shared_ptr<YBPgsqlWriteOp> insert_op(table->NewPgsqlInsert());
  PgsqlWriteRequestPB *req = insert_op->mutable_request();

  // Set the values for columns.
  Status s = ColumnArgsToPB(table, tstmt, req);
  if (PREDICT_FALSE(!s.ok())) {
    return compile_context_->Error(tstmt, s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  if (tstmt->has_column_refs()) {
    s = ColumnRefsToPB(tstmt, req->mutable_column_refs());
    if (PREDICT_FALSE(!s.ok())) {
      return compile_context_->Error(tstmt, s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Save it to proto code.
  pg_proto_->SetDmlWriteOp(insert_op);

  return Status::OK();
}

#if 0
//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgCoder::TStmtToPB(const PgTDeleteStmt *tstmt) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tstmt->table();
  shared_ptr<YBPgsqlWriteOp> delete_op(table->NewPgsqlDelete());
  PgsqlWriteRequestPB *req = delete_op->mutable_request();

  // Set the timestamp
  RETURN_NOT_OK(TimestampToPB(tstmt, req));

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  Status s = WhereClauseToPB(req, tstmt->key_where_ops(), tstmt->where_ops(),
                             tstmt->subscripted_col_where_ops());
  if (PREDICT_FALSE(!s.ok())) {
    return compile_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  s = ColumnRefsToPB(tstmt, req->mutable_column_refs());
  if (PREDICT_FALSE(!s.ok())) {
    return compile_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }
  s = ColumnArgsToPB(table, tstmt, req);
  if (PREDICT_FALSE(!s.ok())) {
    return compile_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the IF clause.
  if (tstmt->if_clause() != nullptr) {
    s = PTExprToPB(tstmt->if_clause(), delete_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return compile_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Apply the operator.
  return compile_context_->Apply(delete_op);
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgCoder::TStmtToPB(const PgTUpdateStmt *tstmt) {
  // Create write request.
  const shared_ptr<client::YBTable>& table = tstmt->table();
  shared_ptr<YBPgsqlWriteOp> update_op(table->NewPgsqlUpdate());
  PgsqlWriteRequestPB *req = update_op->mutable_request();

  // Where clause - Hash, range, and regular columns.
  // NOTE: Currently, where clause for write op doesn't allow regular columns.
  Status s = WhereClauseToPB(req, tstmt->key_where_ops(), tstmt->where_ops(),
      tstmt->subscripted_col_where_ops());
  if (PREDICT_FALSE(!s.ok())) {
    return compile_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Set the ttl
  RETURN_NOT_OK(TtlToPB(tstmt, req));

  // Set the timestamp
  RETURN_NOT_OK(TimestampToPB(tstmt, req));

  // Setup the columns' new values.
  s = ColumnArgsToPB(table, tstmt, update_op->mutable_request());
  if (PREDICT_FALSE(!s.ok())) {
    return compile_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
  }

  // Setup the column values that need to be read.
  if (tstmt->has_column_refs()) {
    s = ColumnRefsToPB(tstmt, req->mutable_column_refs());
    if (PREDICT_FALSE(!s.ok())) {
      return compile_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Set the IF clause.
  if (tstmt->if_clause() != nullptr) {
    s = PTExprToPB(tstmt->if_clause(), update_op->mutable_request()->mutable_if_expr());
    if (PREDICT_FALSE(!s.ok())) {
      return compile_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Apply the operator.
  return compile_context_->Apply(update_op);
}
#endif
//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgCoder::TStmtToPB(const PgTSelectStmt *tstmt) {
  const shared_ptr<client::YBTable>& table = tstmt->table();
  if (table == nullptr) {
    // If a system table does not exist, ignore it.
    if (tstmt->is_system()) {
      return Status::OK();
    }
    LOG(FATAL) << "Unexpected code path. Analyzer should have raised error but did not";
  }

  // Create the read request.
  shared_ptr<YBPgsqlReadOp> select_op(table->NewPgsqlSelect());
  PgsqlReadRequestPB *req = select_op->mutable_request();

  // TODO(neil) Once PostgreSQL partition is specified, this code must be updated.
  // First set the internal partition for the "oid" column.
  const MCVector<ColumnArg>& column_args = tstmt->column_args();
  for (const ColumnArg& col : column_args) {
    PgsqlExpressionPB *col_pb = req->add_partition_column_values();
    const ColumnDesc *col_desc = col.desc();
    col_pb->set_column_id(col_desc->id());
    RETURN_NOT_OK(TExprToPB(col.expr(), col_pb));
  }

#if 0
  // TODO(neil)
  // - Process where clause properly as soon as DocDB layer is working.
  // - Fake a where clause for now. To save time, don't implement details yet because we need to
  //   find out what is needed during execution phase first before constructing data here.
  RETURN_NOT_OK(WhereClauseToPB(req, tstmt->key_where_ops(), tstmt->where_ops(),
                                tstmt->partition_key_ops(), tstmt->func_ops(), &no_results));
#endif

  // Specify selected list by adding the expressions to selected_exprs in read request.
  PgsqlRSRowDescPB *rsrow_desc_pb = req->mutable_rsrow_desc();
  for (const auto& expr : tstmt->selected_exprs()) {
    if (expr->opcode() == TreeNodeOpcode::kPgTAllColumns) {
      RETURN_NOT_OK(TExprToPB(static_cast<const PgTAllColumns*>(expr.get()), req));
    } else {
      RETURN_NOT_OK(TExprToPB(expr, req->add_selected_exprs()));

      // Add the expression metadata (rsrow descriptor).
      PgsqlRSColDescPB *rscol_desc_pb = rsrow_desc_pb->add_rscol_descs();
      rscol_desc_pb->set_name(expr->QLName());
      expr->ql_type()->ToQLTypePB(rscol_desc_pb->mutable_ql_type());
    }
  }

  // Setup the column values that need to be read.
  RETURN_NOT_OK(ColumnRefsToPB(tstmt, req->mutable_column_refs()));

  // Save it to proto code.
  pg_proto_->SetDmlReadOp(select_op);
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb

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

#include "yb/yql/pggate/pg_insert.h"
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
// PgInsert
//--------------------------------------------------------------------------------------------------

PgInsert::PgInsert(PgSession::SharedPtr pg_session,
                   const char *database_name,
                   const char *schema_name,
                   const char *table_name)
    : PgStatement(pg_session, StmtOp::STMT_INSERT),
      table_name_(database_name, table_name) {
}

PgInsert::~PgInsert() {
}

Status PgInsert::Prepare() {
  RETURN_NOT_OK(pg_session_->LoadTable(table_name_, true, &table_, &col_descs_, &key_col_count_,
                                       &partition_col_count_));
  op_.reset(table_->NewPgsqlInsert());
  req_ = op_->mutable_request();

  col_values_.resize(col_descs_.size());
  for (ColumnDesc col : col_descs_) {
    if (col.is_partition()) {
      col_values_[col.id()] = req_->add_partition_column_values();
    } else if (col.is_primary()) {
      col_values_[col.id()] = req_->add_range_column_values();
    } else {
      col_values_[col.id()] = nullptr;
    }
  }
  return Status::OK();
}

PgsqlExpressionPB *PgInsert::AllocColumnPB(int attr_num) {
  for (ColumnDesc col : col_descs_) {
    if (col.attr_num() == attr_num) {
      if (!col.is_partition() && !col.is_primary()) {
        PgsqlColumnValuePB* col_pb = req_->add_column_values();
        col_pb->set_column_id(col.id());
        col_values_[col.id()] = col_pb->mutable_expr();
      }

      // Return the reserved space for the value / expression.
      return col_values_[col.id()];
    }
  }

  LOG(FATAL) << "Invalid attr_num (" << attr_num << ") was passed to PGGate";
  return nullptr;
}

Status PgInsert::SetColumnInt2(int attr_num, int16_t value) {
  PgsqlExpressionPB *expr_pb = AllocColumnPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_int16_value(value);
  return Status::OK();
}

Status PgInsert::SetColumnInt4(int attr_num, int32_t value) {
  PgsqlExpressionPB *expr_pb = AllocColumnPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_int32_value(value);
  return Status::OK();
}

Status PgInsert::SetColumnInt8(int attr_num, int64_t value) {
  PgsqlExpressionPB *expr_pb = AllocColumnPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_int64_value(value);
  return Status::OK();
}

Status PgInsert::SetColumnFloat4(int attr_num, float value) {
  PgsqlExpressionPB *expr_pb = AllocColumnPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_float_value(value);
  return Status::OK();
}

Status PgInsert::SetColumnFloat8(int attr_num, double value) {
  PgsqlExpressionPB *expr_pb = AllocColumnPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_double_value(value);
  return Status::OK();
}

Status PgInsert::SetColumnText(int attr_num, const char *attr_value, int attr_bytes) {
  PgsqlExpressionPB *expr_pb = AllocColumnPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();

  if (!attr_value || attr_bytes == 0) {
    const_pb->set_string_value(string());
  } else {
    const_pb->set_string_value(string(attr_value, attr_bytes));
  }
  return Status::OK();
}

Status PgInsert::SetColumnSerializedData(int attr_num, const char *attr_value, int attr_bytes) {
  return STATUS(NotSupported, "Setting serialized values is not yet supported");
}

Status PgInsert::Exec() {
  return pg_session_->Apply(op_);
}

}  // namespace pggate
}  // namespace yb

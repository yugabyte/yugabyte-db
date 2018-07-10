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
PgDml::PgDml(PgSession::SharedPtr pg_session,
             const char *database_name,
             const char *schema_name,
             const char *table_name,
             StmtOp stmt_op)
    : PgStatement(pg_session, stmt_op),
      table_name_(database_name, table_name) {
}

PgDml::~PgDml() {
}

Status PgDml::LoadTable(bool for_write) {
  return pg_session_->LoadTable(table_name_, for_write, &table_, &col_descs_, &key_col_count_,
                                &partition_col_count_);
}

Status PgDml::ClearBinds() {
  return STATUS(NotSupported, "Clearing binds for prepared statement is not yet implemented");
}

Status PgDml::SetColumnInt2(int attr_num, int16_t value) {
  PgsqlExpressionPB *expr_pb = AllocColumnExprPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_int16_value(value);
  return Status::OK();
}

Status PgDml::SetColumnInt4(int attr_num, int32_t value) {
  PgsqlExpressionPB *expr_pb = AllocColumnExprPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_int32_value(value);
  return Status::OK();
}

Status PgDml::SetColumnInt8(int attr_num, int64_t value) {
  PgsqlExpressionPB *expr_pb = AllocColumnExprPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_int64_value(value);
  return Status::OK();
}

Status PgDml::SetColumnFloat4(int attr_num, float value) {
  PgsqlExpressionPB *expr_pb = AllocColumnExprPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_float_value(value);
  return Status::OK();
}

Status PgDml::SetColumnFloat8(int attr_num, double value) {
  PgsqlExpressionPB *expr_pb = AllocColumnExprPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();
  const_pb->set_double_value(value);
  return Status::OK();
}

Status PgDml::SetColumnText(int attr_num, const char *attr_value, int attr_bytes) {
  PgsqlExpressionPB *expr_pb = AllocColumnExprPB(attr_num);
  QLValuePB *const_pb = expr_pb->mutable_value();

  if (!attr_value || attr_bytes == 0) {
    const_pb->set_string_value(string());
  } else {
    const_pb->set_string_value(string(attr_value, attr_bytes));
  }
  return Status::OK();
}

Status PgDml::SetColumnSerializedData(int attr_num, const char *attr_value, int attr_bytes) {
  return STATUS(NotSupported, "Setting serialized values is not yet supported");
}

}  // namespace pggate
}  // namespace yb

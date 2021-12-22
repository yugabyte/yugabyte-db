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

#include "yb/yql/pggate/pg_column.h"

#include "yb/client/schema.h"

#include "yb/common/ql_type.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/schema.h"

namespace yb {
namespace pggate {

namespace {

ColumnSchema kColumnYBctid(
    "ybctid", QLType::CreatePrimitiveType<DataType::BINARY>(),
    false, false, false, false, to_underlying(PgSystemAttrNum::kYBTupleId));

}

PgColumn::PgColumn(std::reference_wrapper<const Schema> schema, size_t index)
    : schema_(schema), index_(index) {
}

//--------------------------------------------------------------------------------------------------

PgsqlExpressionPB *PgColumn::AllocPrimaryBindPB(PgsqlWriteRequestPB *write_req) {
  if (is_partition()) {
    bind_pb_ = write_req->add_partition_column_values();
  } else if (is_primary()) {
    bind_pb_ = write_req->add_range_column_values();
  }
  return bind_pb_;
}

PgsqlExpressionPB *PgColumn::AllocBindPB(PgsqlWriteRequestPB *write_req) {
  if (bind_pb_ == nullptr) {
    DCHECK(!is_partition() && !is_primary())
      << "Binds for primary columns should have already been allocated by AllocPrimaryBindPB()";

    if (is_virtual_column()) {
      bind_pb_ = write_req->mutable_ybctid_column_value();
    } else {
      PgsqlColumnValuePB* col_pb = write_req->add_column_values();
      col_pb->set_column_id(id());
      bind_pb_ = col_pb->mutable_expr();
    }
  }
  return bind_pb_;
}

PgsqlExpressionPB *PgColumn::AllocAssignPB(PgsqlWriteRequestPB *write_req) {
  if (assign_pb_ == nullptr) {
    PgsqlColumnValuePB* col_pb = write_req->add_column_new_values();
    col_pb->set_column_id(id());
    assign_pb_ = col_pb->mutable_expr();
  }
  return assign_pb_;
}

bool PgColumn::is_partition() const {
  return index_ < schema_.num_hash_key_columns();
}

bool PgColumn::is_primary() const {
  return index_ < schema_.num_key_columns();
}

bool PgColumn::is_virtual_column() const {
  return index_ == schema_.num_columns();
}

const ColumnSchema& PgColumn::desc() const {
  return is_virtual_column() ? kColumnYBctid : schema_.column(index_);
}

//--------------------------------------------------------------------------------------------------

PgsqlExpressionPB *PgColumn::AllocPrimaryBindPB(PgsqlReadRequestPB *read_req) {
  if (is_partition()) {
    bind_pb_ = read_req->add_partition_column_values();
  } else if (is_primary()) {
    bind_pb_ = read_req->add_range_column_values();
  }
  return bind_pb_;
}

PgsqlExpressionPB *PgColumn::AllocBindPB(PgsqlReadRequestPB *read_req) {
  if (bind_pb_ == nullptr) {
    DCHECK(!is_partition() && !is_primary())
      << "Binds for primary columns should have already been allocated by AllocPrimaryBindPB()";

    if (is_virtual_column()) {
      bind_pb_ = read_req->mutable_ybctid_column_value();
    } else {
      DLOG(FATAL) << "Binds for other columns are not allowed";
    }
  }
  return bind_pb_;
}

//--------------------------------------------------------------------------------------------------

PgsqlExpressionPB *PgColumn::AllocBindConditionExprPB(PgsqlReadRequestPB *read_req) {
  if (bind_condition_expr_pb_ == nullptr) {
    bind_condition_expr_pb_ = read_req->mutable_condition_expr();
    bind_condition_expr_pb_->mutable_condition()->set_op(QL_OP_AND);
  }
  return bind_condition_expr_pb_->mutable_condition()->add_operands();
}

void PgColumn::ResetBindPB() {
  bind_pb_ = nullptr;
}

int PgColumn::id() const {
  return is_virtual_column() ? to_underlying(PgSystemAttrNum::kYBTupleId)
                             : schema_.column_id(index_);
}

InternalType PgColumn::internal_type() const {
  return client::YBColumnSchema::ToInternalDataType(desc().type());
}

const std::string& PgColumn::attr_name() const {
  return desc().name();
}

int PgColumn::attr_num() const {
  return desc().order();
}

}  // namespace pggate
}  // namespace yb

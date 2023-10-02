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
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/schema.h"

#include "yb/dockv/key_entry_value.h"

#include "yb/util/logging.h"

#include "yb/yql/pggate/pg_expr.h"

namespace yb {
namespace pggate {

namespace {

ColumnSchema kColumnYBctid(
    "ybctid", QLType::Create(DataType::BINARY), ColumnKind::RANGE_ASC_NULL_FIRST, Nullable::kFalse,
    false, false, to_underlying(PgSystemAttrNum::kYBTupleId));

LWPgsqlExpressionPB* AllocNonVirtualBindPB(
    const PgColumn& column, LWPgsqlWriteRequestPB* write_req) {
  auto* col_pb = write_req->add_column_values();
  col_pb->set_column_id(column.id());
  return col_pb->mutable_expr();
}

LWPgsqlExpressionPB* AllocNonVirtualBindPB(
    const PgColumn& column, LWPgsqlReadRequestPB* write_req) {
  LOG(DFATAL) << "Binds for other columns are not allowed";
  return nullptr;
}

dockv::KeyEntryValue QLValueToKeyEntryValue(const LWQLValuePB* input, const ColumnSchema& desc) {
  return input ? dockv::KeyEntryValue::FromQLValuePBForKey(*input, desc.sorting_type())
               : dockv::KeyEntryValue();
}

ArenaList<LWPgsqlExpressionPB>& SubExprsOut(LWPgsqlExpressionPB* bind_pb) {
  auto it = bind_pb->mutable_condition()->mutable_operands()->begin();
  ++it;
  return *it->mutable_condition()->mutable_operands();
}

} // namespace

PgColumn::PgColumn(std::reference_wrapper<const Schema> schema, size_t index)
    : schema_(schema), index_(index) {
}

//--------------------------------------------------------------------------------------------------

LWPgsqlExpressionPB *PgColumn::AllocPrimaryBindPB(LWPgsqlWriteRequestPB *write_req) {
  return DoAllocPrimaryBindPB(write_req);
}

template <class Req>
LWPgsqlExpressionPB *PgColumn::DoAllocPrimaryBindPB(Req *req) {
  if (is_partition()) {
    bind_pb_ = req->add_partition_column_values();
  } else if (is_primary()) {
    bind_pb_ = req->add_range_column_values();
  }
  return bind_pb_;
}

Result<LWPgsqlExpressionPB*> PgColumn::AllocBindPB(LWPgsqlWriteRequestPB* write_req, PgExpr* expr) {
  return DoAllocBindPB(write_req, expr);
}

template <class Req>
Result<LWPgsqlExpressionPB*> PgColumn::DoAllocBindPB(Req* req, PgExpr* expr) {
  if (bind_pb_ != nullptr) {
    if (ValueBound()) {
      LOG(DFATAL) << "Column already bound";
    } else if (expr) {
      RETURN_NOT_OK(expr->EvalTo(bind_pb_));
    }
    return bind_pb_;
  }

  DCHECK(!is_partition() && !is_primary())
    << "Binds for primary columns should have already been allocated by AllocPrimaryBindPB()";

  bind_pb_ = is_virtual_column() ? req->mutable_ybctid_column_value()
                                 : AllocNonVirtualBindPB(*this, req);
  if (expr) {
    RETURN_NOT_OK(expr->EvalTo(bind_pb_));
  }
  return bind_pb_;
}

bool PgColumn::ValueBound() const {
  if (bind_pb_ && bind_pb_->expr_case() != PgsqlExpressionPB::EXPR_NOT_SET) {
    DCHECK(bind_pb_->has_value());
    return true;
  }
  return false;
}

void PgColumn::UnbindValue() {
  if (bind_pb_) {
    bind_pb_->clear_value();
  }
}

void PgColumn::MoveBoundValueTo(LWPgsqlExpressionPB* out) {
  DCHECK(ValueBound());
  out->ref_value(bind_pb_->mutable_value());
  UnbindValue();
}

LWPgsqlExpressionPB *PgColumn::AllocAssignPB(LWPgsqlWriteRequestPB *write_req) {
  if (assign_pb_ == nullptr) {
    auto* col_pb = write_req->add_column_new_values();
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

LWPgsqlExpressionPB *PgColumn::AllocPrimaryBindPB(LWPgsqlReadRequestPB *read_req) {
  return DoAllocPrimaryBindPB(read_req);
}

Result<LWPgsqlExpressionPB*> PgColumn::AllocBindPB(LWPgsqlReadRequestPB* read_req, PgExpr* expr) {
  return DoAllocBindPB(read_req, expr);
}

//--------------------------------------------------------------------------------------------------

LWPgsqlExpressionPB *PgColumn::AllocBindConditionExprPB(LWPgsqlReadRequestPB *read_req) {
  if (bind_condition_expr_pb_ == nullptr) {
    bind_condition_expr_pb_ = read_req->mutable_condition_expr();
    bind_condition_expr_pb_->mutable_condition()->set_op(QL_OP_AND);
  }
  return bind_condition_expr_pb_->mutable_condition()->add_operands();
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

Result<dockv::KeyEntryValue> PgColumn::BuildKeyColumnValue(LWQLValuePB** dest) const {
  SCHECK(ValueBound(), IllegalState, "Bind value not found for $0", id());
  *dest = bind_pb_->mutable_value();
  return QLValueToKeyEntryValue(*dest, desc());
}

Result<dockv::KeyEntryValue> PgColumn::BuildKeyColumnValue() const {
  LWQLValuePB* temp_value;
  return BuildKeyColumnValue(&temp_value);
}

Result<dockv::KeyEntryValue> PgColumn::BuildSubExprKeyColumnValue(size_t idx) const {
  return QLValueToKeyEntryValue(&std::next(SubExprsOut(bind_pb_).begin(), idx)->value(), desc());
}

Status PgColumn::SetSubExprs(PgDml* stmt, PgExpr **value, size_t count) {
  // There's no "list of expressions" field, so we simulate it with an artificial nested OR
  // with repeated operands, one per bind expression.
  // This is only used for operation unrolling in pg_doc_op and is not understood by DocDB.
  auto cond = bind_pb_->mutable_condition()->add_operands()->mutable_condition();
  cond->set_op(QL_OP_OR);

  for (size_t i = 0; i != count; ++i) {
    RETURN_NOT_OK(value[i]->EvalTo(cond->add_operands()));
  }
  return Status::OK();
}

size_t PgColumn::SubExprsCount() const {
  return SubExprsOut(bind_pb_).size();
}

// Moves IN operator bound for range key component into 'condition_expr' field
Status PgColumn::MoveBoundKeyInOperator(LWPgsqlReadRequestPB* read_req) {
  auto& cond = *AllocBindConditionExprPB(read_req)->mutable_condition();
  cond.set_op(QL_OP_IN);
  cond.mutable_operands()->push_back_ref(
      &bind_pb_->mutable_condition()->mutable_operands()->front());

  auto list = cond.add_operands()->mutable_value()->mutable_list_value();
  auto& out = SubExprsOut(bind_pb_);
  for (auto& expr : out) {
    list->mutable_elems()->push_back_ref(expr.mutable_value());
  }
  out.clear();
  return Status::OK();
}

}  // namespace pggate
}  // namespace yb

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

#include "yb/sql/exec/executor.h"

namespace yb {
namespace sql {

using std::shared_ptr;

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ColumnRefsToPB(const PTDmlStmt *tnode,
                                        YQLReferencedColumnsPB *columns_pb) {
  // Write a list of columns to be read before executing the statement.
  const MCSet<int32>& column_refs = tnode->column_refs();
  for (auto column_ref : column_refs) {
    columns_pb->add_ids(column_ref);
  }

  const MCSet<int32>& static_column_refs = tnode->static_column_refs();
  for (auto column_ref : static_column_refs) {
    columns_pb->add_static_ids(column_ref);
  }
  return Status::OK();
}

CHECKED_STATUS Executor::ColumnArgsToPB(const shared_ptr<client::YBTable>& table,
                                        const PTDmlStmt *tnode,
                                        YQLWriteRequestPB *req,
                                        YBPartialRow *row) {
  const MCVector<ColumnArg>& column_args = tnode->column_args();
  for (const ColumnArg& col : column_args) {
    if (!col.IsInitialized()) {
      // This column is not assigned a value, ignore it. We don't support default value yet.
      continue;
    }

    const ColumnDesc *col_desc = col.desc();
    YQLColumnValuePB* col_pb;

    if (col_desc->is_hash()) {
      col_pb = req->add_hashed_column_values();
    } else if (col_desc->is_primary()) {
      col_pb = req->add_range_column_values();
    } else {
      col_pb = req->add_column_values();
    }

    VLOG(3) << "WRITE request, column id = " << col_desc->id();
    col_pb->set_column_id(col_desc->id());
    YQLExpressionPB *expr_pb = col_pb->mutable_expr();
    RETURN_NOT_OK(PTExprToPB(col.expr(), expr_pb));
    // null values not allowed for primary key: checking here catches nulls introduced by bind
    if (col_desc->is_primary() && expr_pb->has_value() && YQLValue::IsNull(expr_pb->value())) {
      LOG(INFO) << "Unexpected null value. Current request: " << req->DebugString();
      return exec_context_->Error(tnode->loc(), ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY);
    }

    if (col_desc->is_hash()) {
      RETURN_NOT_OK(SetupPartialRow(col_desc, expr_pb, row));
    }
  }

  const MCVector<SubscriptedColumnArg>& subcol_args = tnode->subscripted_col_args();
  for (const SubscriptedColumnArg& col : subcol_args) {
    const ColumnDesc *col_desc = col.desc();
    YQLColumnValuePB *col_pb = req->add_column_values();
    col_pb->set_column_id(col_desc->id());
    YQLExpressionPB *expr_pb = col_pb->mutable_expr();
    RETURN_NOT_OK(PTExprToPB(col.expr(), expr_pb));
    for (auto& col_arg : col.args()->node_list()) {
      YQLExpressionPB *arg_pb = col_pb->add_subscript_args();
      RETURN_NOT_OK(PTExprToPB(col_arg, arg_pb));
    }
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::SetupPartialRow(const ColumnDesc *col_desc,
                                         const YQLExpressionPB *expr_pb,
                                         YBPartialRow *row) {
  DCHECK(expr_pb->has_value()) << "Expecting literals for hash columns";

  const YQLValuePB& value_pb = expr_pb->value();
  if (YQLValue::IsNull(value_pb)) {
    return Status::OK();
  }

  switch (YQLValue::type(value_pb)) {
    case InternalType::kInt8Value:
      RETURN_NOT_OK(row->SetInt8(col_desc->index(), YQLValue::int8_value(value_pb)));
      break;
    case InternalType::kInt16Value:
      RETURN_NOT_OK(row->SetInt16(col_desc->index(), YQLValue::int16_value(value_pb)));
      break;
    case InternalType::kInt32Value:
      RETURN_NOT_OK(row->SetInt32(col_desc->index(), YQLValue::int32_value(value_pb)));
      break;
    case InternalType::kInt64Value:
      RETURN_NOT_OK(row->SetInt64(col_desc->index(), YQLValue::int64_value(value_pb)));
      break;
    case InternalType::kDecimalValue: {
      const string& decimal_value = YQLValue::decimal_value(value_pb);
      RETURN_NOT_OK(row->SetDecimal(col_desc->index(),
                                    Slice(decimal_value.data(), decimal_value.size())));
      break;
    }
    case InternalType::kStringValue:
      RETURN_NOT_OK(row->SetString(col_desc->index(), YQLValue::string_value(value_pb)));
      break;
    case InternalType::kTimestampValue:
      RETURN_NOT_OK(row->SetTimestamp(col_desc->index(),
                                      YQLValue::timestamp_value(value_pb).ToInt64()));
      break;
    case InternalType::kInetaddressValue: {
      std::string bytes;
      RETURN_NOT_OK(YQLValue::inetaddress_value(value_pb).ToBytes(&bytes));
      RETURN_NOT_OK(row->SetInet(col_desc->index(), Slice(bytes)));
      break;
    }
    case InternalType::kUuidValue: {
      std::string bytes;
      RETURN_NOT_OK(YQLValue::uuid_value(value_pb).ToBytes(&bytes));
      RETURN_NOT_OK(row->SetUuidCopy(col_desc->index(), Slice(bytes)));
      break;
    }
    case InternalType::kTimeuuidValue: {
      std::string bytes;
      RETURN_NOT_OK(YQLValue::timeuuid_value(value_pb).ToBytes(&bytes));
      RETURN_NOT_OK(row->SetTimeUuidCopy(col_desc->index(), Slice(bytes)));
      break;
    }
    case InternalType::kBinaryValue:
      RETURN_NOT_OK(row->SetBinary(col_desc->index(), YQLValue::binary_value(value_pb)));
      break;
    case InternalType::kFrozenValue:
      RETURN_NOT_OK(row->SetFrozen(col_desc->index(), YQLValue::frozen_value(value_pb)));
      break;

    case InternalType::kBoolValue: FALLTHROUGH_INTENDED;
    case InternalType::kFloatValue: FALLTHROUGH_INTENDED;
    case InternalType::kDoubleValue: FALLTHROUGH_INTENDED;
    case InternalType::kMapValue: FALLTHROUGH_INTENDED;
    case InternalType::kSetValue: FALLTHROUGH_INTENDED;
    case InternalType::kListValue:
      LOG(FATAL) << "Invalid datatype for partition column";

    case InternalType::kVarintValue: FALLTHROUGH_INTENDED;
    default:
      LOG(FATAL) << "DataType not yet supported";
  }

  return Status::OK();
}

}  // namespace sql
}  // namespace yb

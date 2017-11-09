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

#include "yb/yql/cql/ql/exec/executor.h"

namespace yb {
namespace ql {

using std::shared_ptr;
using yb::bfql::TSOpcode;

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::AggregateResultSets() {
  DCHECK(exec_context_->tnode()->opcode() == TreeNodeOpcode::kPTSelectStmt);
  const PTSelectStmt *pt_select = static_cast<const PTSelectStmt*>(exec_context_->tnode());
  if (!pt_select->is_aggregate()) {
    return Status::OK();
  }

  shared_ptr<RowsResult> rows = std::static_pointer_cast<RowsResult>(result_);
  DCHECK(rows->client() == QLClient::YQL_CLIENT_CQL);
  shared_ptr<QLRowBlock> row_block = rows->GetRowBlock();
  int column_index = 0;
  faststring buffer;

  CQLEncodeLength(1, &buffer);
  for (auto expr_node : pt_select->selected_exprs()) {
    QLValue ql_value;

    switch (expr_node->aggregate_opcode()) {
      case TSOpcode::kNoOp:
        break;
      case TSOpcode::kAvg:
        RETURN_NOT_OK(STATUS(NotSupported, "Function AVG() not yet supported"));
        break;
      case TSOpcode::kCount:
        RETURN_NOT_OK(EvalCount(row_block, column_index, &ql_value));
        break;
      case TSOpcode::kMax:
        RETURN_NOT_OK(EvalMax(row_block, column_index, &ql_value));
        break;
      case TSOpcode::kMin:
        RETURN_NOT_OK(EvalMin(row_block, column_index, &ql_value));
        break;
      case TSOpcode::kSum:
        RETURN_NOT_OK(EvalSum(row_block, column_index, expr_node->ql_type()->main(), &ql_value));
        break;
      default:
        return STATUS(RuntimeError, "Unexpected operator while evaluating aggregate expressions");
    }

    // Serialize the return value.
    ql_value.Serialize(expr_node->ql_type(), rows->client(), &buffer);
    column_index++;
  }

  // Change the result set to the aggregate result.
  std::static_pointer_cast<RowsResult>(result_)->set_rows_data(buffer.c_str(), buffer.size());
  return Status::OK();
}

CHECKED_STATUS Executor::EvalCount(const shared_ptr<QLRowBlock>& row_block,
                                   int column_index,
                                   QLValue *ql_value) {
  int64_t total_count = 0;
  for (auto row : row_block->rows()) {
    total_count += row.column(column_index).int64_value();
  }
  ql_value->set_int64_value(total_count);
  return Status::OK();
}

CHECKED_STATUS Executor::EvalMax(const shared_ptr<QLRowBlock>& row_block,
                                 int column_index,
                                 QLValue *ql_value) {
  for (auto row : row_block->rows()) {
    if (ql_value->IsNull() ||
        (!row.column(column_index).IsNull() && *ql_value < row.column(column_index))) {
      *ql_value = row.column(column_index);
    }
  }
  return Status::OK();
}

CHECKED_STATUS Executor::EvalMin(const shared_ptr<QLRowBlock>& row_block,
                                 int column_index,
                                 QLValue *ql_value) {
  for (auto row : row_block->rows()) {
    if (ql_value->IsNull() ||
        (!row.column(column_index).IsNull() && *ql_value > row.column(column_index))) {
      *ql_value = row.column(column_index);
    }
  }
  return Status::OK();
}

CHECKED_STATUS Executor::EvalSum(const shared_ptr<QLRowBlock>& row_block,
                                 int column_index,
                                 DataType data_type,
                                 QLValue *ql_value) {
  // CQL doesn't return overflow for sum.
  for (auto row : row_block->rows()) {
    if (row.column(column_index).IsNull()) {
      continue;
    }
    if (ql_value->IsNull()) {
      *ql_value = row.column(column_index);
      continue;
    }
    switch (data_type) {
      case DataType::INT8:
        ql_value->set_int8_value(ql_value->int8_value() + row.column(column_index).int8_value());
        break;
      case DataType::INT16:
        ql_value->set_int16_value(ql_value->int16_value() + row.column(column_index).int16_value());
        break;
      case DataType::INT32:
        ql_value->set_int32_value(ql_value->int32_value() + row.column(column_index).int32_value());
        break;
      case DataType::INT64:
        ql_value->set_int64_value(ql_value->int64_value() + row.column(column_index).int64_value());
        break;
      case DataType::FLOAT:
        ql_value->set_float_value(ql_value->float_value() + row.column(column_index).float_value());
        break;
      case DataType::DOUBLE:
        ql_value->set_double_value(ql_value->double_value() +
                                   row.column(column_index).double_value());
        break;
      default:
        return STATUS(RuntimeError, "Unexpected datatype for argument of SUM()");
    }
  }
  return Status::OK();
}

}  // namespace ql
}  // namespace yb

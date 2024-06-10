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

#include "yb/common/ql_protocol_util.h"
#include "yb/qlexpr/ql_rowblock.h"
#include "yb/qlexpr/ql_serialization.h"
#include "yb/common/ql_value.h"

#include "yb/util/decimal.h"
#include "yb/util/result.h"

#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"

namespace yb {
namespace ql {

using std::shared_ptr;
using yb::bfql::TSOpcode;
using yb::util::Decimal;

//--------------------------------------------------------------------------------------------------

Status Executor::AggregateResultSets(const PTSelectStmt* pt_select, TnodeContext* tnode_context) {
  if (!pt_select->is_aggregate()) {
    return Status::OK();
  }

  shared_ptr<RowsResult> rows_result = tnode_context->rows_result();
  DCHECK(rows_result->client() == QLClient::YQL_CLIENT_CQL);
  shared_ptr<qlexpr::QLRowBlock> row_block = rows_result->GetRowBlock();
  int column_index = 0;
  WriteBuffer buffer(1024);

  CQLEncodeLength(1, &buffer);
  for (auto expr_node : pt_select->selected_exprs()) {
    QLValue ql_value;

    switch (expr_node->aggregate_opcode()) {
      case TSOpcode::kNoOp:
        break;
      case TSOpcode::kAvg:
        RETURN_NOT_OK(EvalAvg(row_block, column_index, expr_node->ql_type()->main(),
                              &ql_value));
        // Change type back from MAP to basic type for result of Avg
        rows_result->set_column_schema(column_index, expr_node->ql_type());
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
    qlexpr::SerializeValue(expr_node->ql_type(), rows_result->client(), ql_value.value(), &buffer);
    column_index++;
  }

  // Change the result set to the aggregate result.
  rows_result->set_rows_data(buffer.ToContinuousBlock());
  return Status::OK();
}

Status Executor::EvalCount(const shared_ptr<qlexpr::QLRowBlock>& row_block,
                           int column_index,
                           QLValue *ql_value) {
  int64_t total_count = 0;
  for (auto row : row_block->rows()) {
    if (!row.column(column_index).IsNull()) {
      // Summing up the sub-counts from individual partitions.
      // For details see DocExprExecutor::EvalTSCall() and DocExprExecutor::EvalCount().
      total_count += row.column(column_index).int64_value();
    }
  }
  ql_value->set_int64_value(total_count);
  return Status::OK();
}

Status Executor::EvalMax(const shared_ptr<qlexpr::QLRowBlock>& row_block,
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

Status Executor::EvalMin(const shared_ptr<qlexpr::QLRowBlock>& row_block,
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

Status Executor::EvalSum(const shared_ptr<qlexpr::QLRowBlock>& row_block,
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
      case DataType::VARINT:
        ql_value->set_varint_value(ql_value->varint_value() +
                                   row.column(column_index).varint_value());
        break;
      case DataType::FLOAT:
        ql_value->set_float_value(ql_value->float_value() + row.column(column_index).float_value());
        break;
      case DataType::DOUBLE:
        ql_value->set_double_value(ql_value->double_value() +
                                   row.column(column_index).double_value());
        break;
      case DataType::DECIMAL: {
        Decimal sum, value;
        RETURN_NOT_OK(sum.DecodeFromComparable(ql_value->decimal_value()));
        RETURN_NOT_OK(value.DecodeFromComparable(row.column(column_index).decimal_value()));
        sum = sum + value;
        ql_value->set_decimal_value(sum.EncodeToComparable());
        break;
      }
      default:
        return STATUS(RuntimeError, "Unexpected datatype for argument of SUM()");
    }
  }
  if (ql_value->IsNull()) {
    switch (data_type) {
      case DataType::INT8:
        ql_value->set_int8_value(0);
        break;
      case DataType::INT16:
        ql_value->set_int16_value(0);
        break;
      case DataType::INT32:
        ql_value->set_int32_value(0);
        break;
      case DataType::INT64:
        ql_value->set_int64_value(0);
        break;
      case DataType::VARINT: {
        int64_t tsum;
        tsum = 0;
        VarInt varint(tsum);
        ql_value->set_varint_value(varint);
      }
        break;
      case DataType::FLOAT:
        ql_value->set_float_value(0);
        break;
      case DataType::DOUBLE:
        ql_value->set_double_value(0);
        break;
      case DataType::DECIMAL: {
        Decimal sum;
        ql_value->set_decimal_value(sum.EncodeToComparable());
        break;
      }
      default:
        return STATUS(RuntimeError, "Unexpected datatype for argument of SUM()");
    }
  }
  return Status::OK();
}

Status Executor::EvalAvg(const shared_ptr<qlexpr::QLRowBlock>& row_block,
                         int column_index,
                         DataType data_type,
                         QLValue *ql_value) {
  QLValue sum, count;

  for (auto row : row_block->rows()) {
    if (row.column(column_index).IsNull()) {
      continue;
    }
    QLMapValuePB map = row.column(column_index).map_value();
    if (count.IsNull()) {
      count = QLValue(map.keys(0));
      sum = QLValue(map.values(0));
      continue;
    }

    count.set_int64_value(count.int64_value() + map.keys(0).int64_value());
    switch (data_type) {
      case DataType::INT8:
        sum.set_int8_value(sum.int8_value() + map.values(0).int8_value());
        break;
      case DataType::INT16:
        sum.set_int16_value(sum.int16_value() + map.values(0).int16_value());
        break;
      case DataType::INT32:
        sum.set_int32_value(sum.int32_value() + map.values(0).int32_value());
        break;
      case DataType::INT64:
        sum.set_int64_value(sum.int64_value() + map.values(0).int64_value());
        break;
      case DataType::VARINT:
        sum.set_varint_value(sum.varint_value() + QLValue(map.values(0)).varint_value());
        break;
      case DataType::FLOAT:
        sum.set_float_value(sum.float_value() + map.values(0).float_value());
        break;
      case DataType::DOUBLE:
        sum.set_double_value(sum.double_value() + map.values(0).double_value());
        break;
      default:
        return STATUS(RuntimeError, "Unexpected datatype for argument of AVG()");
    }
  }

  switch (data_type) {
    case DataType::INT8:
      ql_value->set_int8_value(sum.IsNull() ? 0 : sum.int8_value() / count.int64_value());
      break;
    case DataType::INT16:
      ql_value->set_int16_value(sum.IsNull() ? 0 : sum.int16_value() / count.int64_value());
      break;
    case DataType::INT32:
      ql_value->set_int32_value(sum.IsNull() ? 0 : sum.int32_value() / count.int64_value());
      break;
    case DataType::INT64:
      ql_value->set_int64_value(sum.IsNull() ? 0 : sum.int64_value() / count.int64_value());
      break;
    case DataType::VARINT:
      if (sum.IsNull()) {
        VarInt varint(0);
        ql_value->set_varint_value(varint);
      } else {
        int64_t tsum = VERIFY_RESULT(sum.varint_value().ToInt64());
        int64_t tcount = VERIFY_RESULT(count.varint_value().ToInt64());
        VarInt average(tsum / tcount);
        ql_value->set_varint_value(average);
      }
      break;
    case DataType::FLOAT:
      ql_value->set_float_value(sum.IsNull() ? 0 :sum.float_value() / count.int64_value());
      break;
    case DataType::DOUBLE:
      ql_value->set_double_value(sum.IsNull() ? 0 :sum.double_value() / count.int64_value());
      break;
    default:
      return STATUS(RuntimeError, "Unexpected datatype for argument of AVG()");
  }
  return Status::OK();
}

}  // namespace ql
}  // namespace yb

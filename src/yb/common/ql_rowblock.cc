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
//
// This file contains the classes that represent a QL row and a row block.

#include "yb/common/ql_rowblock.h"

#include "yb/util/bfql/directory.h"
#include "yb/util/bfql/bfql.h"
#include "yb/common/wire_protocol.h"

namespace yb {

using std::shared_ptr;

//----------------------------------------- QL row ----------------------------------------
QLRow::QLRow(const shared_ptr<const Schema>& schema)
    : schema_(schema), values_(schema->num_columns()) {
}

QLRow::QLRow(const QLRow& other) : schema_(other.schema_), values_(other.values_) {
}

QLRow::QLRow(QLRow&& other)
    : schema_(std::move(other.schema_)), values_(std::move(other.values_)) {
}

QLRow::~QLRow() {
}

void QLRow::Serialize(const QLClient client, faststring* buffer) const {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    values_.at(col_idx).Serialize(column_type(col_idx), client, buffer);
  }
}

Status QLRow::Deserialize(const QLClient client, Slice* data) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    RETURN_NOT_OK(values_.at(col_idx).Deserialize(column_type(col_idx), client, data));
  }
  return Status::OK();
}

string QLRow::ToString() const {
  string s = "{ ";
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    if (col_idx > 0) {
      s+= ", ";
    }
    s += values_.at(col_idx).ToString();
  }
  s += " }";
  return s;
}

QLRow& QLRow::operator=(const QLRow& other) {
  this->~QLRow();
  new(this) QLRow(other);
  return *this;
}

QLRow& QLRow::operator=(QLRow&& other) {
  this->~QLRow();
  new(this) QLRow(other);
  return *this;
}

//-------------------------------------- QL row block --------------------------------------
QLRowBlock::QLRowBlock(const Schema& schema, const vector<ColumnId>& column_ids)
    : schema_(new Schema()) {
  // TODO: is there a better way to report errors here?
  CHECK_OK(schema.CreateProjectionByIdsIgnoreMissing(column_ids, schema_.get()));
}

QLRowBlock::QLRowBlock(const Schema& schema) : schema_(new Schema(schema)) {
}

QLRowBlock::~QLRowBlock() {
}

QLRow& QLRowBlock::Extend() {
  rows_.emplace_back(schema_);
  return rows_.back();
}

Status QLRowBlock::AddRow(const QLRow& row) {
  // TODO: check for schema compatibility between QLRow and QLRowBlock.
  rows_.push_back(row);
  return Status::OK();
}

string QLRowBlock::ToString() const {
  string s = "{ ";
  for (size_t i = 0; i < rows_.size(); i++) {
    if (i > 0) { s+= ", "; }
    s += rows_[i].ToString();
  }
  s += " }";
  return s;
}

void QLRowBlock::Serialize(const QLClient client, faststring* buffer) const {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  CQLEncodeLength(rows_.size(), buffer);
  for (const auto& row : rows_) {
    row.Serialize(client, buffer);
  }
}

Status QLRowBlock::Deserialize(const QLClient client, Slice* data) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  int32_t count = 0;
  RETURN_NOT_OK(CQLDecodeNum(sizeof(count), NetworkByteOrder::Load32, data, &count));

  for (int32_t i = 0; i < count; ++i) {
    RETURN_NOT_OK(Extend().Deserialize(client, data));
  }
  if (!data->empty()) {
    return STATUS(Corruption, "Extra data at the end of row block");
  }
  return Status::OK();
}

Status QLRowBlock::GetRowCount(const QLClient client, const std::string& data, size_t* count) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  int32_t cnt = 0;
  Slice slice(data);
  RETURN_NOT_OK(CQLDecodeNum(sizeof(cnt), NetworkByteOrder::Load32, &slice, &cnt));
  *count = cnt;
  return Status::OK();
}

Status QLRowBlock::AppendRowsData(
    const QLClient client, const std::string& src, std::string* dst) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  int32_t src_cnt = 0;
  Slice src_slice(src);
  RETURN_NOT_OK(CQLDecodeNum(sizeof(src_cnt), NetworkByteOrder::Load32, &src_slice, &src_cnt));
  if (src_cnt > 0) {
    int32_t dst_cnt = 0;
    Slice dst_slice(*dst);
    RETURN_NOT_OK(CQLDecodeNum(sizeof(dst_cnt), NetworkByteOrder::Load32, &dst_slice, &dst_cnt));
    if (dst_cnt == 0) {
      *dst = src;
    } else {
      dst->append(util::to_char_ptr(src_slice.data()), src_slice.size());
      dst_cnt += src_cnt;
      CQLEncodeLength(dst_cnt, &(*dst)[0]);
    }
  }
  return Status::OK();
}

namespace {

// Evaluate and return the value of an expression for the given row. Evaluate only column and
// literal values for now.
QLValuePB EvaluateValue(const QLExpressionPB& expr, const QLTableRow& table_row) {
  switch (expr.expr_case()) {
    case QLExpressionPB::ExprCase::kColumnId: {
      const auto column_id = ColumnId(expr.column_id());
      const auto it = table_row.find(column_id);
      return it != table_row.end() ? it->second.value : QLValuePB();
    }
    case QLExpressionPB::ExprCase::kSubscriptedCol: {
      const auto column_id = ColumnId(expr.subscripted_col().column_id());
      const auto it = table_row.find(column_id);
      if (it == table_row.end()) {
        return QLValuePB();
      } else {
        if (it->second.value.has_map_value()) { // map['key']
          auto &map = it->second.value.map_value();
          auto key = EvaluateValue(expr.subscripted_col().subscript_args(0), table_row);
          for (int i = 0; i < map.keys_size(); i++) {
            if (map.keys(i) == key) {
              return map.values(i);
            }
          }
        } else if (it->second.value.has_list_value()) { // list[index]
          auto &list = it->second.value.list_value();
          auto index_pb = EvaluateValue(expr.subscripted_col().subscript_args(0), table_row);

          if (index_pb.has_int32_value()) {
            int index = index_pb.int32_value();
            // QL list index starts from 1 not 0
            if (index > 0 && index <= list.elems_size()) {
              return list.elems(index - 1);
            } // otherwise we return null below
          }
        }
      }
      // default (if collection entry not found) is to return null value
      return QLValuePB();
    }
    case QLExpressionPB::ExprCase::kValue:
      return expr.value();

    case QLExpressionPB::ExprCase::kBfcall: {
      switch(static_cast<bfql::BFOpcode>(expr.bfcall().bfopcode())) {
        case bfql::BFOpcode::OPCODE_ttl_40: {
          const QLExpressionPB& column = expr.bfcall().operands(0);
          const auto column_id = ColumnId(column.column_id());
          const auto it = table_row.find(column_id);
          CHECK(it != table_row.end());
          QLValuePB ttl_seconds_pb;
          if (it->second.ttl_seconds != -1) {
            ttl_seconds_pb.set_int64_value(it->second.ttl_seconds);
          } else {
            QLValue::SetNull(&ttl_seconds_pb);
          }
          return ttl_seconds_pb;
        }
        case bfql::BFOpcode::OPCODE_writetime_41: {
          const QLExpressionPB& column = expr.bfcall().operands(0);
          const auto column_id = ColumnId(column.column_id());
          const auto it = table_row.find(column_id);
          CHECK(it != table_row.end());
          QLValuePB write_time_pb;
          write_time_pb.set_int64_value(it->second.write_time);
          return write_time_pb;
        }
        default:
          LOG(FATAL) << "Error: invalid builtin function: " << expr.bfcall().bfopcode();
      }
      break;
    }
    case QLExpressionPB::ExprCase::kCondition: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::EXPR_NOT_SET:
      break;
    // default: fall through
  }
  LOG(FATAL) << "Internal error: invalid column or value expression: " << expr.expr_case();
}

// Evaluate an IN (...) condition.
Status EvaluateInCondition(const google::protobuf::RepeatedPtrField<yb::QLExpressionPB> &operands,
                           const QLTableRow &row,
                           bool *result) {
  // Expecting two operands, second should be list of elements.
  CHECK_EQ(operands.size(), 2);
  *result = false;
  QLValuePB left = EvaluateValue(operands.Get(0), row);
  QLValuePB right = EvaluateValue(operands.Get(1), row);

  for (const QLValuePB& elem : right.list_value().elems()) {
    if (!QLValue::Comparable(left, elem)) return STATUS(RuntimeError, "values not comparable");
    if (left == elem) {
      *result = true;
      break;
    }
  }
  return Status::OK();
}

// Evaluate a BETWEEN(a, b) condition.
Status EvaluateBetweenCondition(
    const google::protobuf::RepeatedPtrField<yb::QLExpressionPB> &operands,
    const QLTableRow &row, bool *result) {
  CHECK_EQ(operands.size(), 3);
  QLValuePB v = EvaluateValue(operands.Get(0), row);
  QLValuePB lower_bound = EvaluateValue(operands.Get(1), row);
  QLValuePB upper_bound = EvaluateValue(operands.Get(2), row);
  if (!QLValue::Comparable(v, lower_bound) || !QLValue::Comparable(v, upper_bound)) {
    return STATUS(RuntimeError, "values not comparable");
  }
  *result = (lower_bound <= v && v <= upper_bound);
  return Status::OK();
}

} // namespace

// Evaluate a condition for the given row.
Status EvaluateCondition(
    const QLConditionPB &condition, const QLTableRow &table_row, bool *result) {
  const auto &operands = condition.operands();
  switch (condition.op()) {
    case QL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), QLExpressionPB::ExprCase::kCondition);
      RETURN_NOT_OK(EvaluateCondition(operands.Get(0).condition(), table_row, result));
      *result = !*result;
      return Status::OK();
    }
    case QL_OP_IS_NULL: {
      CHECK_EQ(operands.size(), 1);
      *result = QLValue::IsNull(EvaluateValue(operands.Get(0), table_row));
      return Status::OK();
    }
    case QL_OP_IS_NOT_NULL: {
      CHECK_EQ(operands.size(), 1);
      *result = !QLValue::IsNull(EvaluateValue(operands.Get(0), table_row));
      return Status::OK();
    }
    case QL_OP_IS_TRUE: {
      CHECK_EQ(operands.size(), 1);
      QLValuePB v = EvaluateValue(operands.Get(0), table_row);
      if (QLValue::type(v) != QLValue::InternalType::kBoolValue)
        return STATUS(RuntimeError, "not a bool value");
      *result = (!QLValue::IsNull(v) && QLValue::bool_value(v));
      return Status::OK();
    }
    case QL_OP_IS_FALSE: {
      CHECK_EQ(operands.size(), 1);
      QLValuePB v = EvaluateValue(operands.Get(0), table_row);
      if (QLValue::type(v) != QLValue::InternalType::kBoolValue)
        return STATUS(RuntimeError, "not a bool value");
      *result = (!QLValue::IsNull(v) && !QLValue::bool_value(v));
      return Status::OK();
    }

#define QL_EVALUATE_RELATIONAL_OP(op, operands, row, result)               \
      do {                                                                  \
        CHECK_EQ(operands.size(), 2);                                       \
        const QLValuePB left = EvaluateValue(operands.Get(0), table_row);  \
        const QLValuePB right = EvaluateValue(operands.Get(1), table_row); \
        if (!QLValue::Comparable(left, right))                             \
          return STATUS(RuntimeError, "values not comparable");             \
        *result = (left op right);                                          \
      } while (0)

    case QL_OP_EQUAL: {
      QL_EVALUATE_RELATIONAL_OP(==, operands, table_row, result);
      return Status::OK();
    }
    case QL_OP_LESS_THAN: {
      QL_EVALUATE_RELATIONAL_OP(<, operands, table_row, result);
      return Status::OK();
    }
    case QL_OP_LESS_THAN_EQUAL: {
      QL_EVALUATE_RELATIONAL_OP(<=, operands, table_row, result);
      return Status::OK();
    }
    case QL_OP_GREATER_THAN: {
      QL_EVALUATE_RELATIONAL_OP(>, operands, table_row, result);
      return Status::OK();
    }
    case QL_OP_GREATER_THAN_EQUAL: {
      QL_EVALUATE_RELATIONAL_OP(>=, operands, table_row, result);
      return Status::OK();
    }
    case QL_OP_NOT_EQUAL: {
      QL_EVALUATE_RELATIONAL_OP(!=, operands, table_row, result);
      return Status::OK();
    }

#undef QL_EVALUATE_RELATIONAL_OP

    case QL_OP_AND: {
      *result = true;
      CHECK_GT(operands.size(), 0);
      for (const auto &operand : operands) {
        bool value = false;
        CHECK_EQ(operand.expr_case(), QLExpressionPB::ExprCase::kCondition);
        RETURN_NOT_OK(EvaluateCondition(operand.condition(), table_row, &value));
        *result = *result && value;
        if (!*result)
          break;
      }
      return Status::OK();
    }
    case QL_OP_OR: {
      *result = false;
      CHECK_GT(operands.size(), 0);
      for (const auto &operand : operands) {
        bool value = true;
        CHECK_EQ(operand.expr_case(), QLExpressionPB::ExprCase::kCondition);
        RETURN_NOT_OK(EvaluateCondition(operand.condition(), table_row, &value));
        *result = *result || value;
        if (*result)
          break;
      }
      return Status::OK();
    }

    case QL_OP_LIKE:     FALLTHROUGH_INTENDED;
    case QL_OP_NOT_LIKE:
      return STATUS(RuntimeError, "LIKE operator not supported yet");

    case QL_OP_IN: {
      return EvaluateInCondition(operands, table_row, result);
    }
    case QL_OP_NOT_IN: {
      RETURN_NOT_OK(EvaluateInCondition(operands, table_row, result));
      *result = !*result;
      return Status::OK();
    }

    case QL_OP_BETWEEN: {
      return EvaluateBetweenCondition(operands, table_row, result);
    }
    case QL_OP_NOT_BETWEEN: {
      RETURN_NOT_OK(EvaluateBetweenCondition(operands, table_row, result));
      *result = !*result;
      return Status::OK();
    }

    // When a row exists, the primary key columns are always populated in the row (value-map) by
    // DocRowwiseIterator and only when it exists. Therefore, the row exists if and only if
    // the row (value-map) is not empty.
    case QL_OP_EXISTS: {
      *result = !table_row.empty();
      return Status::OK();
    }
    case QL_OP_NOT_EXISTS: {
      *result = table_row.empty();
      return Status::OK();
    }

    case QL_OP_NOOP:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: illegal or unknown operator " << condition.op();
}

} // namespace yb

// Copyright (c) YugaByte, Inc.
//
// This file contains the classes that represent a YQL row and a row block.

#include "yb/common/yql_rowblock.h"

#include "yb/util/bfyql/directory.h"
#include "yb/util/bfyql/bfyql.h"
#include "yb/common/wire_protocol.h"

namespace yb {

using std::shared_ptr;

//----------------------------------------- YQL row ----------------------------------------
YQLRow::YQLRow(const shared_ptr<const Schema>& schema)
    : schema_(schema), values_(schema->num_columns()) {
}

YQLRow::YQLRow(const YQLRow& other) : schema_(other.schema_), values_(other.values_) {
}

YQLRow::YQLRow(YQLRow&& other)
    : schema_(std::move(other.schema_)), values_(std::move(other.values_)) {
}

YQLRow::~YQLRow() {
}

void YQLRow::Serialize(const YQLClient client, faststring* buffer) const {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    values_.at(col_idx).Serialize(column_type(col_idx), client, buffer);
  }
}

Status YQLRow::Deserialize(const YQLClient client, Slice* data) {
  for (size_t col_idx = 0; col_idx < schema_->num_columns(); ++col_idx) {
    RETURN_NOT_OK(values_.at(col_idx).Deserialize(column_type(col_idx), client, data));
  }
  return Status::OK();
}

string YQLRow::ToString() const {
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

YQLRow& YQLRow::operator=(const YQLRow& other) {
  this->~YQLRow();
  new(this) YQLRow(other);
  return *this;
}

YQLRow& YQLRow::operator=(YQLRow&& other) {
  this->~YQLRow();
  new(this) YQLRow(other);
  return *this;
}

//-------------------------------------- YQL row block --------------------------------------
YQLRowBlock::YQLRowBlock(const Schema& schema, const vector<ColumnId>& column_ids)
    : schema_(new Schema()) {
  // TODO: is there a better way to report errors here?
  CHECK_OK(schema.CreateProjectionByIdsIgnoreMissing(column_ids, schema_.get()));
}

YQLRowBlock::YQLRowBlock(const Schema& schema) : schema_(new Schema(schema)) {
}

YQLRowBlock::~YQLRowBlock() {
}

YQLRow& YQLRowBlock::Extend() {
  rows_.emplace_back(schema_);
  return rows_.back();
}

Status YQLRowBlock::AddRow(const YQLRow& row) {
  // TODO: check for schema compatibility between YQLRow and YQLRowBlock.
  rows_.push_back(row);
  return Status::OK();
}

string YQLRowBlock::ToString() const {
  string s = "{ ";
  for (size_t i = 0; i < rows_.size(); i++) {
    if (i > 0) { s+= ", "; }
    s += rows_[i].ToString();
  }
  s += " }";
  return s;
}

void YQLRowBlock::Serialize(const YQLClient client, faststring* buffer) const {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  CQLEncodeLength(rows_.size(), buffer);
  for (const auto& row : rows_) {
    row.Serialize(client, buffer);
  }
}

Status YQLRowBlock::Deserialize(const YQLClient client, Slice* data) {
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

Status YQLRowBlock::GetRowCount(const YQLClient client, const std::string& data, size_t* count) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  int32_t cnt = 0;
  Slice slice(data);
  RETURN_NOT_OK(CQLDecodeNum(sizeof(cnt), NetworkByteOrder::Load32, &slice, &cnt));
  *count = cnt;
  return Status::OK();
}

Status YQLRowBlock::AppendRowsData(
    const YQLClient client, const std::string& src, std::string* dst) {
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
YQLValuePB EvaluateValue(const YQLExpressionPB& expr, const YQLTableRow& table_row) {
  switch (expr.expr_case()) {
    case YQLExpressionPB::ExprCase::kColumnId: {
      const auto column_id = ColumnId(expr.column_id());
      const auto it = table_row.find(column_id);
      return it != table_row.end() ? it->second.value : YQLValuePB();
    }
    case YQLExpressionPB::ExprCase::kSubscriptedCol: {
      const auto column_id = ColumnId(expr.subscripted_col().column_id());
      const auto it = table_row.find(column_id);
      if (it == table_row.end()) {
        return YQLValuePB();
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
            // YQL list index starts from 1 not 0
            if (index > 0 && index <= list.elems_size()) {
              return list.elems(index - 1);
            } // otherwise we return null below
          }
        }
      }
      // default (if collection entry not found) is to return null value
      return YQLValuePB();
    }
    case YQLExpressionPB::ExprCase::kValue:
      return expr.value();

    case YQLExpressionPB::ExprCase::kBfcall: {
      switch(static_cast<bfyql::BFOpcode>(expr.bfcall().bfopcode())) {
        case bfyql::BFOpcode::OPCODE_ttl_40: {
          const YQLExpressionPB& column = expr.bfcall().operands(0);
          const auto column_id = ColumnId(column.column_id());
          const auto it = table_row.find(column_id);
          CHECK(it != table_row.end());
          YQLValuePB ttl_seconds_pb;
          if (it->second.ttl_seconds != -1) {
            ttl_seconds_pb.set_int64_value(it->second.ttl_seconds);
          } else {
            YQLValue::SetNull(&ttl_seconds_pb);
          }
          return ttl_seconds_pb;
        }
        case bfyql::BFOpcode::OPCODE_writetime_41: {
          const YQLExpressionPB& column = expr.bfcall().operands(0);
          const auto column_id = ColumnId(column.column_id());
          const auto it = table_row.find(column_id);
          CHECK(it != table_row.end());
          YQLValuePB write_time_pb;
          write_time_pb.set_int64_value(it->second.write_time);
          return write_time_pb;
        }
        default:
          LOG(FATAL) << "Error: invalid builtin function: " << expr.bfcall().bfopcode();
      }
      break;
    }
    case YQLExpressionPB::ExprCase::kCondition: FALLTHROUGH_INTENDED;
    case YQLExpressionPB::ExprCase::EXPR_NOT_SET:
      break;
    // default: fall through
  }
  LOG(FATAL) << "Internal error: invalid column or value expression: " << expr.expr_case();
}

// Evaluate an IN (...) condition.
Status EvaluateInCondition(const google::protobuf::RepeatedPtrField<yb::YQLExpressionPB> &operands,
                           const YQLTableRow &row,
                           bool *result) {
  // Expecting two operands, second should be list of elements.
  CHECK_EQ(operands.size(), 2);
  *result = false;
  YQLValuePB left = EvaluateValue(operands.Get(0), row);
  YQLValuePB right = EvaluateValue(operands.Get(1), row);

  for (const YQLValuePB& elem : right.list_value().elems()) {
    if (!YQLValue::Comparable(left, elem)) return STATUS(RuntimeError, "values not comparable");
    if (left == elem) {
      *result = true;
      break;
    }
  }
  return Status::OK();
}

// Evaluate a BETWEEN(a, b) condition.
Status EvaluateBetweenCondition(
    const google::protobuf::RepeatedPtrField<yb::YQLExpressionPB> &operands,
    const YQLTableRow &row, bool *result) {
  CHECK_EQ(operands.size(), 3);
  YQLValuePB v = EvaluateValue(operands.Get(0), row);
  YQLValuePB lower_bound = EvaluateValue(operands.Get(1), row);
  YQLValuePB upper_bound = EvaluateValue(operands.Get(2), row);
  if (!YQLValue::Comparable(v, lower_bound) || !YQLValue::Comparable(v, upper_bound)) {
    return STATUS(RuntimeError, "values not comparable");
  }
  *result = (lower_bound <= v && v <= upper_bound);
  return Status::OK();
}

} // namespace

// Evaluate a condition for the given row.
Status EvaluateCondition(
    const YQLConditionPB &condition, const YQLTableRow &table_row, bool *result) {
  const auto &operands = condition.operands();
  switch (condition.op()) {
    case YQL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), YQLExpressionPB::ExprCase::kCondition);
      RETURN_NOT_OK(EvaluateCondition(operands.Get(0).condition(), table_row, result));
      *result = !*result;
      return Status::OK();
    }
    case YQL_OP_IS_NULL: {
      CHECK_EQ(operands.size(), 1);
      *result = YQLValue::IsNull(EvaluateValue(operands.Get(0), table_row));
      return Status::OK();
    }
    case YQL_OP_IS_NOT_NULL: {
      CHECK_EQ(operands.size(), 1);
      *result = !YQLValue::IsNull(EvaluateValue(operands.Get(0), table_row));
      return Status::OK();
    }
    case YQL_OP_IS_TRUE: {
      CHECK_EQ(operands.size(), 1);
      YQLValuePB v = EvaluateValue(operands.Get(0), table_row);
      if (YQLValue::type(v) != YQLValue::InternalType::kBoolValue)
        return STATUS(RuntimeError, "not a bool value");
      *result = (!YQLValue::IsNull(v) && YQLValue::bool_value(v));
      return Status::OK();
    }
    case YQL_OP_IS_FALSE: {
      CHECK_EQ(operands.size(), 1);
      YQLValuePB v = EvaluateValue(operands.Get(0), table_row);
      if (YQLValue::type(v) != YQLValue::InternalType::kBoolValue)
        return STATUS(RuntimeError, "not a bool value");
      *result = (!YQLValue::IsNull(v) && !YQLValue::bool_value(v));
      return Status::OK();
    }

#define YQL_EVALUATE_RELATIONAL_OP(op, operands, row, result)         \
      do {                                                            \
        CHECK_EQ(operands.size(), 2);                                 \
        const YQLValuePB left = EvaluateValue(operands.Get(0), table_row);  \
        const YQLValuePB right = EvaluateValue(operands.Get(1), table_row); \
        if (!YQLValue::Comparable(left, right))                       \
          return STATUS(RuntimeError, "values not comparable");       \
        *result = (left op right);                                    \
      } while (0)

    case YQL_OP_EQUAL: {
      YQL_EVALUATE_RELATIONAL_OP(==, operands, table_row, result);
      return Status::OK();
    }
    case YQL_OP_LESS_THAN: {
      YQL_EVALUATE_RELATIONAL_OP(<, operands, table_row, result);
      return Status::OK();
    }
    case YQL_OP_LESS_THAN_EQUAL: {
      YQL_EVALUATE_RELATIONAL_OP(<=, operands, table_row, result);
      return Status::OK();
    }
    case YQL_OP_GREATER_THAN: {
      YQL_EVALUATE_RELATIONAL_OP(>, operands, table_row, result);
      return Status::OK();
    }
    case YQL_OP_GREATER_THAN_EQUAL: {
      YQL_EVALUATE_RELATIONAL_OP(>=, operands, table_row, result);
      return Status::OK();
    }
    case YQL_OP_NOT_EQUAL: {
      YQL_EVALUATE_RELATIONAL_OP(!=, operands, table_row, result);
      return Status::OK();
    }

#undef YQL_EVALUATE_RELATIONAL_OP

// Evaluate a logical AND/OR operation. To see if we can short-circuit, we do
// "(left op true) ^ (left op false)" that applies the "left" result with both
// "true" and "false" and only if the answers are different (i.e. exclusive or ^)
// that we should evaluate the "right" result also.
#define YQL_EVALUATE_LOGICAL_OP(op, operands, table_row, result)                            \
      do {                                                                            \
        CHECK_EQ(operands.size(), 2);                                                 \
        CHECK_EQ(operands.Get(0).expr_case(), YQLExpressionPB::ExprCase::kCondition); \
        CHECK_EQ(operands.Get(1).expr_case(), YQLExpressionPB::ExprCase::kCondition); \
        bool left = false, right = false;                                             \
        RETURN_NOT_OK(EvaluateCondition(operands.Get(0).condition(), table_row, &left));    \
        if ((left op true) ^ (left op false)) {                                       \
          RETURN_NOT_OK(EvaluateCondition(operands.Get(1).condition(), table_row, &right)); \
        }                                                                             \
        *result = (left op right);                                                    \
      } while (0)

    case YQL_OP_AND: {
      YQL_EVALUATE_LOGICAL_OP(&&, operands, table_row, result);
      return Status::OK();
    }
    case YQL_OP_OR: {
      YQL_EVALUATE_LOGICAL_OP(||, operands, table_row, result);
      return Status::OK();
    }

#undef YQL_EVALUATE_LOGICAL_OP

    case YQL_OP_LIKE:     FALLTHROUGH_INTENDED;
    case YQL_OP_NOT_LIKE:
      return STATUS(RuntimeError, "LIKE operator not supported yet");

    case YQL_OP_IN: {
      return EvaluateInCondition(operands, table_row, result);
    }
    case YQL_OP_NOT_IN: {
      RETURN_NOT_OK(EvaluateInCondition(operands, table_row, result));
      *result = !*result;
      return Status::OK();
    }

    case YQL_OP_BETWEEN: {
      return EvaluateBetweenCondition(operands, table_row, result);
    }
    case YQL_OP_NOT_BETWEEN: {
      RETURN_NOT_OK(EvaluateBetweenCondition(operands, table_row, result));
      *result = !*result;
      return Status::OK();
    }

    // When a row exists, the primary key columns are always populated in the row (value-map) by
    // DocRowwiseIterator and only when it exists. Therefore, the row exists if and only if
    // the row (value-map) is not empty.
    case YQL_OP_EXISTS: {
      *result = !table_row.empty();
      return Status::OK();
    }
    case YQL_OP_NOT_EXISTS: {
      *result = table_row.empty();
      return Status::OK();
    }

    case YQL_OP_NOOP:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: illegal or unknown operator " << condition.op();
}

} // namespace yb

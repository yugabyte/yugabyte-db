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

#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_dml.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::placeholders::_1;
using std::placeholders::_2;

//--------------------------------------------------------------------------------------------------

PgExpr::PgExpr(PgExpr::Opcode op, InternalType internal_type)
    : op_(op), internal_type_(internal_type) {
}

PgExpr::~PgExpr() {
}

Status PgExpr::Prepare(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  // For expression that doesn't need to be setup and prepared at construction time.
  return Status::OK();
}

Status PgExpr::Eval(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  // Expressions that are neither bind_variable nor constant don't need to be updated.
  // Only values for bind variables and constants need to be updated in the SQL requests.
  return Status::OK();
}

void PgExpr::TranslateText(Slice *yb_cursor, const PgWireDataHeader& header,
                           PgTuple *pg_tuple, int index) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }

  int64_t text_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &text_size);
  yb_cursor->remove_prefix(read_size);

  pg_tuple->Write(index, header, yb_cursor->cdata(), text_size);
  yb_cursor->remove_prefix(text_size);
}

void PgExpr::TranslateComplex(Slice *yb_cursor, const PgWireDataHeader& header,
                              PgTuple *pg_tuple, int index) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }

  int64_t binary_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &binary_size);
  yb_cursor->remove_prefix(read_size);

  pg_tuple->Write(index, header, yb_cursor->data(), binary_size);
  yb_cursor->remove_prefix(binary_size);
}

//--------------------------------------------------------------------------------------------------

PgConstant::PgConstant(int16_t value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kInt16Value) {
  if (!is_null) {
    ql_value_.set_int16_value(value);
  }
  TranslateData = TranslateNumber<int16_t>;
}

PgConstant::PgConstant(int32_t value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kInt32Value) {
  if (!is_null) {
    ql_value_.set_int32_value(value);
  }
  TranslateData = TranslateNumber<int32_t>;
}

PgConstant::PgConstant(int64_t value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kInt64Value) {
  if (!is_null) {
    ql_value_.set_int64_value(value);
  }
  TranslateData = TranslateNumber<int64_t>;
}

PgConstant::PgConstant(float value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kFloatValue) {
  if (!is_null) {
    ql_value_.set_float_value(value);
  }
  TranslateData = TranslateNumber<float>;
}

PgConstant::PgConstant(double value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kDoubleValue) {
  if (!is_null) {
    ql_value_.set_double_value(value);
  }
  TranslateData = TranslateNumber<double>;
}

PgConstant::PgConstant(const char *value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kStringValue) {
  if (!is_null) {
    ql_value_.set_string_value(value);
  }
  TranslateData = TranslateText;
}

PgConstant::PgConstant(const char *value, size_t bytes, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kStringValue) {
  if (!is_null) {
    ql_value_.set_binary_value(value);
  }
  TranslateData = TranslateText;
}

PgConstant::~PgConstant() {
}

void PgConstant::UpdateConstant(int16_t value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_int16_value(value);
  }
}

void PgConstant::UpdateConstant(int32_t value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_int32_value(value);
  }
}

void PgConstant::UpdateConstant(int64_t value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_int64_value(value);
  }
}

void PgConstant::UpdateConstant(float value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_float_value(value);
  }
}

void PgConstant::UpdateConstant(double value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_double_value(value);
  }
}

void PgConstant::UpdateConstant(const char *value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_string_value(value);
  }
}

void PgConstant::UpdateConstant(const char *value, size_t bytes, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_string_value(value, bytes);
  }
}

Status PgConstant::Eval(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  QLValuePB *result = expr_pb->mutable_value();
  *result = ql_value_;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgColumnRef::PgColumnRef(int attr_num)
  : PgExpr(PgExpr::Opcode::PG_EXPR_COLREF), attr_num_(attr_num) {
}

PgColumnRef::~PgColumnRef() {
}

Status PgColumnRef::Prepare(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  const PgColumn *col;
  RETURN_NOT_OK(pg_stmt->PrepareColumnForRead(attr_num_, expr_pb, &col));

  switch (col->internal_type()) {
    case QLValue::InternalType::kInt16Value:
      TranslateData = TranslateNumber<int16_t>;
      break;
    case QLValue::InternalType::kInt32Value:
      TranslateData = TranslateNumber<int32_t>;
      break;
    case QLValue::InternalType::kInt64Value:
      TranslateData = TranslateNumber<int64_t>;
      break;
    case QLValue::InternalType::kFloatValue:
      TranslateData = TranslateNumber<float>;
      break;
    case QLValue::InternalType::kDoubleValue:
      TranslateData = TranslateNumber<double>;
      break;
    case QLValue::InternalType::kStringValue:

      TranslateData = TranslateText;
      break;

    case QLValue::InternalType::kDecimalValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kTimestampValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kInetaddressValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kJsonbValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kUuidValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kTimeuuidValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kBoolValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kBinaryValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kVarintValue:
      return STATUS_SUBSTITUTE(NotSupported, "Datatype $0 is not yet supported",
                               col->internal_type());

    case QLValue::InternalType::kInt8Value: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kMapValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kSetValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kListValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kFrozenValue:
      return STATUS_SUBSTITUTE(NotSupported, "CQL type $0 is not supported in Postgres",
                               col->internal_type());

    case QLValue::InternalType::VALUE_NOT_SET:
      return STATUS(Corruption, "Unexpected code path");
      break;
  }

  return Status::OK();
}

}  // namespace pggate
}  // namespace yb
